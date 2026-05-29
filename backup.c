#include <psp2/types.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>

#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/devctl.h>
#include <psp2/io/stat.h>
#include <psp2/rtc.h>
#include <psp2/vshbridge.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "backup.h"
#include "ui.h"
#include "hash.h"
#include "zip_utils.h"

BackupEntry entries[] = {
    { "Plugins (tai)",       "ur0:tai",                              1 },
    { "SaveData",            "ux0:user/00/savedata",                 1 },
    { "Licenses",            "ux0:license",                          1 },
    { "Game Trophies",       "ux0:user/00/trophy",                   1 },
    { "LiveArea DB",         "ur0:shell/db",                         1 },
    { "Adrenaline",          "ux0:pspemu/PSP",                       1 },
    { "Themes / Livearea",   "ux0:theme",                            0 },
    { "Screenshots",         "ux0:picture",                          0 },
    { "Music",               "ux0:music",                            0 },
    { "Videos",              "ux0:video",                             0 },
    { "Downloads",           "ux0:download",                         0 },
    { "Boot Config (ur0)",   "ur0:vsh/boot",                         0 },
    { "tm0_licenses",        "tm0:npdrm/act.dat|tm0:psmdrm/act.dat", 0 },
    { "System Registry",     "vd0:registry",                         0 },
    { "App metadata",        "ux0:app",                              0 },
    { "RetroArch Data",      "ux0:data/retroarch",                   0 },
    { "System Playlog",      "ur0:user/00/shell/playlog",            0 },
    { "Custom Folder",       "",                                      0 },
};

char g_backup_root[PATH_MAX_SIZE] = "ux0:data/VitaVault";

int ENTRY_COUNT = sizeof(entries) / sizeof(entries[0]);

ProfileType current_profile = PROFILE_NORMAL;
FTPConfig ftp_config;
BackupInfo g_backups[MAX_BACKUPS];
int g_backup_count = 0;
char g_last_backup_path[PATH_MAX_SIZE + 128] = "";
char g_last_log_path[PATH_MAX_SIZE + 128] = "";

const char *profile_names[] = { "NONE", "MINIMAL", "NORMAL", "COMPLETE" };

static ProgressCallback g_progress_cb = NULL;
static int g_prog_eidx = 0;
static int g_prog_total_entries = 0;
static const char *g_prog_entry_name = NULL;

static int vshIoMount(SceVshMountId id, const char *path, int permission, int a4, int a5, int a6) {
    uint32_t buf[3];
    buf[0] = a4;
    buf[1] = a5;
    buf[2] = a6;
    return _vshIoMount(id, path, permission, buf);
}

static void remount(SceVshMountId id) {
    vshIoUmount(id, 0, 0, 0);
    vshIoUmount(id, 1, 0, 0);
    vshIoMount(id, NULL, 0, 0, 0, 0);
}

void mount_all_partitions() {

    int tm0_res = vshIoMount(0x600, NULL, 2, 0, 0, 0);
    

    if (tm0_res < 0) {
        vshIoMount(0x500, NULL, 2, 0, 0, 0);  
    }
    if (tm0_res < 0) {
        vshIoMount(0x400, NULL, 2, 0, 0, 0);  
    }

    
    vshIoMount(0x200, NULL, 0, 0, 0, 0);  
    vshIoMount(0x000, NULL, 0, 0, 0, 0);  
    vshIoMount(0x300, NULL, 0, 0, 0, 0);  
    remount(0x800);                         
    vshIoMount(0xF00, NULL, 0, 0, 0, 0);  
}


void format_size(char *out, int out_size, SceOff bytes) {
    if (bytes < 1024)
        snprintf(out, out_size, "%lld B", (long long)bytes);
    else if (bytes < 1024 * 1024)
        snprintf(out, out_size, "%.1f KB", (double)bytes / 1024.0);
    else if (bytes < (SceOff)1024 * 1024 * 1024)
        snprintf(out, out_size, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    else
        snprintf(out, out_size, "%.1f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
}

void get_timestamp(char *out, int size) {
    SceDateTime time;
    sceRtcGetCurrentClockLocalTime(&time);
    snprintf(out, size, "%04d-%02d-%02d_%02d-%02d-%02d",
             time.year, time.month, time.day,
             time.hour, time.minute, time.second);
}

void create_dir(const char *path) {
    sceIoMkdir(path, 0777);
}

void build_backup_root(char *out, int size) {
    char ts[64];
    get_timestamp(ts, sizeof(ts));
    snprintf(out, size, "%s/%s", g_backup_root, ts);
}

SceOff get_free_space(const char *path) {
    SceIoDevInfo info;
    memset(&info, 0, sizeof(info));
    int ret = sceIoDevctl(path, 0x3000003, NULL, 0, &info, sizeof(info));
    if (ret < 0) return 0;
    return info.free_size;
}

int check_unsafe_permissions() {
    SceUID dfd = sceIoDopen("os0:/");
    if (dfd >= 0) {
        sceIoDclose(dfd);
        return 1; 
    }
    return 0; 
}



int count_files_recursive(const char *path, int *file_count, SceOff *total_bytes) {
    SceUID dir = sceIoDopen(path);
    if (dir < 0) return -1;

    SceIoDirent ent;
    memset(&ent, 0, sizeof(ent));

    while (sceIoDread(dir, &ent) > 0) {
        if (!strcmp(ent.d_name, ".") || !strcmp(ent.d_name, "..")) {
            memset(&ent, 0, sizeof(ent));
            continue;
        }
        char full_path[PATH_MAX_SIZE];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, ent.d_name);

        
        int is_dir = SCE_S_ISDIR(ent.d_stat.st_mode);
        if (ent.d_stat.st_mode == 0) {
            SceIoStat st;
            if (sceIoGetstat(full_path, &st) >= 0) {
                is_dir = SCE_S_ISDIR(st.st_mode);
                ent.d_stat.st_size = st.st_size;
            }
        }

        if (is_dir) {
            count_files_recursive(full_path, file_count, total_bytes);
        } else {
            if (file_count)  (*file_count)++;
            if (total_bytes) (*total_bytes) += ent.d_stat.st_size;
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(dir);
    return 0;
}


static int check_tm0_access() {
    
    SceUID dir = sceIoDopen("tm0:");
    if (dir >= 0) {
        sceIoDclose(dir);
        return 1; 
    }
    
    
    int device_ids[] = {0x600, 0x500, 0x400, 0x700, 0x900};
    for (int i = 0; i < 5; i++) {
        vshIoMount(device_ids[i], NULL, 2, 0, 0, 0);
        dir = sceIoDopen("tm0:");
        if (dir >= 0) {
            sceIoDclose(dir);
            return 1; 
        }
    }
    
    
    for (int i = 0; i < 5; i++) {
        vshIoMount(device_ids[i], NULL, 1, 0, 0, 0);
        dir = sceIoDopen("tm0:");
        if (dir >= 0) {
            sceIoDclose(dir);
            return 1;
        }
    }
    
    return 0; 
}

void prevent_standby(void) {
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
}

int copy_file(const char *src, const char *dst, CopyContext *ctx, BackupLog *log) {
    prevent_standby();
    SceUID in = sceIoOpen(src, SCE_O_RDONLY, 0);
    if (in < 0) {
        
        char err_log[PATH_MAX_SIZE + 64];
        snprintf(err_log, sizeof(err_log), "  SKIP: %s (sceIoOpen Error 0x%08X)\n", src, (unsigned int)in);
        if (log) log_write(log, err_log);

        if (ctx) {
            ctx->has_error = 0;
            ctx->current_file++;
        }
        return in; 
    }

    SceUID out = sceIoOpen(dst, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (out < 0) {
        sceIoClose(in);
        
        char err_log[PATH_MAX_SIZE + 64];
        snprintf(err_log, sizeof(err_log), "  SKIP: Cannot create destination %s (Error 0x%08X)\n", dst, (unsigned int)out);
        if (log) log_write(log, err_log);
        
        if (ctx) ctx->has_error = 0;
        return out; 
    }

    char buffer[32768];
    int bytes_since_update = 0;
    while (1) {
        int r = sceIoRead(in, buffer, sizeof(buffer));
        if (r <= 0) break;
        int w = sceIoWrite(out, buffer, r);
        if (w < 0) { if (ctx) ctx->has_error = 1; break; }

        
        SceCtrlData pad_check;
        if (sceCtrlPeekBufferPositive(0, &pad_check, 1) > 0) {
            if (pad_check.buttons & SCE_CTRL_CIRCLE) {
                if (ctx) ctx->cancel = 1;
                log_write(NULL, "User requested cancellation...\n");
                break;
            }
        }

        if (ctx) {
            ctx->current_bytes += r;
            bytes_since_update += r;

            
            if (g_progress_cb && bytes_since_update >= 524288) {
                g_progress_cb(g_prog_entry_name, g_prog_eidx, g_prog_total_entries,
                              ctx->current_bytes, ctx->total_bytes,
                              ctx->current_file, ctx->total_files);
                bytes_since_update = 0;
            }
        }
    }
    sceIoClose(in);
    sceIoClose(out);
    if (ctx) {
        ctx->current_file++;
        if (ctx->total_files > 0 && g_progress_cb) {
            g_progress_cb(g_prog_entry_name, g_prog_eidx, g_prog_total_entries,
                          ctx->current_bytes, ctx->total_bytes,
                          ctx->current_file, ctx->total_files);
        }
    }
    return 0;
}

int copy_directory(const char *src, const char *dst, CopyContext *ctx, BackupLog *log) {
    create_dir(dst);
    SceUID dir = sceIoDopen(src);
    if (ctx && ctx->cancel) return -1;

    if (dir < 0) {
        if (ctx) ctx->has_error = 1;
        return -1;
    }

    
    int is_tm0 = (strncmp(src, "tm0:", 4) == 0);
    int tm0_failures = 0;

    SceIoDirent ent;
    memset(&ent, 0, sizeof(ent));
    while (sceIoDread(dir, &ent) > 0) {
        if (!strcmp(ent.d_name, ".") || !strcmp(ent.d_name, "..")) {
            memset(&ent, 0, sizeof(ent));
            continue;
        }
        if (ctx && ctx->cancel) break;

        char sp[PATH_MAX_SIZE], dp[PATH_MAX_SIZE];
        snprintf(sp, sizeof(sp), "%s/%s", src, ent.d_name);
        snprintf(dp, sizeof(dp), "%s/%s", dst, ent.d_name);
        
        if (SCE_S_ISDIR(ent.d_stat.st_mode)) {
            int dir_result = copy_directory(sp, dp, ctx, log);
            if (is_tm0 && dir_result < 0) {
                tm0_failures++;
                char fail_log[PATH_MAX_SIZE + 64];
                snprintf(fail_log, sizeof(fail_log), "  tm0: Failed to copy directory: %s\n", sp);
                if (log) log_write(log, fail_log);
            }
        } else {
            int file_result = copy_file(sp, dp, ctx, log);
            if (is_tm0 && file_result < 0) {
                tm0_failures++;
                char fail_log[PATH_MAX_SIZE + 64];
                snprintf(fail_log, sizeof(fail_log), "  tm0: Failed to copy file: %s\n", sp);
                if (log) log_write(log, fail_log);
            }
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(dir);
    
    if (is_tm0 && tm0_failures > 0) {
        char summary[128];
        snprintf(summary, sizeof(summary), "  tm0: Total failures: %d\n", tm0_failures);
        if (log) log_write(log, summary);
        if (ctx) ctx->has_error = 1;
    }
    
    return 0;
}

int restore_entry(const char *src, const char *dst, int *fr, SceOff *br, int *errs) {
    create_dir(dst);
    SceUID dir = sceIoDopen(src);
    if (dir < 0) return -1;

    SceIoDirent ent;
    memset(&ent, 0, sizeof(ent));
    while (sceIoDread(dir, &ent) > 0) {
        if (!strcmp(ent.d_name, ".") || !strcmp(ent.d_name, "..")) {
            memset(&ent, 0, sizeof(ent));
            continue;
        }
        char sp[PATH_MAX_SIZE], dp[PATH_MAX_SIZE];
        snprintf(sp, sizeof(sp), "%s/%s", src, ent.d_name);
        snprintf(dp, sizeof(dp), "%s/%s", dst, ent.d_name);

        if (SCE_S_ISDIR(ent.d_stat.st_mode)) {
            restore_entry(sp, dp, fr, br, errs);
        } else {
            char *ls = strrchr(dp, '/');
            if (ls) { *ls = '\0'; create_dir(dp); *ls = '/'; }

            SceUID in = sceIoOpen(sp, SCE_O_RDONLY, 0);
            if (in < 0) { if (errs) (*errs)++; continue; }

            SceUID out = sceIoOpen(dp, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
            if (out < 0) { sceIoClose(in); if (errs) (*errs)++; continue; }

            char buf[16384];
            while (1) {
                int r = sceIoRead(in, buf, sizeof(buf));
                if (r <= 0) break;
                sceIoWrite(out, buf, r);
                if (br) (*br) += r;
            }
            sceIoClose(in);
            sceIoClose(out);
            if (fr) (*fr)++;
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(dir);
    return 0;
}

void log_init(BackupLog *log) {
    char log_dir[PATH_MAX_SIZE + 64];
    snprintf(log_dir, sizeof(log_dir), "%s/logs", g_backup_root);
    create_dir(log_dir);
    create_dir(g_backup_root);

    char ts[64];
    get_timestamp(ts, sizeof(ts));
    strcpy(log->start_time, ts);
    int needed = snprintf(log->path, sizeof(log->path), "%s/%s.txt", log_dir, ts);
    if (needed >= (int)sizeof(log->path)) {
        snprintf(log->path, sizeof(log->path), "%s.txt", ts);
    }

    log->fd = sceIoOpen(log->path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    log->total_entries = 0;
    log->total_files = 0;
    log->total_bytes = 0;
    log->errors = 0;

    if (log->fd >= 0) {
        char h[2048];
        snprintf(h, sizeof(h),
            "============================================\n"
            "  VitaVault Backup Log\n"
            "============================================\n"
            "Started : %s\n"
            "Root    : %s\n"
            "--------------------------------------------\n\n",
            ts, g_backup_root);
        sceIoWrite(log->fd, h, strlen(h));
    }
}

void log_write_entry_header(BackupLog *log, int idx, int total, const char *name,
                            const char *src, const char *dst, int fc, SceOff tb) {
    if (!log || log->fd < 0) return;
    char buf[2048], sz[32];
    format_size(sz, sizeof(sz), tb);
    snprintf(buf, sizeof(buf),
        "[%d/%d] %s\n  Source: %s\n  Dest: %s\n  Files: %d\n  Size: %s\n",
        idx, total, name, src, dst, fc, sz);
    sceIoWrite(log->fd, buf, strlen(buf));
}

void log_write_entry_result(BackupLog *log, int has_error) {
    if (!log || log->fd < 0) return;
    char buf[256];
    snprintf(buf, sizeof(buf), "  Status: %s\n\n", has_error ? "ERRORS" : "OK");
    sceIoWrite(log->fd, buf, strlen(buf));
}

void log_write(BackupLog *log, const char *text) {
    if (log && log->fd >= 0) sceIoWrite(log->fd, text, strlen(text));
}

void log_close(BackupLog *log) {
    if (!log) return;
    char footer[2048], end_time[64], sz[32];
    get_timestamp(end_time, sizeof(end_time));
    format_size(sz, sizeof(sz), log->total_bytes);
    snprintf(footer, sizeof(footer),
        "--------------------------------------------\n"
        "  SUMMARY\n"
        "--------------------------------------------\n"
        "Entries: %d\nFiles: %d\nSize: %s\nErrors: %d\n"
        "Started: %s\nEnded: %s\nStatus: %s\n"
        "============================================\n",
        log->total_entries, log->total_files, sz, log->errors,
        log->start_time, end_time,
        log->errors == 0 ? "SUCCESS" : "COMPLETED WITH ERRORS");
    if (log->fd >= 0) {
        sceIoWrite(log->fd, footer, strlen(footer));
        sceIoClose(log->fd);
        log->fd = -1;
    }
}

void save_config() {
    create_dir("ux0:data/VitaVault");
    SceUID fd = sceIoOpen(CONFIG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) return;

    char buf[4096];
    int n = snprintf(buf, sizeof(buf),
        "# VitaVault Configuration\n"
        "profile=%d\n"
        "backup_root=%s\n"
        "ftp_enabled=%d\n"
        "compression=%d\n"
        "checksum=%d\n"
        "# Entry states\n",
        (int)current_profile, g_backup_root,
        ftp_config.enabled,
        ftp_config.compression, ftp_config.checksum);
    sceIoWrite(fd, buf, n);

    for (int i = 0; i < ENTRY_COUNT; i++) {
        n = snprintf(buf, sizeof(buf), "%s=%d|%s\n", entries[i].name, entries[i].enabled, entries[i].source);
        sceIoWrite(fd, buf, n);
    }
    sceIoClose(fd);
}

void load_config() {
    strcpy(ftp_config.host, FTP_DEFAULT_HOST);
    ftp_config.port = FTP_DEFAULT_PORT;
    strcpy(ftp_config.user, FTP_DEFAULT_USER);
    strcpy(ftp_config.pass, FTP_DEFAULT_PASS);
    strcpy(ftp_config.remote_dir, FTP_DEFAULT_DIR);
    ftp_config.enabled = 0;

    SceUID fd = sceIoOpen(CONFIG_PATH, SCE_O_RDONLY, 0);
    if (fd < 0) return;

    char line[MAX_LINE];
    int pos = 0;
    char ch;
    while (sceIoRead(fd, &ch, 1) == 1 && pos < MAX_LINE - 1) {
        if (ch == '\n') {
            line[pos] = '\0';
            pos = 0;
            if (line[0] == '#' || line[0] == '\0') continue;

            if (strncmp(line, "profile=", 8) == 0) {
                int p = atoi(line + 8);
                if (p >= 0 && p <= 3) current_profile = (ProfileType)p;
            } else if (strncmp(line, "backup_root=", 12) == 0) {
                strncpy(g_backup_root, line + 12, PATH_MAX_SIZE - 1);
            } else if (strncmp(line, "ftp_enabled=", 12) == 0)
                ftp_config.enabled = atoi(line + 12);
            else if (strncmp(line, "ftp_host=", 9) == 0)
                strncpy(ftp_config.host, line + 9, 255);
            else if (strncmp(line, "ftp_port=", 9) == 0)
                ftp_config.port = atoi(line + 9);
            else if (strncmp(line, "ftp_user=", 9) == 0)
                strncpy(ftp_config.user, line + 9, 63);
            else if (strncmp(line, "ftp_pass=", 9) == 0)
                strncpy(ftp_config.pass, line + 9, 63);
            else if (strncmp(line, "ftp_dir=", 8) == 0)
                strncpy(ftp_config.remote_dir, line + 8, 255);
            else {
                char *eq = strchr(line, '=');
                if (eq) {
                    *eq = '\0';
                    char *val = eq + 1;
                    char *pipe = strchr(val, '|');
                    for (int i = 0; i < ENTRY_COUNT; i++) {
                        if (strcmp(entries[i].name, line) == 0) {
                            if (pipe) {
                                *pipe = '\0';
                                strncpy(entries[i].source, pipe + 1, PATH_MAX_SIZE - 1);
                            }
                            entries[i].enabled = atoi(val);
                            break;
                        }
                    }
                }
            }
        } else {
            line[pos++] = ch;
        }
    }

    
    if (pos > 0) {
        line[pos] = '\0';
        if (line[0] != '#' && line[0] != '\0') {
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = '\0';
                for (int i = 0; i < ENTRY_COUNT; i++) {
                    if (strcmp(entries[i].name, line) == 0) {
                        entries[i].enabled = atoi(eq + 1);
                        break;
                    }
                }
            }
        }
    }
    sceIoClose(fd);
}

void apply_profile(ProfileType profile) {
    current_profile = profile;
    for (int i = 0; i < ENTRY_COUNT; i++) entries[i].enabled = 0;

    if (profile == PROFILE_NONE) {
        save_config();
        return;
    }

    int core[] = {0, 1, 2};
    for (int i = 0; i < 3; i++) entries[core[i]].enabled = 1;

    if (profile >= PROFILE_NORMAL) {
        
        int norm[] = {3, 4, 5, 15, 16};
        for (int i = 0; i < 5; i++) entries[norm[i]].enabled = 1;
    }

    if (profile >= PROFILE_COMPLETE) {
        
        for (int i = 0; i < ENTRY_COUNT - 1; i++) entries[i].enabled = 1;
        
        if (entries[ENTRY_COUNT - 1].source[0] != '\0')
            entries[ENTRY_COUNT - 1].enabled = 1;
    }
    save_config();
}

void cycle_profile() {
    int max_profile = 3;
    current_profile = (ProfileType)(((int)current_profile + 1) % (max_profile + 1));
    apply_profile(current_profile);
}

int do_backup(char *backup_root, int root_size, BackupLog *log) {
    
    mount_all_partitions();

    
    int tm0_accessible = check_tm0_access();

    build_backup_root(backup_root, root_size);
    create_dir(backup_root);
    memset(log, 0, sizeof(*log));
    log_init(log);

    
    char tm0_status[256];
    snprintf(tm0_status, sizeof(tm0_status), "tm0: access check: %s\n", tm0_accessible ? "SUCCESS" : "FAILED");
    log_write(log, tm0_status);
    
    if (!tm0_accessible) {
       
        log_write(log, "  WARNING: tm0: partition not accessible despite h-encore/Enso\n");
    }

    int active = 0;
    for (int i = 0; i < ENTRY_COUNT; i++)
        if (entries[i].enabled) active++;

    g_progress_cb = draw_backup_running;
    g_prog_total_entries = active;

    int cancelled = 0;
    int eidx = 0;
    for (int i = 0; i < ENTRY_COUNT; i++) {
        if (!entries[i].enabled) continue;
        eidx++;

        
        if (strcmp(entries[i].source, "tm0:") == 0 && !tm0_accessible) {
            char dst[PATH_MAX_SIZE];
            snprintf(dst, sizeof(dst), "%s/%s", backup_root, entries[i].name);
            log_write_entry_header(log, eidx, active, entries[i].name,
                                   entries[i].source, dst, 0, 0);
            char err_msg[PATH_MAX_SIZE + 128];
            snprintf(err_msg, sizeof(err_msg), "  ERROR: tm0: partition not accessible. Tried multiple device IDs and permissions.\n");
            log_write(log, err_msg);
            log->errors++;
            log_write_entry_result(log, 1);
            continue;
        }

        int fc = 0;
        SceOff fs = 0;
        int ok = 0;

        
        char *pipe = strchr(entries[i].source, '|');
        if (pipe) {
            
            char source_copy[PATH_MAX_SIZE];
            strncpy(source_copy, entries[i].source, sizeof(source_copy) - 1);
            source_copy[sizeof(source_copy) - 1] = '\0';
            
            char *file_path = strtok(source_copy, "|");
            while (file_path != NULL) {
                
                while (*file_path == ' ') file_path++;
                
                
                SceIoStat st;
                int stat_result = sceIoGetstat(file_path, &st);
                if (stat_result >= 0) {
                    fc++;
                    fs += st.st_size;
                    char found_log[PATH_MAX_SIZE + 64];
                    snprintf(found_log, sizeof(found_log), "  Found file: %s (size: %lld bytes)\n", file_path, (long long)st.st_size);
                    log_write(log, found_log);
                } else {
                    char not_found_log[PATH_MAX_SIZE + 64];
                    snprintf(not_found_log, sizeof(not_found_log), "  File not found: %s (Error: 0x%08X)\n", file_path, (unsigned int)stat_result);
                    log_write(log, not_found_log);
                }
                file_path = strtok(NULL, "|");
            }
            ok = (fc > 0) ? 0 : -1;
        } else {
            
            ok = count_files_recursive(entries[i].source, &fc, &fs);
        }

        
        if (strcmp(entries[i].source, "tm0:npdrm/act.dat|tm0:psmdrm/act.dat") == 0) {
            char tm0_debug[256];
            snprintf(tm0_debug, sizeof(tm0_debug), "  tm0: Found %d license files, total size: %lld bytes\n", fc, (long long)fs);
            log_write(log, tm0_debug);
            
            if (fc == 0) {
                log_write(log, "  tm0: WARNING - No license files found\n");
            }
        }

        char dst[PATH_MAX_SIZE];
        snprintf(dst, sizeof(dst), "%s/%s", backup_root, entries[i].name);
        log_write_entry_header(log, eidx, active, entries[i].name,
                               entries[i].source, dst, fc, fs);

        if (ok < 0) {
            char err_msg[PATH_MAX_SIZE + 64];
            snprintf(err_msg, sizeof(err_msg), "  ERROR: Cannot access source %s (Permissions?)\n", entries[i].source);
            log_write(log, err_msg);
            log->errors++;
            log_write_entry_result(log, 1);
            continue;
        }

        g_prog_eidx = eidx;
        g_prog_entry_name = entries[i].name;

        CopyContext ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.total_files = fc;
        ctx.total_bytes = fs;
        ctx.cancel = cancelled;

        
        if (strcmp(entries[i].source, "tm0:npdrm/act.dat|tm0:psmdrm/act.dat") == 0) {
            log_write(log, "  tm0: Starting license files copy...\n");
        }

        if (pipe) {
            
            char source_copy[PATH_MAX_SIZE];
            strncpy(source_copy, entries[i].source, sizeof(source_copy) - 1);
            source_copy[sizeof(source_copy) - 1] = '\0';
            
            create_dir(dst);
            
            char *file_path = strtok(source_copy, "|");
            while (file_path != NULL) {
                
                while (*file_path == ' ') file_path++;
                
                
                char *rel_path = strchr(file_path, ':');
                if (rel_path) {
                    rel_path++; 
                } else {
                    rel_path = file_path;
                }
                
                
                char dst_file[2048];
                snprintf(dst_file, sizeof(dst_file), "%s/%s", dst, rel_path);
                
                
                char path_log[2200];
                snprintf(path_log, sizeof(path_log), "  Destination path: %s (length: %zu)\n", dst_file, strlen(dst_file));
                log_write(log, path_log);
                
                
                char *last_slash = strrchr(dst_file, '/');
                if (last_slash) {
                    *last_slash = '\0';
                    create_dir(dst_file);
                    *last_slash = '/';
                }
                
                CopyContext file_ctx;
                memset(&file_ctx, 0, sizeof(file_ctx));
                int copy_result = copy_file(file_path, dst_file, &file_ctx, log);
                
                if (copy_result < 0) {
                    char fail_log[PATH_MAX_SIZE + 64];
                    snprintf(fail_log, sizeof(fail_log), "  Failed to copy: %s (Error 0x%08X)\n", file_path, (unsigned int)copy_result);
                    log_write(log, fail_log);
                    ctx.has_error = 1;
                } else {
                    ctx.current_file++;
                    ctx.current_bytes += file_ctx.current_bytes;
                }
                
                file_path = strtok(NULL, "|");
            }
        } else if (ftp_config.compression) {
            char zip_dst[PATH_MAX_SIZE];
            snprintf(zip_dst, sizeof(zip_dst), "%s/%s.zip", backup_root, entries[i].name);
            log_write(log, "  Mode: Compressed (ZIP)\n");
            zip_directory(entries[i].source, zip_dst, &ctx, entries[i].name, 
                          eidx, active, g_progress_cb);
        } else {
            copy_directory(entries[i].source, dst, &ctx, log);
        }

        
        if (strcmp(entries[i].source, "tm0:npdrm/act.dat|tm0:psmdrm/act.dat") == 0) {
            char tm0_copy_result[256];
            snprintf(tm0_copy_result, sizeof(tm0_copy_result), "  tm0: Copy completed. Files copied: %d/%d, Bytes: %lld/%lld, Error: %d\n",
                     ctx.current_file, ctx.total_files, (long long)ctx.current_bytes, (long long)ctx.total_bytes, ctx.has_error);
            log_write(log, tm0_copy_result);
        }

        if (ctx.cancel) {
            cancelled = 1;
            log_write(log, "!!! BACKUP ABORTED BY USER !!!\n");
        }

        log_write_entry_result(log, ctx.has_error);
        if (ctx.has_error) log->errors++;
        log->total_entries++;
        log->total_files += fc;
        log->total_bytes += fs;

        if (cancelled) break;
    }

    g_progress_cb = NULL;
    g_prog_entry_name = NULL;

    log_close(log);

    strcpy(g_last_backup_path, backup_root);
    strcpy(g_last_log_path, log->path);
    return 0;
}

int check_space_before_backup() {
    SceOff needed = 0;
    for (int i = 0; i < ENTRY_COUNT; i++) {
        if (!entries[i].enabled) continue;
        int fc = 0;
        SceOff fs = 0;
        if (count_files_recursive(entries[i].source, &fc, &fs) == 0)
            needed += fs;
    }
    SceOff free = get_free_space("ux0:");
    if (free == 0) return 1;
    if (needed >= free) return 2;
    return 0;
}

void delete_directory(const char *path) {
    SceUID dir = sceIoDopen(path);
    if (dir < 0) {
        sceIoRemove(path);
        return;
    }
    SceIoDirent ent;
    memset(&ent, 0, sizeof(ent));
    while (sceIoDread(dir, &ent) > 0) {
        if (strcmp(ent.d_name, ".") == 0 || strcmp(ent.d_name, "..") == 0) {
            memset(&ent, 0, sizeof(ent));
            continue;
        }
        char sub[2048];
        snprintf(sub, sizeof(sub), "%s/%s", path, ent.d_name);
        if (SCE_S_ISDIR(ent.d_stat.st_mode)) {
            delete_directory(sub);
        } else {
            sceIoRemove(sub);
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(dir);
    sceIoRmdir(path);
}

void cleanup_old_backups(int keep_count) {
    BackupInfo backups[MAX_BACKUPS];
    int count = list_backups(backups, MAX_BACKUPS);

    if (count <= keep_count) return;

    for (int i = keep_count; i < count; i++) {
        delete_directory(backups[i].path);
        char notify[128];
        snprintf(notify, sizeof(notify), "Auto-purge: removed backup %s", backups[i].timestamp);
        ui_set_notification(notify);
    }
}

int list_backups(BackupInfo *backups, int max) {
    int count = 0;
    SceUID dir = sceIoDopen(g_backup_root);
    if (dir < 0) return 0;

    char dir_names[MAX_BACKUPS][64];
    int dc = 0;
    SceIoDirent ent;
    memset(&ent, 0, sizeof(ent));

    while (sceIoDread(dir, &ent) > 0 && dc < MAX_BACKUPS) {
        if (strcmp(ent.d_name, ".") == 0 ||
            strcmp(ent.d_name, "..") == 0 ||
            strcmp(ent.d_name, "logs") == 0 ||
            strcmp(ent.d_name, "config.cfg") == 0 ||
            ent.d_name[0] == '_') {
            memset(&ent, 0, sizeof(ent));
            continue;
        }
        if (SCE_S_ISDIR(ent.d_stat.st_mode)) {
            strncpy(dir_names[dc], ent.d_name, 63);
            dir_names[dc][63] = '\0';
            dc++;
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(dir);

    for (int i = 0; i < dc - 1; i++) {
        for (int j = i + 1; j < dc; j++) {
            if (strcmp(dir_names[i], dir_names[j]) < 0) {
                char tmp[64];
                strncpy(tmp, dir_names[i], 63);
                strncpy(dir_names[i], dir_names[j], 63);
                strncpy(dir_names[j], tmp, 63);
            }
        }
    }

    for (int i = 0; i < dc && count < max; i++) {
        snprintf(backups[count].path, sizeof(backups[count].path),
                 "%s/%s", g_backup_root, dir_names[i]);
        strncpy(backups[count].timestamp, dir_names[i], 63);
        backups[count].timestamp[63] = '\0';
        backups[count].total_entries = 0;
        backups[count].total_size = 0;
        backups[count].entry_count = 0;

        SceUID sd = sceIoDopen(backups[count].path);
        if (sd >= 0) {
            SceIoDirent se;
            memset(&se, 0, sizeof(se));
            while (sceIoDread(sd, &se) > 0) {
                if (strcmp(se.d_name, ".") == 0 || strcmp(se.d_name, "..") == 0) {
                    memset(&se, 0, sizeof(se));
                    continue;
                }
                if (SCE_S_ISDIR(se.d_stat.st_mode)) {
                    backups[count].entry_count++;
                    char sp[PATH_MAX_SIZE];
                    int needed2 = snprintf(sp, sizeof(sp), "%s/%s", backups[count].path, se.d_name);
                    if (needed2 >= (int)sizeof(sp)) {
                        memset(&se, 0, sizeof(se));
                        continue;
                    }
                    int fc = 0;
                    SceOff fs = 0;
                    count_files_recursive(sp, &fc, &fs);
                    backups[count].total_entries += fc;
                    backups[count].total_size += fs;
                }
                memset(&se, 0, sizeof(se));
            }
            sceIoDclose(sd);
        }
        count++;
    }
    return count;
}