#include <psp2/types.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>

#include <psp2/ctrl.h>
int sceIoMkdir(const char *file, unsigned int mode);
#include <psp2/io/dirent.h>
#include <psp2/io/devctl.h>
#include <psp2/rtc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "backup.h"
#include "ui.h"

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
    { "Videos",              "ux0:video",                            0 },
    { "Downloads",           "ux0:download",                         0 },
    { "Boot Config (ur0)",   "ur0:vsh/boot",                         0 },
    { "Licenses (tm0:)",     "tm0:/",                                0 },
    { "System Registry",     "vd0:registry",                         0 },
    { "App metadata",        "ux0:app",                              0 },
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

// --------------------------------------------------
// FILE OPS
// --------------------------------------------------

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
        if (SCE_S_ISDIR(ent.d_stat.st_mode)) {
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

void prevent_standby(void) {
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
}

int copy_file(const char *src, const char *dst, CopyContext *ctx) {
    prevent_standby();
    SceUID in = sceIoOpen(src, SCE_O_RDONLY, 0);
    if (in < 0) {
        // Se non riusciamo ad aprire il file (es. su tm0:), saltiamo senza segnare errore critico
        if (ctx) ctx->has_error = 0;
        return -1;
    }

    SceUID out = sceIoOpen(dst, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (out < 0) {
        sceIoClose(in);
        // Se non possiamo creare la destinazione, potrebbe essere un problema di permessi su ux0
        if (ctx) ctx->has_error = 0;
        return -1;
    }

    char buffer[32768];
    int bytes_since_update = 0;
    while (1) {
        int r = sceIoRead(in, buffer, sizeof(buffer));
        if (r <= 0) break;
        int w = sceIoWrite(out, buffer, r);
        if (w < 0) { if (ctx) ctx->has_error = 1; break; }

        // Controllo cancellazione (Tasto Cerchio)
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

            // Aggiorna la UI ogni 512KB per evitare che l'app sembri bloccata su file grandi
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

int copy_directory(const char *src, const char *dst, CopyContext *ctx) {
    create_dir(dst);
    SceUID dir = sceIoDopen(src);
    if (ctx && ctx->cancel) return -1;

    if (dir < 0) {
        if (ctx) ctx->has_error = 1;
        return -1;
    }

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
        if (SCE_S_ISDIR(ent.d_stat.st_mode))
            copy_directory(sp, dp, ctx);
        else
            copy_file(sp, dp, ctx);
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(dir);
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
        "ftp_host=%s\n"
        "ftp_port=%d\n"
        "ftp_user=%s\n"
        "ftp_pass=%s\n"
        "ftp_dir=%s\n"
        "# Entry states\n",
        (int)current_profile, g_backup_root,
        ftp_config.enabled, ftp_config.host, ftp_config.port,
        ftp_config.user, ftp_config.pass, ftp_config.remote_dir);
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

    // Handle last line if no newline
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
        int norm[] = {3, 4, 5};
        for (int i = 0; i < 3; i++) entries[norm[i]].enabled = 1;
    }
    if (profile >= PROFILE_COMPLETE) {
        for (int i = 0; i < ENTRY_COUNT; i++) entries[i].enabled = 1;
    }
    save_config();
}

void cycle_profile() {
    int max_profile = 3;
    current_profile = (ProfileType)(((int)current_profile + 1) % (max_profile + 1));
    apply_profile(current_profile);
}

int do_backup(char *backup_root, int root_size, BackupLog *log) {
    build_backup_root(backup_root, root_size);
    create_dir(backup_root);
    memset(log, 0, sizeof(*log));
    log_init(log);

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

        int fc = 0;
        SceOff fs = 0;
        int ok = count_files_recursive(entries[i].source, &fc, &fs);

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

        copy_directory(entries[i].source, dst, &ctx);

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

void cleanup_old_backups(int keep_count) {
    BackupInfo backups[MAX_BACKUPS];
    int count = list_backups(backups, MAX_BACKUPS);

    if (count <= keep_count) return;

    for (int i = keep_count; i < count; i++) {
        char del_path[PATH_MAX_SIZE + 128];
        snprintf(del_path, sizeof(del_path), "%s/_AUTO_PURGE_%s", 
                 g_backup_root, backups[i].timestamp);
        
        if (sceIoRename(backups[i].path, del_path) >= 0) {
            char notify[128];
            snprintf(notify, sizeof(notify), "Auto-pulizia: rimosso backup %s", backups[i].timestamp);
            ui_set_notification(notify);
        }
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
            strcmp(ent.d_name, "config.cfg") == 0) {
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