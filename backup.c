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
#include "language.h"

static BackupEntry static_entries[] = {
    { "Adrenaline",          "ux0:pspemu/PSP",                       1 },
    { "App metadata",        "ux0:app",                              0 },
    { "Boot Config (ur0)",   "ur0:vsh/boot",                         0 },
    { "Custom Folder",       "",                                      0 },
    { "DLC / Add-ons",       "ux0:addcont",                          0 },
    { "Downloads",           "ux0:download",                         0 },
    { "Full ux0 Backup",     "ux0:",                                 0 },
    { "Game Patches",        "ux0:patch",                            0 },
    { "Game Trophies",       "ux0:user/00/trophy",                   1 },
    { "Licenses",            "ux0:license",                          1 },
    { "LiveArea DB",         "ur0:shell/db",                         1 },
    { "Music",               "ux0:music",                            0 },
    { "Plugins (tai)",       "ur0:tai",                              1 },
    { "RetroArch Data",      "ux0:data/retroarch",                   0 },
    { "ROMS",                "ux0:roms",                             0 },
    { "SaveData",            "ux0:user/00/savedata",                 1 },
    { "Screenshots",         "ux0:picture",                          0 },
    { "System Playlog",      "ur0:user/00/shell/playlog",            0 },
    { "System Registry",     "vd0:registry",                         0 },
    { "Themes / Livearea",   "ux0:theme",                            0 },
    { "tm0_licenses",        "tm0:npdrm/act.dat|tm0:psmdrm/act.dat", 0 },
    { "User Data (ux0)",     "ux0:data",                             0 },
    { "Videos",              "ux0:video",                             0 },
};

BackupEntry *entries = NULL;
int ENTRY_COUNT = 0;
int ENTRIES_CAPACITY = 0;

char g_backup_root[PATH_MAX_SIZE] = "ux0:VitaVault";

ProfileType current_profile = PROFILE_NONE;
FTPConfig ftp_config;
BackupInfo *g_backups = NULL;
int g_backup_count = 0;
int g_backups_capacity = 0;
char g_last_backup_path[PATH_MAX_SIZE + 128] = "";
char g_preferred_usb_device[64] = "";
char g_preferred_usb_name[64] = "";
char g_last_log_path[PATH_MAX_SIZE + 128] = "";
int g_sidebar_selected = 0;

const char *profile_names[] = { "NONE", "MINIMAL", "NORMAL", "COMPLETE" };

ProgressCallback g_progress_cb = NULL;
int g_prog_eidx = 0;
int g_prog_total_entries = 0;
const char *g_prog_entry_name = NULL;

void init_dynamic_arrays() {
    int static_count = sizeof(static_entries) / sizeof(static_entries[0]);
    
    entries = (BackupEntry *)malloc(static_count * sizeof(BackupEntry));
    if (entries) {
        memcpy(entries, static_entries, static_count * sizeof(BackupEntry));
        ENTRY_COUNT = static_count;
        ENTRIES_CAPACITY = static_count;
    }
    
    g_backups = (BackupInfo *)malloc(INITIAL_BACKUPS * sizeof(BackupInfo));
    if (g_backups) {
        g_backups_capacity = INITIAL_BACKUPS;
        g_backup_count = 0;
    }
}

void cleanup_dynamic_arrays() {
    if (entries) {
        free(entries);
        entries = NULL;
        ENTRY_COUNT = 0;
        ENTRIES_CAPACITY = 0;
    }
    
    if (g_backups) {
        free(g_backups);
        g_backups = NULL;
        g_backup_count = 0;
        g_backups_capacity = 0;
    }
}

int expand_backups_array() {
    int new_capacity = g_backups_capacity * 2;
    if (new_capacity == 0) new_capacity = INITIAL_BACKUPS;
    
    BackupInfo *new_backups = (BackupInfo *)realloc(g_backups, new_capacity * sizeof(BackupInfo));
    if (new_backups) {
        g_backups = new_backups;
        g_backups_capacity = new_capacity;
        return 1;
    }
    return 0;
}

static int vshIoMount(SceVshMountId id, const char *path, int permission, int a4, int a5, int a6) {
    uint32_t buf[3];
    buf[0] = a4;
    buf[1] = a5;
    buf[2] = a6;
    return _vshIoMount(id, path, permission, buf);
}

void remount(SceVshMountId id) {
    vshIoUmount(id, 0, 0, 0);
    vshIoUmount(id, 1, 0, 0);
    vshIoMount(id, NULL, 0, 0, 0, 0);
}

void mount_all_partitions() {
    typedef struct {
        SceVshMountId id;
        const char *name;
        int permission;
        int critical;
    } MountPoint;

    MountPoint mount_points[] = {
        {0x600, "tm0 (primary)", 2, 0},
        {0x500, "tm0 (fallback 1)", 2, 0},
        {0x400, "tm0 (fallback 2)", 2, 0},
        {0x200, "ur0", 0, 1},
        {0x000, "ux0", 0, 1},
        {0x300, "uma0", 0, 0},
        {0xF00, "vd0", 0, 0}
    };

    int total_mounts = 0;
    int failed_mounts = 0;
    int tm0_mounted = 0;

    for (int i = 0; i < sizeof(mount_points) / sizeof(mount_points[0]); i++) {
        MountPoint *mp = &mount_points[i];
        int res = vshIoMount(mp->id, NULL, mp->permission, 0, 0, 0);
        
        total_mounts++;
        
        if (res >= 0) {
            if (mp->id >= 0x400 && mp->id <= 0x600) {
                tm0_mounted = 1;
            }
        } else {
            failed_mounts++;
            if (mp->critical) {
                sceIoMkdir("ux0:data/VitaVault", 0777);
                BackupLog err_log;
                log_init(&err_log);
                char err_msg[256];
                snprintf(err_msg, sizeof(err_msg), "CRITICAL: Failed to mount %s (ID: 0x%X, error: 0x%X)", 
                         mp->name, mp->id, res);
                log_write(&err_log, err_msg);
                log_close(&err_log);
            }
            if (mp->id >= 0x400 && mp->id <= 0x600 && !tm0_mounted) {
                continue;
            }
        }
    }

    remount(0x800);

    BackupLog mount_log;
    log_init(&mount_log);
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Mount summary: %d/%d successful, %d failed", 
             total_mounts - failed_mounts, total_mounts, failed_mounts);
    log_write(&mount_log, log_msg);
    log_close(&mount_log);
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
    int ret = sceIoDevctl(path, 0x3001, NULL, 0, &info, sizeof(info));
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

static char g_copy_exclude[PATH_MAX_SIZE] = "";

int list_backups(BackupInfo *backups, int max);

int entry_source_exists(const BackupEntry *entry) {
    if (!entry || entry->source[0] == '\0')
        return 0;

    char copy[PATH_MAX_SIZE];
    strncpy(copy, entry->source, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char *part = strtok(copy, "|");
    while (part) {
        SceUID dfd = sceIoDopen(part);
        if (dfd >= 0) {
            sceIoDclose(dfd);
            return 1;
        }
        SceUID fd = sceIoOpen(part, SCE_O_RDONLY, 0);
        if (fd >= 0) {
            sceIoClose(fd);
            return 1;
        }
        part = strtok(NULL, "|");
    }
    return 0;
}

void get_last_backup_summary(char *out, int out_size) {
    BackupInfo *backups = (BackupInfo *)malloc(g_backups_capacity * sizeof(BackupInfo));
    if (!backups) {
        snprintf(out, out_size, "Last: none (memory error)");
        return;
    }
    int count = list_backups(backups, g_backups_capacity);

    if (count == 0) {
        snprintf(out, out_size, "Last: none");
        free(backups);
        return;
    }

    char sz[32];
    format_size(sz, sizeof(sz), backups[0].total_size);
    snprintf(out, out_size, "Last: %s  %s", backups[0].timestamp, sz);
    free(backups);
}

static int should_skip_copy_path(const char *path) {
    if (!g_copy_exclude[0])
        return 0;

    size_t elen = strlen(g_copy_exclude);
    if (strncmp(path, g_copy_exclude, elen) != 0)
        return 0;

    return (path[elen] == '\0' || path[elen] == '/');
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

        if (should_skip_copy_path(sp))
            continue;
        
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

static void rotate_log_files(const char *log_dir) {
    SceUID dir = sceIoDopen(log_dir);
    if (dir < 0) return;
    
    char log_files[MAX_LOG_FILES][PATH_MAX_SIZE];
    int log_count = 0;
    SceIoDirent ent;
    
    while (sceIoDread(dir, &ent) > 0 && log_count < MAX_LOG_FILES) {
        if (strstr(ent.d_name, ".txt")) {
            snprintf(log_files[log_count], sizeof(log_files[log_count]), "%s/%s", log_dir, ent.d_name);
            log_count++;
        }
    }
    sceIoDclose(dir);
    
    if (log_count <= MAX_LOG_FILES) return;
    
    for (int i = 0; i < log_count; i++) {
        for (int j = i + 1; j < log_count; j++) {
            SceIoStat stat_i, stat_j;
            if (sceIoGetstat(log_files[i], &stat_i) >= 0 && 
                sceIoGetstat(log_files[j], &stat_j) >= 0) {
                int cmp = 0;
                if (stat_i.st_mtime.year > stat_j.st_mtime.year) cmp = 1;
                else if (stat_i.st_mtime.year < stat_j.st_mtime.year) cmp = -1;
                else if (stat_i.st_mtime.month > stat_j.st_mtime.month) cmp = 1;
                else if (stat_i.st_mtime.month < stat_j.st_mtime.month) cmp = -1;
                else if (stat_i.st_mtime.day > stat_j.st_mtime.day) cmp = 1;
                else if (stat_i.st_mtime.day < stat_j.st_mtime.day) cmp = -1;
                else if (stat_i.st_mtime.hour > stat_j.st_mtime.hour) cmp = 1;
                else if (stat_i.st_mtime.hour < stat_j.st_mtime.hour) cmp = -1;
                else if (stat_i.st_mtime.minute > stat_j.st_mtime.minute) cmp = 1;
                else if (stat_i.st_mtime.minute < stat_j.st_mtime.minute) cmp = -1;
                else if (stat_i.st_mtime.second > stat_j.st_mtime.second) cmp = 1;
                else if (stat_i.st_mtime.second < stat_j.st_mtime.second) cmp = -1;
                
                if (cmp > 0) {
                    char temp[PATH_MAX_SIZE];
                    strcpy(temp, log_files[i]);
                    strcpy(log_files[i], log_files[j]);
                    strcpy(log_files[j], temp);
                }
            }
        }
    }
    
    int to_delete = log_count - MAX_LOG_FILES;
    for (int i = 0; i < to_delete; i++) {
        sceIoRemove(log_files[i]);
    }
}

void log_init(BackupLog *log) {
    char log_dir[PATH_MAX_SIZE + 64];
    snprintf(log_dir, sizeof(log_dir), "%s/logs", g_backup_root);
    create_dir(log_dir);
    create_dir(g_backup_root);
    
    rotate_log_files(log_dir);

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
    if (!log || log->fd < 0) return;
    
    SceIoStat stat;
    if (sceIoGetstat(log->path, &stat) >= 0) {
        if (stat.st_size > MAX_LOG_SIZE) {
            char new_path[PATH_MAX_SIZE + 128];
            char ts[64];
            get_timestamp(ts, sizeof(ts));
            snprintf(new_path, sizeof(new_path), "%s/logs/%s_rotated.txt", g_backup_root, ts);
            

            sceIoClose(log->fd);
            sceIoRename(log->path, new_path);
            

            log->fd = sceIoOpen(log->path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
            if (log->fd >= 0) {
                char h[PATH_MAX_SIZE + 256];
                snprintf(h, sizeof(h), "[Log rotated due to size limit - continued from %s]\n\n", new_path);
                sceIoWrite(log->fd, h, strlen(h));
            }
        }
    }
    
    if (log->fd >= 0) sceIoWrite(log->fd, text, strlen(text));
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
        "compression_level=%d\n"
        "checksum=%d\n"
        "preferred_usb_device=%s\n"
        "preferred_usb_name=%s\n"
        "language=%d\n"
        "\n# Individual Entry Toggle States (1 = Enabled, 0 = Disabled)\n",
        (int)current_profile, g_backup_root,
        ftp_config.enabled, ftp_config.compression,
        ftp_config.compression_level, ftp_config.checksum,
        g_preferred_usb_device, g_preferred_usb_name,
        (int)g_current_language);
    if (n > 0) sceIoWrite(fd, buf, n);

    for (int i = 0; i < ENTRY_COUNT; i++) {
        n = snprintf(buf, sizeof(buf), "%s=%d|%s\n", entries[i].name, entries[i].enabled, entries[i].source);
        if (n > 0) sceIoWrite(fd, buf, n);
    }
    sceIoClose(fd);
}

int load_config() {
    strcpy(ftp_config.host, FTP_DEFAULT_HOST);
    ftp_config.port = FTP_DEFAULT_PORT;
    strcpy(ftp_config.user, FTP_DEFAULT_USER);
    strcpy(ftp_config.pass, FTP_DEFAULT_PASS);
    strcpy(ftp_config.remote_dir, FTP_DEFAULT_DIR);
    ftp_config.enabled = 0;
    ftp_config.compression = 0;
    ftp_config.checksum = 0;
    // Imposta un livello di compressione predefinito se non presente nel file di configurazione
    if (ftp_config.compression_level < 0 || ftp_config.compression_level > 9) {
        ftp_config.compression_level = COMPRESS_NORMAL;
    }

    g_preferred_usb_device[0] = '\0';
    g_preferred_usb_name[0] = '\0';

    SceUID fd = sceIoOpen(CONFIG_PATH, SCE_O_RDONLY, 0);
    if (fd < 0) {
        current_profile = PROFILE_NONE;
        for (int i = 0; i < ENTRY_COUNT; i++)
            entries[i].enabled = 0;
        return 0;
    }

    char buffer[65536];
    int total_read = 0;
    while (total_read < sizeof(buffer) - 1) {
        int read = sceIoRead(fd, buffer + total_read, sizeof(buffer) - 1 - total_read);
        if (read <= 0) break;
        total_read += read;
    }
    buffer[total_read] = '\0';
    
    char *line = strtok(buffer, "\n");
    while (line != NULL) {
        char *cr = strchr(line, '\r');
        if (cr) *cr = '\0';
        if (line[0] == '#' || line[0] == '\0') {
            line = strtok(NULL, "\n");
            continue;
        }

        if (strncmp(line, "profile=", 8) == 0) {
            int p = atoi(line + 8);
            if (p >= 0 && p <= 3) current_profile = (ProfileType)p;
        } else if (strncmp(line, "backup_root=", 12) == 0) {
            strncpy(g_backup_root, line + 12, PATH_MAX_SIZE - 1);
        } else if (strncmp(line, "preferred_usb_device=", 21) == 0) {
            strncpy(g_preferred_usb_device, line + 21, sizeof(g_preferred_usb_device) - 1);
        } else if (strncmp(line, "preferred_usb_name=", 20) == 0) {
            strncpy(g_preferred_usb_name, line + 20, sizeof(g_preferred_usb_name) - 1);
        } else if (strncmp(line, "language=", 9) == 0) {
            int lang = atoi(line + 9);
            if (lang >= 0) g_current_language = lang;
        } else if (strncmp(line, "compression_level=", 18) == 0) {
            ftp_config.compression_level = atoi(line + 18);
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
        
        line = strtok(NULL, "\n");
    }
    
    sceIoClose(fd);
    return 1;
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
        
        int norm[] = {3, 4, 5, 18, 19};
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

        g_copy_exclude[0] = '\0';
        if (strcmp(entries[i].source, "ux0:data") == 0) {
            strncpy(g_copy_exclude, g_backup_root, sizeof(g_copy_exclude) - 1);
            g_copy_exclude[sizeof(g_copy_exclude) - 1] = '\0';
            log_write(log, "  Excluding backup destination from ux0:data copy\n");
        }

        
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
        } else if (ftp_config.compression_level > COMPRESS_NONE) {
            char zip_dst[PATH_MAX_SIZE];
            snprintf(zip_dst, sizeof(zip_dst), "%s/%s.zip", backup_root, entries[i].name);
            log_write(log, "  Mode: Compressed (ZIP)\n");
            zip_directory(entries[i].source, zip_dst, &ctx, entries[i].name, 
                          eidx, active, g_progress_cb, ftp_config.compression_level);
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
    BackupInfo *backups = (BackupInfo *)malloc(g_backups_capacity * sizeof(BackupInfo));
    if (!backups) return;
    int count = list_backups(backups, g_backups_capacity);

    if (count <= keep_count) {
        free(backups);
        return;
    }

    for (int i = keep_count; i < count; i++) {
        delete_directory(backups[i].path);
        char notify[128];
        snprintf(notify, sizeof(notify), "Auto-purge: removed backup %s", backups[i].timestamp);
        ui_set_notification(notify);
    }
    free(backups);
}

static int is_backup_folder_name(const char *name) {
    if (!name || !name[0])
        return 0;

    if (strcmp(name, "logs") == 0 ||
        strcmp(name, "module") == 0 ||
        strcmp(name, "config.cfg") == 0)
        return 0;

    if (name[0] == '_')
        return 0;

    int len = (int)strlen(name);
    if (len < 19)
        return 0;

    if (name[4] != '-' || name[7] != '-' || name[10] != '_')
        return 0;

    for (int i = 0; i < len; i++) {
        char c = name[i];
        if ((c >= '0' && c <= '9') || c == '-' || c == '_')
            continue;
        return 0;
    }

    return 1;
}

int list_backups(BackupInfo *backups, int max) {
    int count = 0;
    SceUID dir = sceIoDopen(g_backup_root);
    if (dir < 0) return 0;

    char (*dir_names)[64] = (char (*)[64])malloc(g_backups_capacity * 64);
    if (!dir_names) return 0;
    int dc = 0;
    SceIoDirent ent;
    memset(&ent, 0, sizeof(ent));

    while (sceIoDread(dir, &ent) > 0 && dc < g_backups_capacity) {
        if (strcmp(ent.d_name, ".") == 0 ||
            strcmp(ent.d_name, "..") == 0 ||
            !is_backup_folder_name(ent.d_name)) {
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
                // Aggiungi la dimensione dei file .zip
                else if (strstr(se.d_name, ".zip")) {
                    backups[count].entry_count++;
                    backups[count].total_entries++; // Considera un file zip come una singola entry/file
                    backups[count].total_size += se.d_stat.st_size;
                }
                memset(&se, 0, sizeof(se));
            }
            sceIoDclose(sd);
        }
        count++;
    }
    free(dir_names);
    return count;
}

int delete_logs(void) {
    char log_dir[PATH_MAX_SIZE + 64];
    snprintf(log_dir, sizeof(log_dir), "%s/logs", g_backup_root);

    SceUID dir = sceIoDopen(log_dir);
    if (dir < 0) {
        return 0;
    }

    int deleted = 0;
    SceIoDirent ent;
    memset(&ent, 0, sizeof(ent));
    while (sceIoDread(dir, &ent) > 0) {
        if (strcmp(ent.d_name, ".") == 0 || strcmp(ent.d_name, "..") == 0) {
            memset(&ent, 0, sizeof(ent));
            continue;
        }
        char file_path[PATH_MAX_SIZE + 512];
        snprintf(file_path, sizeof(file_path), "%s/%s", log_dir, ent.d_name);
        if (sceIoRemove(file_path) >= 0) {
            deleted++;
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(dir);
    return deleted;
}

int reset_config(void) {
    current_profile = PROFILE_NONE;
    for (int i = 0; i < ENTRY_COUNT; i++) {
        entries[i].enabled = 0;
    }
    strcpy(g_backup_root, "ux0:VitaVault");
    ftp_config.enabled = 0;
    ftp_config.compression = 0;
    ftp_config.compression_level = COMPRESS_NORMAL;
    ftp_config.checksum = 0;
    save_config();
    return 0;
}

static int collect_files_recursive(const char *path, char **files, SceOff *sizes, int *count, int max_files) {
    SceUID dir = sceIoDopen(path);
    if (dir < 0) return -1;
    
    SceIoDirent ent;
    while (sceIoDread(dir, &ent) > 0 && *count < max_files) {
        if (strcmp(ent.d_name, ".") == 0 || strcmp(ent.d_name, "..") == 0) continue;
        
        char full_path[PATH_MAX_SIZE];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, ent.d_name);
        
        if (SCE_S_ISDIR(ent.d_stat.st_mode)) {
            collect_files_recursive(full_path, files, sizes, count, max_files);
        } else {
            files[*count] = strdup(full_path);
            sizes[*count] = ent.d_stat.st_size;
            (*count)++;
        }
    }
    sceIoDclose(dir);
    return 0;
}

int compare_backups(const char *backup_path_a, const char *backup_path_b, BackupComparison *comp) {
    if (!comp) return -1;
    
    memset(comp, 0, sizeof(BackupComparison));
    
    char *files_a[1000];
    char *files_b[1000];
    SceOff sizes_a[1000];
    SceOff sizes_b[1000];
    int count_a = 0, count_b = 0;
    
    collect_files_recursive(backup_path_a, files_a, sizes_a, &count_a, 1000);
    collect_files_recursive(backup_path_b, files_b, sizes_b, &count_b, 1000);
    
    comp->total_files_a = count_a;
    comp->total_files_b = count_b;
    comp->total_size_a = 0;
    comp->total_size_b = 0;
    
    for (int i = 0; i < count_a; i++) comp->total_size_a += sizes_a[i];
    for (int i = 0; i < count_b; i++) comp->total_size_b += sizes_b[i];
    
    int max_diffs = count_a + count_b;
    comp->diffs = (FileDiff *)malloc(max_diffs * sizeof(FileDiff));
    if (!comp->diffs) {
        for (int i = 0; i < count_a; i++) free(files_a[i]);
        for (int i = 0; i < count_b; i++) free(files_b[i]);
        return -1;
    }
    
    comp->diff_count = 0;
    
    for (int i = 0; i < count_a; i++) {
        int found = 0;
        for (int j = 0; j < count_b; j++) {
            if (strcmp(files_a[i], files_b[j]) == 0) {
                found = 1;
                if (sizes_a[i] != sizes_b[j]) {
                    FileDiff *diff = &comp->diffs[comp->diff_count++];
                    strncpy(diff->path, files_a[i], PATH_MAX_SIZE - 1);
                    diff->path[PATH_MAX_SIZE - 1] = '\0';
                    diff->only_in_a = 0;
                    diff->only_in_b = 0;
                    diff->different_size = 1;
                    diff->size_a = sizes_a[i];
                    diff->size_b = sizes_b[j];
                }
                break;
            }
        }
        if (!found) {
            FileDiff *diff = &comp->diffs[comp->diff_count++];
            strncpy(diff->path, files_a[i], PATH_MAX_SIZE - 1);
            diff->path[PATH_MAX_SIZE - 1] = '\0';
            diff->only_in_a = 1;
            diff->only_in_b = 0;
            diff->different_size = 0;
            diff->size_a = sizes_a[i];
            diff->size_b = 0;
        }
    }
    
    for (int i = 0; i < count_b; i++) {
        int found = 0;
        for (int j = 0; j < count_a; j++) {
            if (strcmp(files_b[i], files_a[j]) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            FileDiff *diff = &comp->diffs[comp->diff_count++];
            strncpy(diff->path, files_b[i], PATH_MAX_SIZE - 1);
            diff->path[PATH_MAX_SIZE - 1] = '\0';
            diff->only_in_a = 0;
            diff->only_in_b = 1;
            diff->different_size = 0;
            diff->size_a = 0;
            diff->size_b = sizes_b[i];
        }
    }
    
    for (int i = 0; i < count_a; i++) free(files_a[i]);
    for (int i = 0; i < count_b; i++) free(files_b[i]);
    
    return 0;
}

void free_backup_comparison(BackupComparison *comp) {
    if (comp) {
        if (comp->diffs) {
            free(comp->diffs);
            comp->diffs = NULL;
        }
        comp->diff_count = 0;
    }
}

#define STATE_FILE_PATH "ux0:data/VitaVault/backup_state.bin"

int save_backup_state(BackupState *state) {
    if (!state) return -1;
    
    char state_dir[PATH_MAX_SIZE];
    snprintf(state_dir, sizeof(state_dir), "ux0:data/VitaVault");
    create_dir(state_dir);
    
    SceUID fd = sceIoOpen(STATE_FILE_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) return -1;
    
    sceIoWrite(fd, state, sizeof(BackupState));
    sceIoClose(fd);
    return 0;
}

int load_backup_state(BackupState *state) {
    if (!state) return -1;
    
    SceUID fd = sceIoOpen(STATE_FILE_PATH, SCE_O_RDONLY, 0);
    if (fd < 0) return -1;
    
    int read = sceIoRead(fd, state, sizeof(BackupState));
    sceIoClose(fd);
    
    if (read != sizeof(BackupState)) return -1;
    return 0;
}

int clear_backup_state(void) {
    sceIoRemove(STATE_FILE_PATH);
    return 0;
}

int can_resume_backup(void) {
    BackupState state;
    if (load_backup_state(&state) < 0) return 0;
    return state.is_active;
}

int resume_backup(char *backup_root, int root_size, BackupLog *log) {
    BackupState state;
    if (load_backup_state(&state) < 0) return -1;
    
    if (!state.is_active) return -1;
    
    strncpy(backup_root, state.backup_path, root_size - 1);
    backup_root[root_size - 1] = '\0';
    
    log_init(log);
    char resume_msg[256];
    snprintf(resume_msg, sizeof(resume_msg), "Resuming backup from entry %d/%d\n", 
             state.current_entry_index, state.total_entries);
    log_write(log, resume_msg);
    
    int active = 0;
    for (int i = 0; i < ENTRY_COUNT; i++) {
        if (entries[i].enabled) active++;
    }
    
    CopyContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.current_file = state.current_file_index;
    ctx.total_files = state.total_files;
    ctx.current_bytes = state.current_bytes;
    ctx.total_bytes = state.total_bytes;
    
    for (int i = state.current_entry_index; i < ENTRY_COUNT; i++) {
        if (!entries[i].enabled) continue;
        
        state.current_entry_index = i;
        strncpy(state.last_entry_name, entries[i].name, sizeof(state.last_entry_name) - 1);
        save_backup_state(&state);
        
        int eidx = 0;
        for (int j = 0; j < i; j++) {
            if (entries[j].enabled) eidx++;
        }
        
        char dst[PATH_MAX_SIZE];
        snprintf(dst, sizeof(dst), "%s/%s", backup_root, entries[i].name);
        
        log_write_entry_header(log, eidx + 1, active, entries[i].name,
                               entries[i].source, dst, 0, 0);
        
        copy_directory(entries[i].source, dst, &ctx, log);
        
        state.current_file_index = ctx.current_file;
        state.current_bytes = ctx.current_bytes;
        save_backup_state(&state);
        
        log_write_entry_result(log, ctx.has_error);
        
        if (ctx.cancel) {
            log_write(log, "Backup cancelled by user. State saved for resume.\n");
            log_close(log);
            return -2;
        }
    }
    
    state.is_active = 0;
    save_backup_state(&state);
    clear_backup_state();
    
    log_write(log, "Backup resumed and completed successfully.\n");
    log_close(log);
    return 0;
}