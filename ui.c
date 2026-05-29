#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <psp2/rtc.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ui.h"
#include "backup.h"
#include "ftp.h"

static vita2d_pgf *g_font = NULL;
float g_font_size = 1.0f;
int g_screen_w = 960;
int g_screen_h = 544;
#define BASELINE_OFFSET(size) ((int)((size) * 16.0f))

static char g_notification_msg[128] = "";
static uint64_t g_notification_timer = 0;

int init_font() {
    g_font = vita2d_load_default_pgf();
    if (g_font) return 1;
    return 0;
}

int text_width(const char *text) {
    return text_width_at(text, g_font_size);
}

int text_width_at(const char *text, float size) {
    if (g_font) return vita2d_pgf_text_width(g_font, size, text);
    return (int)(strlen(text) * size * 10.0f);
}

void ui_fini() {
    if (g_font) {
        vita2d_free_pgf(g_font);
        g_font = NULL;
    }
}

void draw_panel(int x, int y, int w, int h, unsigned int color) {
    vita2d_draw_rectangle(x, y, w, h, color);
    vita2d_draw_rectangle(x, y, w, 2, COLOR_BORDER);
    vita2d_draw_rectangle(x, y + h - 2, w, 2, COLOR_BORDER);
    vita2d_draw_rectangle(x, y, 2, h, COLOR_BORDER);
    vita2d_draw_rectangle(x + w - 2, y, 2, h, COLOR_BORDER);
}

void draw_text(int x, int y, unsigned int color, float size, const char *text) {
    if (g_font) {
        vita2d_pgf_draw_text(g_font, x, y + BASELINE_OFFSET(size), color, size, text);
    }
}

void draw_progress_bar_gui(int x, int y, int w, int h, int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    vita2d_draw_rectangle(x, y, w, h, COLOR_BAR_BG);
    if (pct > 0) {
        int fill_w = ((w - 4) * pct) / 100;
        if (fill_w < 0) fill_w = 0;
        unsigned int bar_color = (pct < 50) ? COLOR_ORANGE :
                                 (pct < 90) ? COLOR_ACCENT : COLOR_GREEN;
        vita2d_draw_rectangle(x + 2, y + 2, fill_w, h - 4, bar_color);
    }
    vita2d_draw_rectangle(x, y, w, 2, COLOR_BORDER);
    vita2d_draw_rectangle(x, y + h - 2, w, 2, COLOR_BORDER);
    vita2d_draw_rectangle(x, y, 2, h, COLOR_BORDER);
    vita2d_draw_rectangle(x + w - 2, y, 2, h, COLOR_BORDER);
}

void draw_checkbox(int x, int y, int checked) {
    vita2d_draw_rectangle(x, y, 16, 16, checked ? COLOR_GREEN : COLOR_BG_PANEL);
    vita2d_draw_rectangle(x, y, 16, 1, COLOR_BORDER);
    vita2d_draw_rectangle(x, y + 15, 16, 1, COLOR_BORDER);
    vita2d_draw_rectangle(x, y, 1, 16, COLOR_BORDER);
    vita2d_draw_rectangle(x + 15, y, 1, 16, COLOR_BORDER);
    if (checked) {
        vita2d_draw_rectangle(x + 3, y + 7, 4, 4, COLOR_TEXT_BRIGHT);
        vita2d_draw_rectangle(x + 7, y + 4, 4, 7, COLOR_TEXT_BRIGHT);
    }
}

void draw_scrollbar(int total, int visible, int selected, int x, int y, int h) {
    if (total <= visible) return;
    int bar_h = (h * visible) / total;
    if (bar_h < 10) bar_h = 10;
    int bar_y = y + (selected * (h - bar_h)) / (total - visible);
    if (bar_y < y) bar_y = y;
    if (bar_y + bar_h > y + h) bar_y = y + h - bar_h;
    vita2d_draw_rectangle(x, y, 6, h, COLOR_BAR_BG);
    vita2d_draw_rectangle(x, bar_y, 6, bar_h, COLOR_ACCENT);
}

void ui_set_notification(const char *msg) {
    if (!msg) return;
    strncpy(g_notification_msg, msg, sizeof(g_notification_msg) - 1);
    g_notification_msg[sizeof(g_notification_msg) - 1] = '\0';
    g_notification_timer = sceKernelGetProcessTimeWide() + 3000000;
}

static void draw_notification() {
    if (sceKernelGetProcessTimeWide() < g_notification_timer) {
        int tw = text_width_at(g_notification_msg, 0.9f);
        int w = tw + 40;
        int h = 35;
        int x = (g_screen_w - w) / 2;
        int y = g_screen_h - 80; 
        draw_panel(x, y, w, h, COLOR_BG_HEADER);
        draw_text(x + 20, y + 6, COLOR_YELLOW, 0.9f, g_notification_msg);
    }
}

void draw_text_screen(const char *title, const char *text) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    draw_panel(10, 10, g_screen_w - 20, 60, COLOR_BG_HEADER);
    draw_text(20, 18, COLOR_TEXT_BRIGHT, 1.4f, title);
    draw_text(30, 85, COLOR_TEXT_MAIN, 1.0f, text);
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

int draw_confirm_screen(const char *title, const char *message) {
    SceCtrlData pad;
    int choice = 0;

    while (choice == 0) {
        vita2d_start_drawing();
        vita2d_clear_screen();

        draw_panel(0, 0, g_screen_w, 55, COLOR_BG_HEADER);
        draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, "Confirm");

        draw_text(30, 80, COLOR_TEXT_MAIN, 1.0f, message);

        draw_panel(200, 200, 200, 40, COLOR_BG_SELECTED);
        draw_text(250, 208, COLOR_TEXT_BRIGHT, 1.1f, "X = YES");

        draw_panel(560, 200, 200, 40, COLOR_BG_PANEL);
        draw_text(610, 208, COLOR_TEXT_MAIN, 1.1f, "O = NO");

        vita2d_end_drawing();
        vita2d_swap_buffers();

        sceCtrlPeekBufferPositive(0, &pad, 1);
        sceKernelDelayThread(10000);

        if (pad.buttons & SCE_CTRL_CROSS) {
            choice = 1;
            sceKernelDelayThread(150000);
        } else if (pad.buttons & SCE_CTRL_CIRCLE) {
            choice = 2;
            sceKernelDelayThread(150000);
        }
    }

    return (choice == 1) ? 1 : 0;
}

void draw_main_menu(int selected) {
    vita2d_start_drawing();
    vita2d_clear_screen();

    draw_panel(0, 0, g_screen_w, 55, COLOR_BG_HEADER);
    draw_text(15, 4, COLOR_TEXT_BRIGHT, 1.4f, "VitaVault Backup Utility");

    // Info Batteria e Ora
    char sys_info[64];
    SceDateTime time;
    sceRtcGetCurrentClockLocalTime(&time);
    int batt_level = scePowerGetBatteryLifePercent();
    unsigned int batt_color = (batt_level > 20) ? COLOR_GREEN : COLOR_RED;
    
    snprintf(sys_info, sizeof(sys_info), "%02d:%02d  |  %d%%", 
             time.hour, time.minute, batt_level);
    
    // Move system info higher to save space
    int info_x = g_screen_w - text_width_at(sys_info, 0.6f) - 15;
    draw_text(info_x, 2, batt_color, 0.6f, sys_info);

    if (g_ftp_active) {
        draw_text(420, 6, COLOR_GREEN, 0.9f, "[FTP: ON]");
    } else {
        draw_text(420, 6, COLOR_TEXT_DIM, 0.9f, "[FTP: OFF]");
    }

    
    char profile_text[128];
    int active = 0;
    for (int i = 0; i < ENTRY_COUNT; i++)
        if (entries[i].enabled) active++;
    snprintf(profile_text, sizeof(profile_text),
             "Profile: [%s]  %d/%d active",
             profile_names[current_profile], active, ENTRY_COUNT);

    draw_text(g_screen_w - 20 - text_width_at(profile_text, 0.7f), 26,
              COLOR_TEXT_DIM, 0.7f, profile_text);

    char dest_text[PATH_MAX_SIZE + 32];
    if (strlen(g_backup_root) > 40) {
        snprintf(dest_text, sizeof(dest_text), "Dest: ...%.100s", g_backup_root + (strlen(g_backup_root) - 37));
    } else {
        snprintf(dest_text, sizeof(dest_text), "Dest: %.100s", g_backup_root);
    }
    draw_text(g_screen_w - 20 - text_width_at(dest_text, 0.7f), 42, COLOR_ACCENT, 0.7f, dest_text);

    int list_x = 0;
    int list_y = 60;
    int list_w = g_screen_w;
    int item_h = 30;
    int visible = (g_screen_h - list_y - 40) / item_h;
    int start = selected - visible / 2;
    if (start < 0) start = 0;
    if (start + visible > ENTRY_COUNT) start = ENTRY_COUNT - visible;
    if (start < 0) start = 0;

    vita2d_draw_rectangle(list_x, list_y, list_w,
                          visible * item_h, COLOR_BG_PANEL);

    for (int i = 0; i < visible && (start + i) < ENTRY_COUNT; i++) {
        int idx = start + i;
        int y = list_y + i * item_h;

        if (idx == selected) {
            vita2d_draw_rectangle(0, y, g_screen_w, item_h, COLOR_BG_SELECTED);
        }

        draw_checkbox(10, y + 7, entries[idx].enabled);

        draw_text(35, y + 5,
                  (idx == selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_MAIN,
                  1.0f, entries[idx].name);

        draw_text(list_w - 20 - text_width_at(entries[idx].source, 0.8f), y + 5,
                  COLOR_TEXT_DIM, 0.8f, entries[idx].source);
    }

    int fy = g_screen_h - 35;
    draw_panel(0, fy, g_screen_w, 35, COLOR_BG_HEADER);
    draw_text(15, fy + 7, COLOR_TEXT_DIM, 0.8f,
        "X=Backup  O=Toggle  []=Dest  SEL=Sett  SEL+▼=Prof  ▲=Manage  START=FTP");

    draw_scrollbar(ENTRY_COUNT, visible, selected,
                   g_screen_w - 8, list_y, visible * item_h);

    draw_notification();

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void draw_file_browser(const char *current_path, char names[][256], int count, int selected) {
    vita2d_start_drawing();
    vita2d_clear_screen();

    draw_panel(0, 0, g_screen_w, 55, COLOR_BG_HEADER);
    draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, "Select Folder");

    draw_text(20, 65, COLOR_ACCENT, 0.8f, "Current:");
    draw_text(20, 85, COLOR_TEXT_MAIN, 0.9f, current_path);

    int list_y = 115;
    int item_h = 30;
    int visible = (g_screen_h - list_y - 45) / item_h;
    int start = selected - visible / 2;
    if (start < 0) start = 0;

    for (int i = 0; i < visible && (start + i) < count; i++) {
        int idx = start + i;
        int y = list_y + i * item_h;
        if (idx == selected) {
            vita2d_draw_rectangle(0, y, g_screen_w, item_h, COLOR_BG_SELECTED);
        }
        
        draw_text(15, y + 5, (idx == selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_DIM, 1.0f, "[DIR]");
        draw_text(70, y + 5, (idx == selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_MAIN, 1.0f, names[idx]);
    }

    if (count > visible) {
        draw_scrollbar(count, visible, selected, g_screen_w - 8, list_y, visible * item_h);
    }

    int fy = g_screen_h - 35;
    draw_panel(0, fy, g_screen_w, 35, COLOR_BG_HEADER);
    draw_text(15, fy + 7, COLOR_TEXT_DIM, 0.8f, "X=Enter  O=Back  ▲=New Folder  START=Confirm");

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void draw_space_check(int space_status, SceOff needed, SceOff free_space) {
    vita2d_start_drawing();
    vita2d_clear_screen();

    draw_panel(0, 0, g_screen_w, 55, COLOR_BG_HEADER);
    draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, "Available Space Check");

    int y = 75;
    char buf[PATH_MAX_SIZE + 64];

    for (int i = 0; i < ENTRY_COUNT; i++) {
        if (!entries[i].enabled) continue;
        int fc = 0;
        SceOff fs = 0;
        if (count_files_recursive(entries[i].source, &fc, &fs) == 0) {
            char sz[32];
            format_size(sz, sizeof(sz), fs);
            snprintf(buf, sizeof(buf), "%s: %d files (%s)",
                     entries[i].name, fc, sz);
            draw_text(30, y, COLOR_TEXT_MAIN, 1.0f, buf);
            y += 25;
        } else {
            snprintf(buf, sizeof(buf), "%s: (not found)", entries[i].name);
            draw_text(30, y, COLOR_RED, 1.0f, buf);
            y += 25;
        }
    }

    char nsz[32], fsz[32];
    format_size(nsz, sizeof(nsz), needed);
    format_size(fsz, sizeof(fsz), free_space);
    snprintf(buf, sizeof(buf), "Required: %s  Available: %s", nsz, fsz);
    draw_text(30, y + 10, COLOR_TEXT_BRIGHT, 1.0f, buf);
    y += 35;

    if (space_status == 2) {
        draw_text(30, y, COLOR_RED, 1.2f, "WARNING: Insufficient space!");
        draw_text(30, y + 28, COLOR_TEXT_DIM, 0.8f,
                  "Press X to proceed anyway, O to cancel");
    } else if (space_status == 0) {
        draw_text(30, y, COLOR_GREEN, 1.2f, "Sufficient space available.");
        draw_text(30, y + 28, COLOR_TEXT_DIM, 0.8f,
                  "Press X to start backup, O to cancel");
    } else {
        draw_text(30, y, COLOR_YELLOW, 1.2f,
                  "Unable to check space. Proceeding anyway.");
        draw_text(30, y + 28, COLOR_TEXT_DIM, 0.8f,
                  "Press X to start, O to cancel");
    }

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void draw_backup_running(const char *entry_name, int current, int total,
                          SceOff cbytes, SceOff tbytes,
                          int cur_file, int total_files) {
    vita2d_start_drawing();
    vita2d_clear_screen();

    draw_panel(0, 0, g_screen_w, 55, COLOR_BG_HEADER);
    draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, "Backup in Progress...");

    int y = 75;
    char buf[PATH_MAX_SIZE + 64];

    snprintf(buf, sizeof(buf), "[%d/%d] %s", current, total, entry_name);
    draw_text(20, y, COLOR_TEXT_BRIGHT, 1.1f, buf);
    y += 35;

    int pct = (tbytes > 0) ? (int)((cbytes * 100) / tbytes) : 0;
    draw_progress_bar_gui(20, y, g_screen_w - 40, 26, pct);
    y += 38;

    snprintf(buf, sizeof(buf), "%d%%  (%d/%d files)", pct, cur_file, total_files);
    draw_text(20, y, COLOR_ACCENT, 1.0f, buf);
    y += 28;

    char cs[32], ts[32];
    format_size(cs, sizeof(cs), cbytes);
    format_size(ts, sizeof(ts), tbytes);
    if (cbytes > 0 && tbytes > 0 && cbytes < tbytes) {
        SceOff remaining = tbytes - cbytes;
        char rs[32];
        format_size(rs, sizeof(rs), remaining);
        snprintf(buf, sizeof(buf), "Transferred: %s / %s  (remaining: %s)", cs, ts, rs);
    } else {
        snprintf(buf, sizeof(buf), "Transferred: %s / %s", cs, ts);
    }
    draw_text(20, y, COLOR_TEXT_MAIN, 0.9f, buf);
    y += 28;

    static int dot = 0;
    dot = (dot + 1) % 4;
    snprintf(buf, sizeof(buf), "Processing  %c", ".oOo"[dot]);
    draw_text(g_screen_w - 25 - text_width_at(buf, 1.2f), 14, COLOR_ACCENT, 1.2f, buf);

    draw_text(20, g_screen_h - 30, COLOR_TEXT_DIM, 0.8f, "Hold (O) to Cancel Backup");

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void draw_backup_complete(const BackupLog *log) {
    vita2d_start_drawing();
    vita2d_clear_screen();

    draw_panel(0, 0, g_screen_w, 55, COLOR_BG_HEADER);
    draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, "Backup Completed!");

    char sz[32];
    format_size(sz, sizeof(sz), log->total_bytes);
    int y = 80;
    char buf[PATH_MAX_SIZE + 64];

    snprintf(buf, sizeof(buf), "Entries: %d  Files: %d  Size: %s  Errors: %d",
             log->total_entries, log->total_files, sz, log->errors);
    draw_text(20, y, COLOR_TEXT_BRIGHT, 1.0f, buf);
    y += 30;

    draw_text(20, y, COLOR_TEXT_DIM, 0.9f, "Saved to:");
    y += 22;
    draw_text(20, y, COLOR_ACCENT, 0.9f, g_last_backup_path);
    y += 28;

    draw_text(20, y, COLOR_TEXT_DIM, 0.9f, "Log:");
    y += 22;
    draw_text(20, y, COLOR_ACCENT, 0.9f, g_last_log_path);
    y += 32;

    if (log->errors > 0) {
        draw_text(20, y, COLOR_RED, 1.0f,
                  "THERE WERE ERRORS! Check the log for details.");
        y += 28;
    }

    if (ftp_config.enabled) {
        draw_text(20, y, COLOR_GREEN, 0.9f, "Press START to start FTP server and download to PC.");
        y += 22;
        draw_text(20, y, COLOR_TEXT_DIM, 0.8f, "Press O to exit.");
    } else {
        draw_text(20, y, COLOR_TEXT_DIM, 0.9f, "Press START to exit.");
    }

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void draw_manage_list(int selected, int count) {
    vita2d_start_drawing();
    vita2d_clear_screen();

    draw_panel(0, 0, g_screen_w, 55, COLOR_BG_HEADER);
    draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, "Manage Backups");

    if (count == 0) {
        draw_text(30, 80, COLOR_TEXT_DIM, 1.2f, "No backups found.");
        draw_text(30, 110, COLOR_TEXT_DIM, 0.9f, "Press O to return.");
        vita2d_end_drawing();
        vita2d_swap_buffers();
        return;
    }

    int list_y = 60;
    int item_h = 55;
    int visible = (g_screen_h - list_y - 40) / item_h;
    int start = selected - visible / 2;
    if (start < 0) start = 0;
    if (start + visible > count) start = count - visible;
    if (start < 0) start = 0;

    vita2d_draw_rectangle(0, list_y, g_screen_w,
                          visible * item_h, COLOR_BG_PANEL);

    for (int i = 0; i < visible && (start + i) < count; i++) {
        int idx = start + i;
        int y = list_y + i * item_h;
        if (idx == selected) {
            vita2d_draw_rectangle(0, y, g_screen_w, item_h, COLOR_BG_SELECTED);
        }

        char sz[32];
        format_size(sz, sizeof(sz), g_backups[idx].total_size);
        char buf[PATH_MAX_SIZE + 64];

        snprintf(buf, sizeof(buf), "%s", g_backups[idx].timestamp);
        draw_text(15, y + 5,
                  (idx == selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_MAIN,
                  1.0f, buf);

        snprintf(buf, sizeof(buf), "%d entries, %d files, %s",
                 g_backups[idx].entry_count,
                 g_backups[idx].total_entries, sz);
        draw_text(15, y + 28, COLOR_TEXT_DIM, 0.8f, buf);
    }

    draw_scrollbar(count, visible, selected,
                   g_screen_w - 8, list_y, visible * item_h);

    draw_panel(0, g_screen_h - 35, g_screen_w, 35, COLOR_BG_HEADER);
    draw_text(15, g_screen_h - 26, COLOR_TEXT_DIM, 0.8f,
              "▲/▼=Navigate  X=Details  O=Back");

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void draw_backup_details(const BackupInfo *b) {
    vita2d_start_drawing();
    vita2d_clear_screen();

    draw_panel(0, 0, g_screen_w, 55, COLOR_BG_HEADER);
    draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, "Backup Details");

    int y = 75;
    char buf[PATH_MAX_SIZE + 256];

    snprintf(buf, sizeof(buf), "Backup: %s", b->timestamp);
    draw_text(20, y, COLOR_TEXT_BRIGHT, 1.1f, buf);
    y += 28;

    snprintf(buf, sizeof(buf), "Path: %s", b->path);
    draw_text(20, y, COLOR_TEXT_DIM, 0.8f, buf);
    y += 22;

    char sz[32];
    format_size(sz, sizeof(sz), b->total_size);
    snprintf(buf, sizeof(buf), "Size: %s   Entries: %d   Files: %d",
             sz, b->entry_count, b->total_entries);
    draw_text(20, y, COLOR_TEXT_MAIN, 1.0f, buf);
    y += 28;

    draw_text(20, y, COLOR_TEXT_DIM, 1.0f, "Included entries:");
    y += 24;

    SceUID sd = sceIoDopen(b->path);
    if (sd >= 0) {
        SceIoDirent se;
        memset(&se, 0, sizeof(se));
        while (sceIoDread(sd, &se) > 0) {
            if (strcmp(se.d_name, ".") == 0 || strcmp(se.d_name, "..") == 0) {
                memset(&se, 0, sizeof(se));
                continue;
            }
            if (SCE_S_ISDIR(se.d_stat.st_mode)) {
                snprintf(buf, sizeof(buf), "  - %s", se.d_name);
                draw_text(30, y, COLOR_ACCENT, 0.9f, buf);
                y += 22;
            }
            memset(&se, 0, sizeof(se));
        }
        sceIoDclose(sd);
    }

    draw_panel(0, g_screen_h - 35, g_screen_w, 35, COLOR_BG_HEADER);
    draw_text(15, g_screen_h - 26, COLOR_TEXT_DIM, 0.8f,
              "X=Restore  SEL=Delete  O=Back");

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void draw_restore_progress(const char *entry, int done, int errors) {
    vita2d_start_drawing();
    vita2d_clear_screen();

    draw_panel(0, 0, g_screen_w, 55, COLOR_BG_HEADER);
    draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, "Restoring Backup...");

    char buf[PATH_MAX_SIZE + 64];
    snprintf(buf, sizeof(buf), "Restoring: %s", entry);
    draw_text(20, 80, COLOR_TEXT_BRIGHT, 1.0f, buf);

    snprintf(buf, sizeof(buf), "Files restored: %d   Errors: %d", done, errors);
    draw_text(20, 110, COLOR_TEXT_MAIN, 0.9f, buf);

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void draw_ftp_upload(int done_files, int total_files,
                     SceOff done_bytes, SceOff total_bytes) {
    vita2d_start_drawing();
    vita2d_clear_screen();

    draw_panel(0, 0, g_screen_w, 55, COLOR_BG_HEADER);
    draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, "FTP Upload in Progress...");

    char buf[PATH_MAX_SIZE + 64];
    draw_text(20, 75, COLOR_TEXT_DIM, 0.9f, "Uploading to:");
    snprintf(buf, sizeof(buf), "%s:%d/%s",
             ftp_config.host, ftp_config.port, ftp_config.remote_dir);
    draw_text(20, 98, COLOR_ACCENT, 0.9f, buf);

    int pct = (total_files > 0) ? (done_files * 100) / total_files : 0;
    draw_progress_bar_gui(20, 130, g_screen_w - 40, 28, pct);

    snprintf(buf, sizeof(buf), "%d%%  (%d/%d files)", pct, done_files, total_files);
    draw_text(20, 165, COLOR_ACCENT, 1.0f, buf);

    char cs[32], ts[32];
    format_size(cs, sizeof(cs), done_bytes);
    format_size(ts, sizeof(ts), total_bytes);
    snprintf(buf, sizeof(buf), "Uploaded: %s / %s", cs, ts);
    draw_text(20, 195, COLOR_TEXT_MAIN, 0.9f, buf);

    if (done_bytes > 0 && total_bytes > 0 && done_bytes < total_bytes) {
        static SceOff last_b = 0;
        static unsigned long long last_t = 0;
        unsigned long long now = sceKernelGetProcessTimeWide();

        if (last_b == 0) {
            last_b = done_bytes;
            last_t = now;
            snprintf(buf, sizeof(buf), "ETA: calculating...");
        } else {
            SceOff delta_b = done_bytes - last_b;
            unsigned long long delta_t = now - last_t;
            if (delta_t > 500000 && delta_b > 0) {
                double speed = (double)delta_b / ((double)delta_t / 1000000.0);
                SceOff remaining = total_bytes - done_bytes;
                int eta_sec = (int)((double)remaining / speed);
                if (eta_sec >= 3600)
                    snprintf(buf, sizeof(buf), "ETA: %dh %dm %ds",
                             eta_sec/3600, (eta_sec%3600)/60, eta_sec%60);
                else if (eta_sec >= 60)
                    snprintf(buf, sizeof(buf), "ETA: %dm %ds",
                             eta_sec/60, eta_sec%60);
                else
                    snprintf(buf, sizeof(buf), "ETA: %ds", eta_sec);
                last_b = done_bytes;
                last_t = now;
            } else {
                snprintf(buf, sizeof(buf), "ETA: calculating...");
            }
        }
        draw_text(20, 225, COLOR_YELLOW, 0.9f, buf);
    }

    static int spin = 0;
    spin = (spin + 1) % 4;
    const char *spins = "|/-\\";
    char spin_buf[8];
    snprintf(spin_buf, sizeof(spin_buf), "%c", spins[spin]);
    draw_text(g_screen_w - 35, 14, COLOR_ACCENT, 1.4f, spin_buf);

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void draw_settings(int selected) {
    vita2d_start_drawing();
    vita2d_clear_screen();

    char buf[PATH_MAX_SIZE + 64];
    draw_panel(0, 0, g_screen_w, 55, COLOR_BG_HEADER);
    draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, "Global Settings");

    int y = 85;
    const char *options[] = {
        "Automatic FTP Upload",
        "Backup Compression (ZIP)",
        "Integrity Check (MD5)",
        "Backup Profile",
        "Start FTP Server (Manual)"
    };
    int states[] = { ftp_config.enabled, ftp_config.compression, ftp_config.checksum, 0, 0 };

    for (int i = 0; i < 5; i++) {
        if (i == selected) {
            vita2d_draw_rectangle(10, y - 5, g_screen_w - 20, 35, COLOR_BG_SELECTED);
        }
        
        if (i < 3) {
            draw_checkbox(25, y + 5, states[i]);
        } else if (i == 3) {
            draw_text(25, y + 2, COLOR_ACCENT, 1.0f, "[CYC]");
        } else if (i == 4) {
            draw_text(25, y + 2, COLOR_ACCENT, 1.0f, "[RUN]");
        }

        if (i == 3) {
            snprintf(buf, sizeof(buf), "%s: [%s]", options[i], profile_names[current_profile]);
            draw_text(170, y + 2, (i == selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_MAIN, 1.0f, buf);
        } else {
            draw_text(170, y + 2, (i == selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_MAIN, 1.0f, options[i]);
        }
        
        y += 45;
    }

    y += 20;
    draw_text(25, y, COLOR_TEXT_DIM, 0.8f, "Current FTP Configuration:");
    snprintf(buf, sizeof(buf), "%s:%d -> %s", ftp_config.host, ftp_config.port, ftp_config.remote_dir);
    draw_text(25, y + 20, COLOR_ACCENT, 0.8f, buf);

    draw_panel(0, g_screen_h - 35, g_screen_w, 35, COLOR_BG_HEADER);
    draw_text(15, g_screen_h - 26, COLOR_TEXT_DIM, 0.8f,
              "▲/▼=Navigate  X=Change/Execute  O=Back");

    vita2d_end_drawing();
    vita2d_swap_buffers();
}
