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
#include "usb.h"
#include "language.h"
#include "zip_utils.h"

static vita2d_pgf *g_font = NULL;
float g_font_size = 1.0f;
int g_screen_w = 960;
int g_screen_h = 544;
#define BASELINE_OFFSET(size) ((int)((size) * 16.0f))

static char g_notification_msg[128] = "";
static uint64_t g_notification_timer = 0;

static void draw_icon_play(int x, int y, unsigned int color);
static void draw_icon_stop(int x, int y, unsigned int color);

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
    int selected_option = 0;

    while (choice == 0) {
        vita2d_start_drawing();
        vita2d_clear_screen();

        draw_panel(0, 0, g_screen_w, 55, COLOR_BG_HEADER);
        draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, "Confirm");

        draw_text(30, 80, COLOR_TEXT_MAIN, 1.0f, message);

        if (selected_option == 0) {
            draw_panel(200, 200, 200, 40, COLOR_BG_SELECTED);
            draw_text(250, 208, COLOR_TEXT_BRIGHT, 1.1f, "YES");
            draw_panel(560, 200, 200, 40, COLOR_BG_PANEL);
            draw_text(610, 208, COLOR_TEXT_MAIN, 1.1f, "NO");
        } else {
            draw_panel(200, 200, 200, 40, COLOR_BG_PANEL);
            draw_text(250, 208, COLOR_TEXT_MAIN, 1.1f, "YES");
            draw_panel(560, 200, 200, 40, COLOR_BG_SELECTED);
            draw_text(610, 208, COLOR_TEXT_BRIGHT, 1.1f, "NO");
        }

        vita2d_end_drawing();
        vita2d_swap_buffers();

        sceCtrlPeekBufferPositive(0, &pad, 1);
        sceKernelDelayThread(10000);

        if (pad.buttons & SCE_CTRL_LEFT) {
            selected_option = 0;
            sceKernelDelayThread(150000);
        } else if (pad.buttons & SCE_CTRL_RIGHT) {
            selected_option = 1;
            sceKernelDelayThread(150000);
        } else if (pad.buttons & SCE_CTRL_CROSS) {
            choice = (selected_option == 0) ? 1 : 2;
            sceKernelDelayThread(150000);
        } else if (pad.buttons & SCE_CTRL_CIRCLE) {
            choice = 2;
            sceKernelDelayThread(150000);
        }
    }

    return (choice == 1) ? 1 : 0;
}

static void get_partition_root(const char *path, char *out, int out_size) {
    const char *colon = strchr(path, ':');
    if (!colon) {
        snprintf(out, out_size, "ux0:");
        return;
    }
    int len = (int)(colon - path) + 1;
    if (len >= out_size)
        len = out_size - 1;
    strncpy(out, path, len);
    out[len] = '\0';
}

static void format_entry_path_display(const BackupEntry *entry, char *out, int out_size) {
    if (entry->source[0] == '\0') {
        snprintf(out, out_size, "(not set)");
        return;
    }

    if (!entry_source_exists(entry)) {
        if (strlen(entry->source) > 36)
            snprintf(out, out_size, "...%s (not found)", entry->source + strlen(entry->source) - 33);
        else
            snprintf(out, out_size, "%s (not found)", entry->source);
        return;
    }

    if (strlen(entry->source) > 42)
        snprintf(out, out_size, "...%s", entry->source + strlen(entry->source) - 39);
    else
        snprintf(out, out_size, "%s", entry->source);
}

static void get_main_menu_footer(int selected, char *out, int out_size) {
    switch (g_sidebar_selected) {
        case 0:
            if (selected >= 0 && selected < ENTRY_COUNT) {
                 snprintf(out, out_size, "%s %s | %s", entries[selected].name, entries[selected].enabled ? tr("common_on_bracket") : tr("common_off_bracket"), tr("footer_backup_entry"));
            } else {
                 snprintf(out, out_size, "%s", tr("footer_backup_main"));
            }
            break;
        case 1:
            snprintf(out, out_size, "%s", tr("footer_restore"));
            break;
        case 2:
            if (GAME_COUNT == 0) {
                snprintf(out, out_size, "%s", tr("footer_games_empty"));
            } else {
                snprintf(out, out_size, "%s", tr("footer_games"));
            }
            break;
        case 3:
            snprintf(out, out_size, "%s", tr("footer_tools"));
            break;
        case 4:
            snprintf(out, out_size, "%s", tr("footer_ftp"));
            break;
        case 5:
            snprintf(out, out_size, "%s", tr("footer_usb"));
            break;
        case 6:
            snprintf(out, out_size, "%s", tr("footer_settings"));
            break;
        default:
            snprintf(out, out_size, "%s", tr("footer_navigate_only"));
            break;
    }
}

void draw_main_menu(int selected) {
    vita2d_start_drawing();
    vita2d_clear_screen();

    char buf[PATH_MAX_SIZE + 128];
    const int header_h = 72;
    const int sidebar_w = 150;
    const int list_x = sidebar_w;
    const int list_w = g_screen_w - sidebar_w;
    const int list_y = header_h + 2;

    draw_panel(0, 0, g_screen_w, header_h, COLOR_BG_HEADER);

    const char *title = tr("main_title");
    const float title_size = 1.15f;
    int title_w = text_width_at(title, title_size);
    draw_text((g_screen_w - title_w) / 2, 2, COLOR_TEXT_BRIGHT, title_size, title);

    SceDateTime time;
    sceRtcGetCurrentClockLocalTime(&time);
    int batt_level = scePowerGetBatteryLifePercent();
    unsigned int batt_color = (batt_level > 20) ? COLOR_GREEN : COLOR_RED;

    snprintf(buf, sizeof(buf), "%02d:%02d  %d%%", time.hour, time.minute, batt_level);
    draw_text(g_screen_w - 20 - text_width_at(buf, 0.65f), 2, batt_color, 0.65f, buf);

    int status_x = 15;
    draw_text(status_x, 18, g_ftp_active ? COLOR_GREEN : COLOR_TEXT_DIM, 0.75f,
              g_ftp_active ? "FTP: ON" : "FTP: OFF");
    status_x += text_width_at("FTP: OFF  ", 0.75f);
    draw_text(status_x, 18, g_usb_active ? COLOR_GREEN : COLOR_TEXT_DIM, 0.75f,
              g_usb_active ? "USB: ON" : "USB: OFF");

    int active = 0;
    for (int i = 0; i < ENTRY_COUNT; i++)
        if (entries[i].enabled) active++;

    snprintf(buf, sizeof(buf), "Profile: %s  %d/%d",
             profile_names[current_profile], active, ENTRY_COUNT);
    draw_text(g_screen_w - 20 - text_width_at(buf, 0.72f), 18, COLOR_TEXT_DIM, 0.72f, buf);

    // System info in header
    char part[16];
    get_partition_root(g_backup_root, part, sizeof(part));
    char free_sz[32];
    SceOff free_space = get_free_space(part);
    format_size(free_sz, sizeof(free_sz), free_space);

    // Color based on free space (red if less than 1GB, yellow if less than 5GB, green otherwise)
    unsigned int free_color = COLOR_GREEN;
    if (free_space < (SceOff)1024 * 1024 * 1024) { // Less than 1GB
        free_color = COLOR_RED;
    } else if (free_space < (SceOff)5 * 1024 * 1024 * 1024) { // Less than 5GB
        free_color = 0xFFFFAA00; // Yellow
    }

    snprintf(buf, sizeof(buf), "Free: %s", free_sz);
    draw_text(15, 45, free_color, 0.65f, buf);

    int free_x = 15 + text_width_at(buf, 0.65f);
    snprintf(buf, sizeof(buf), "  Dest: %s", g_backup_root);
    draw_text(free_x, 45, COLOR_TEXT_DIM, 0.65f, buf);

    // Sidebar
    draw_panel(0, header_h, sidebar_w, g_screen_h - header_h, COLOR_BG_SIDEBAR);
    const char *sidebar_items[] = {
        tr("sidebar_backup"),
        tr("sidebar_restore"),
        tr("sidebar_games"),
        tr("sidebar_tools"),
        tr("sidebar_ftp"),
        tr("sidebar_usb"),
        tr("sidebar_settings")
    };
    int sidebar_count = 7;
    int sidebar_item_h = 45; // Altezza aumentata per uno stile più moderno
    int total_sidebar_h = g_screen_h - header_h - 35; // Escludiamo header e footer
    int sidebar_start_y = header_h + (total_sidebar_h - (sidebar_count * sidebar_item_h)) / 2;

    for (int i = 0; i < sidebar_count; i++) {
        int y = sidebar_start_y + i * sidebar_item_h;
        if (i == g_sidebar_selected) {
            // Effetto bagliore e selezione centrati nello slot
            vita2d_draw_rectangle(-2, y - 5, sidebar_w + 4, 40, COLOR_BG_GLOW);
            vita2d_draw_rectangle(0, y - 3, sidebar_w, 36, COLOR_BG_SELECTED);
        }
        // Leggero spostamento a destra (x=15) per staccarsi dal bordo
        draw_text(15, y + 4, (i == g_sidebar_selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_MAIN, 0.9f, sidebar_items[i]);
    }

    // Main content area based on sidebar selection
    if (g_sidebar_selected == 0) {
        // Backup - show entry list
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
                // Glow effect
                vita2d_draw_rectangle(list_x - 2, y - 2, list_w + 4, item_h + 4, COLOR_BG_GLOW);
                // Main selection
                vita2d_draw_rectangle(list_x, y, list_w, item_h, COLOR_BG_SELECTED);
            }

            draw_checkbox(list_x + 10, y + 7, entries[idx].enabled);

            draw_text(list_x + 35, y + 5,
                      (idx == selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_MAIN,
                      1.0f, entries[idx].name);

            // Only show path when selected
            if (idx == selected) {
                format_entry_path_display(&entries[idx], buf, sizeof(buf));
                unsigned int path_color = entry_source_exists(&entries[idx]) ?
                    COLOR_TEXT_DIM : COLOR_RED;
                draw_text(list_x + list_w - 20 - text_width_at(buf, 0.8f), y + 5,
                          path_color, 0.8f, buf);
            }
        }

        draw_scrollbar(ENTRY_COUNT, visible, selected,
                       list_x + list_w - 8, list_y, visible * item_h);
    } else if (g_sidebar_selected == 1) {
        // Restore - show backup list
        int backup_count = list_backups(g_backups, g_backups_capacity);
        int item_h = 55;
        int visible = (g_screen_h - list_y - 40) / item_h;
        int restore_selected = selected;
        int start = restore_selected - visible / 2;
        if (start < 0) start = 0;
        if (start + visible > backup_count) start = backup_count - visible;
        if (start < 0) start = 0;

        vita2d_draw_rectangle(list_x, list_y, list_w,
                              visible * item_h, COLOR_BG_PANEL);

        if (backup_count == 0) {
            draw_text(list_x + 20, list_y + 50, COLOR_TEXT_DIM, 1.0f, "No backups found.");
        } else {
            for (int i = 0; i < visible && (start + i) < backup_count; i++) {
                int idx = start + i;
                int y = list_y + i * item_h;
                if (idx == restore_selected) {
                    // Glow effect
                    vita2d_draw_rectangle(list_x - 2, y - 2, list_w + 4, item_h + 4, COLOR_BG_GLOW);
                    // Main selection
                    vita2d_draw_rectangle(list_x, y, list_w, item_h, COLOR_BG_SELECTED);
                }

                char sz[32];
                format_size(sz, sizeof(sz), g_backups[idx].total_size);

                snprintf(buf, sizeof(buf), "%s", g_backups[idx].timestamp);
                draw_text(list_x + 15, y + 5,
                          (idx == restore_selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_MAIN,
                          1.0f, buf);

                snprintf(buf, sizeof(buf), "%d entries, %d files, %s",
                         g_backups[idx].entry_count,
                         g_backups[idx].total_entries, sz);
                draw_text(list_x + 15, y + 28, COLOR_TEXT_DIM, 0.8f, buf);
            }

            if (backup_count > visible) {
                draw_scrollbar(backup_count, visible, restore_selected,
                               list_x + list_w - 8, list_y, visible * item_h);
            }
        }
    } else if (g_sidebar_selected == 2) {
        // Games - show game list
        vita2d_draw_rectangle(list_x, list_y, list_w,
                              g_screen_h - list_y - 40, COLOR_BG_PANEL);

        if (GAME_COUNT == 0) {
            draw_text(list_x + 20, list_y + 50, COLOR_TEXT_DIM, 1.0f, "No games found.");
            draw_text(list_x + 20, list_y + 80, COLOR_TEXT_DIM, 0.8f, "Scan ux0:app for games.");
        } else {
            int item_h = 50;
            int visible = (g_screen_h - list_y - 40) / item_h;
            int start = selected - visible / 2;
            if (start < 0) start = 0;
            if (start + visible > GAME_COUNT) start = GAME_COUNT - visible;
            if (start < 0) start = 0;

            for (int i = 0; i < visible && (start + i) < GAME_COUNT; i++) {
                int idx = start + i;
                int y = list_y + i * item_h;

                if (idx == selected) {
                    // Glow effect
                    vita2d_draw_rectangle(list_x - 2, y - 2, list_w + 4, item_h + 4, COLOR_BG_GLOW);
                    // Main selection
                    vita2d_draw_rectangle(list_x, y, list_w, item_h, COLOR_BG_SELECTED);
                }

                draw_text(list_x + 15, y + 5,
                          (idx == selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_MAIN,
                          1.0f, games[idx].name);

                // Show game info
                char info[128];
                snprintf(info, sizeof(info), "%s | %s%s%s%s",
                         games[idx].title_id,
                         games[idx].has_addcont ? "[DLC] " : "",
                         games[idx].has_patch ? "[PATCH] " : "",
                         games[idx].has_savedata ? "[SAVE] " : "",
                         games[idx].has_trophy ? "[TROP]" : "");
                draw_text(list_x + 15, y + 28, COLOR_TEXT_DIM, 0.7f, info);

                if (GAME_COUNT > visible) {
                    draw_scrollbar(GAME_COUNT, visible, selected,
                                   list_x + list_w - 8, list_y, visible * item_h);
                }
            }
        }
    } else if (g_sidebar_selected == 3) {
        // Tools - show tools options
        vita2d_draw_rectangle(list_x, list_y, list_w,
                              g_screen_h - list_y - 40, COLOR_BG_PANEL);
        
        typedef struct {
            const char *label_key;
            int divider_after;
        } ToolRow;

        static const ToolRow rows[] = {
            { "tools_change_dest", 0 },
            { "tools_change_source", 1 },
            { "tools_reset_dest", 0 },
        };

        const int row_count = (int)(sizeof(rows) / sizeof(rows[0]));
        const int item_h = 40;
        int y = list_y + 8;

        for (int i = 0; i < row_count; i++) {
            if (i == selected) {
                vita2d_draw_rectangle(list_x, y - 4, list_w - 20, item_h - 2, COLOR_BG_SELECTED);
            }

            unsigned int label_color = (i == selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_MAIN;
            draw_text(list_x + 10, y + 4, label_color, 0.95f, tr(rows[i].label_key));
            draw_icon_play(list_x + list_w - 48, y + 4, COLOR_ACCENT);

            y += item_h;

            if (rows[i].divider_after) {
                vita2d_draw_rectangle(list_x + 10, y - 6, list_w - 30, 1, COLOR_BORDER);
                y += 4;
            }
        }

        char dest_buf[PATH_MAX_SIZE + 64];
        snprintf(dest_buf, sizeof(dest_buf), "%s %s", tr("tools_current"), g_backup_root);
        draw_text(list_x + 10, y + 10, COLOR_TEXT_DIM, 0.7f, dest_buf);
    } else if (g_sidebar_selected == 4) {
        // FTP - show FTP settings
        vita2d_draw_rectangle(list_x, list_y, list_w,
                              g_screen_h - list_y - 40, COLOR_BG_PANEL);
        
        typedef struct {
            const char *label_key;
            int divider_after;
        } FTPSettingRow;

        static const FTPSettingRow rows[] = {
            { "ftp_enabled", 0 },
            { "ftp_host", 0 },
            { "ftp_port", 0 },
            { "ftp_user", 0 },
            { "ftp_password", 0 },
            { "ftp_remote_dir", 1 },
            { "ftp_start_server", 0 },
        };

        const int row_count = (int)(sizeof(rows) / sizeof(rows[0]));
        const int item_h = 35;
        int y = list_y + 8;

        for (int i = 0; i < row_count; i++) {
            if (i == selected) {
                vita2d_draw_rectangle(list_x, y - 4, list_w - 20, item_h - 2, COLOR_BG_SELECTED);
            }

            unsigned int label_color = (i == selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_MAIN;

            if (i == 0) {
                draw_checkbox(list_x + 10, y + 4, ftp_config.enabled);
                draw_text(list_x + 35, y + 4, label_color, 0.9f, tr(rows[i].label_key));
                const char *state = ftp_config.enabled ? tr("common_on") : tr("common_off");
                unsigned int state_color = ftp_config.enabled ? COLOR_GREEN : COLOR_TEXT_DIM;
                int sw = text_width_at(state, 0.85f);
                draw_text(list_x + list_w - 20 - sw, y + 6, state_color, 0.85f, state);
            } else if (i == 1) {
                draw_text(list_x + 10, y + 4, label_color, 0.9f, tr(rows[i].label_key));
                int sw = text_width_at(ftp_config.host, 0.85f);
                draw_text(list_x + list_w - 20 - sw, y + 6, COLOR_ACCENT, 0.85f, ftp_config.host);
            } else if (i == 2) {
                draw_text(list_x + 10, y + 4, label_color, 0.9f, tr(rows[i].label_key));
                char port_buf[16];
                snprintf(port_buf, sizeof(port_buf), "%d", ftp_config.port);
                int sw = text_width_at(port_buf, 0.85f);
                draw_text(list_x + list_w - 20 - sw, y + 6, COLOR_ACCENT, 0.85f, port_buf);
            } else if (i == 3) {
                draw_text(list_x + 10, y + 4, label_color, 0.9f, tr(rows[i].label_key));
                int sw = text_width_at(ftp_config.user, 0.85f);
                draw_text(list_x + list_w - 20 - sw, y + 6, COLOR_ACCENT, 0.85f, ftp_config.user);
            } else if (i == 4) {
                draw_text(list_x + 10, y + 4, label_color, 0.9f, tr(rows[i].label_key));
                const char *pass_display = (ftp_config.pass[0] != '\0') ? "******" : tr("common_empty");
                int sw = text_width_at(pass_display, 0.85f);
                draw_text(list_x + list_w - 20 - sw, y + 6, COLOR_ACCENT, 0.85f, pass_display);
            } else if (i == 5) {
                draw_text(list_x + 10, y + 4, label_color, 0.9f, tr(rows[i].label_key));
                int sw = text_width_at(ftp_config.remote_dir, 0.85f);
                draw_text(list_x + list_w - 20 - sw, y + 6, COLOR_ACCENT, 0.85f, ftp_config.remote_dir);
            } else {
                draw_text(list_x + 10, y + 4, label_color, 0.9f, tr(rows[i].label_key));
                draw_icon_play(list_x + list_w - 48, y + 4, COLOR_ACCENT);
            }

            y += item_h;

            if (rows[i].divider_after) {
                vita2d_draw_rectangle(list_x + 10, y - 6, list_w - 30, 1, COLOR_BORDER);
                y += 4;
            }
        }
    } else if (g_sidebar_selected == 5) {
        // USB - show USB settings
        vita2d_draw_rectangle(list_x, list_y, list_w,
                              g_screen_h - list_y - 40, COLOR_BG_PANEL);
        
        typedef struct {
            const char *label_key;
            int divider_after;
        } USBSettingRow;

        static const USBSettingRow rows[] = {
            { "usb_start_mass", 0 },
            { "usb_pref_device", 1 },
        };

        const int row_count = (int)(sizeof(rows) / sizeof(rows[0]));
        const int item_h = 35;
        int y = list_y + 8;

        for (int i = 0; i < row_count; i++) {
            if (i == selected) {
                vita2d_draw_rectangle(list_x, y - 4, list_w - 20, item_h - 2, COLOR_BG_SELECTED);
            }

            unsigned int label_color = (i == selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_MAIN;

            if (i == 0) {
                draw_text(list_x + 10, y + 4, label_color, 0.9f, tr(rows[i].label_key));
                if (g_usb_active) {
                    const char *state = tr("usb_active");
                    int sw = text_width_at(state, 0.8f);
                    draw_text(list_x + list_w - 20 - sw, y + 6, COLOR_GREEN, 0.8f, state);
                    draw_icon_stop(list_x + list_w - 36 - sw, y + 4, COLOR_RED);
                } else {
                    draw_icon_play(list_x + list_w - 48, y + 4, COLOR_ACCENT);
                }
            } else {
                draw_text(list_x + 10, y + 4, label_color, 0.9f, tr(rows[i].label_key));
                draw_icon_play(list_x + list_w - 48, y + 4, COLOR_ACCENT);
                if (g_preferred_usb_name[0] != '\0') {
                    int sw = text_width_at(g_preferred_usb_name, 0.7f);
                    draw_text(list_x + list_w - 65 - sw, y + 6, COLOR_ACCENT, 0.7f, g_preferred_usb_name);
                } else {
                    const char *state = tr("usb_auto");
                    int sw = text_width_at(state, 0.7f);
                    draw_text(list_x + list_w - 65 - sw, y + 6, COLOR_TEXT_DIM, 0.7f, state);
                }
            }

            y += item_h;

            if (rows[i].divider_after) {
                vita2d_draw_rectangle(list_x + 10, y - 6, list_w - 30, 1, COLOR_BORDER);
                y += 4;
            }
        }
    } else if (g_sidebar_selected == 6) {
        // Settings - show settings options
        vita2d_draw_rectangle(list_x, list_y, list_w,
                              g_screen_h - list_y - 40, COLOR_BG_PANEL);
        
        typedef struct {
            const char *label_key;
            int divider_after;
        } SettingRow;

        static const SettingRow rows[] = {
            { "setting_compression", 0 },
            { "setting_checksum", 1 },
            { "setting_language", 0 },
            { "setting_profile", 0 },
            { "setting_advanced", 0 },
        };

        const int row_count = (int)(sizeof(rows) / sizeof(rows[0]));
        const int item_h = 35;
        int y = list_y + 8;

        for (int i = 0; i < row_count; i++) {
            if (i == selected) {
                vita2d_draw_rectangle(list_x, y - 4, list_w - 20, item_h - 2, COLOR_BG_SELECTED);
            }

            unsigned int label_color = (i == selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_MAIN;

            if (i == 0) {
                const char *comp_level_str = "OFF";
                unsigned int state_color = COLOR_TEXT_DIM;
                if (ftp_config.compression_level == COMPRESS_FAST) {
                    comp_level_str = "FAST";
                    state_color = COLOR_YELLOW;
                } else if (ftp_config.compression_level == COMPRESS_NORMAL) {
                    comp_level_str = "NORMAL";
                    state_color = COLOR_GREEN;
                } else if (ftp_config.compression_level == COMPRESS_MAXIMUM) {
                    comp_level_str = "MAX";
                    state_color = COLOR_RED;
                }

                draw_text(list_x + 35, y + 4, label_color, 0.9f, tr(rows[i].label_key));
                int sw = text_width_at(comp_level_str, 0.85f);
                draw_text(list_x + list_w - 20 - sw, y + 6, state_color, 0.85f, comp_level_str);
            } else if (i == 1) {
                draw_checkbox(list_x + 10, y + 4, ftp_config.checksum);
                draw_text(list_x + 35, y + 4, label_color, 0.9f, tr(rows[i].label_key));
                const char *state = ftp_config.checksum ? tr("common_on") : tr("common_off");
                unsigned int state_color = ftp_config.checksum ? COLOR_GREEN : COLOR_TEXT_DIM;
                int sw = text_width_at(state, 0.85f);
                draw_text(list_x + list_w - 20 - sw, y + 6, state_color, 0.85f, state);
            } else if (i == 2) {
                draw_text(list_x + 10, y + 4, label_color, 0.9f, tr(rows[i].label_key));
                const char *lang_name = g_languages[g_current_language].name;
                int sw = text_width_at(lang_name, 0.85f);
                draw_text(list_x + list_w - 20 - sw, y + 6, COLOR_ACCENT, 0.7f, lang_name);
            } else if (i == 3) {
                draw_text(list_x + 10, y + 4, label_color, 0.9f, tr(rows[i].label_key));
                char profile_buf[64];
                snprintf(profile_buf, sizeof(profile_buf), "%s", profile_names[current_profile]);
                int bw = text_width_at(profile_buf, 0.85f);
                draw_text(list_x + list_w - 20 - bw, y + 6, COLOR_ACCENT, 0.85f, profile_buf);
            } else {
                draw_text(list_x + 10, y + 4, label_color, 0.9f, tr(rows[i].label_key));
                draw_icon_play(list_x + list_w - 48, y + 4, COLOR_TEXT_DIM);
            }

            y += item_h;

            if (rows[i].divider_after) {
                vita2d_draw_rectangle(list_x + 10, y - 6, list_w - 30, 1, COLOR_BORDER);
                y += 4;
            }
        }
    }

    int fy = g_screen_h - 35;
    draw_panel(0, fy, g_screen_w, 35, COLOR_BG_HEADER);
    
    get_main_menu_footer(selected, buf, sizeof(buf));

    // Adatta dinamicamente la dimensione del font del footer se il testo è troppo lungo
    float footer_font_size = 0.72f;
    int footer_width = text_width_at(buf, footer_font_size);
    if (footer_width > g_screen_w - 30) { // 15px di padding per lato
        footer_font_size = 0.65f; // Riduci la dimensione se non ci sta
    }
    draw_text(15, fy + 7, COLOR_TEXT_DIM, footer_font_size, buf);
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
    draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, tr("title_backup_running"));

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
    draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, tr("title_backup_complete"));

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

    draw_text(20, y, COLOR_GREEN, 0.9f, "[]: USB Mass Storage - copy backup to PC via cable.");
    y += 22;

    if (ftp_config.enabled) {
        draw_text(20, y, COLOR_GREEN, 0.9f, "FTP download starts automatically after backup.");
        y += 22;
    }

    draw_text(20, y, COLOR_TEXT_DIM, 0.8f, "O: Exit");

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

static void draw_icon_play(int x, int y, unsigned int color) {
    vita2d_draw_rectangle(x, y + 4, 2, 4, color);
    vita2d_draw_rectangle(x + 2, y + 2, 2, 8, color);
    vita2d_draw_rectangle(x + 4, y, 2, 12, color);
    vita2d_draw_rectangle(x + 6, y + 2, 2, 8, color);
    vita2d_draw_rectangle(x + 8, y + 4, 2, 4, color);
}

static void draw_icon_stop(int x, int y, unsigned int color) {
    vita2d_draw_rectangle(x, y, 12, 12, color);
}

static void draw_settings_footer(const char *help) {
    draw_panel(0, g_screen_h - 35, g_screen_w, 35, COLOR_BG_HEADER);
    draw_text(15, g_screen_h - 26, COLOR_TEXT_DIM, 0.72f, help);
}

void draw_settings(int selected) {
    vita2d_start_drawing();
    vita2d_clear_screen();

    char buf[PATH_MAX_SIZE + 64];
    draw_panel(0, 0, g_screen_w, 55, COLOR_BG_HEADER);
    draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, "Global Settings");

    typedef enum {
        SET_TOGGLE,
        SET_CYCLE,
        SET_ACTION,
        SET_USB,
        SET_SUBMENU
    } SettingKind;

    typedef struct {
        SettingKind kind;
        const char *label;
        int divider_after;
    } SettingRow;

    static const SettingRow rows[] = {
        { SET_TOGGLE,  "setting_compression", 0 },
        { SET_TOGGLE,  "setting_checksum",    1 },
        { SET_CYCLE,   "setting_profile",     0 },
        { SET_CYCLE,   "setting_language",    0 },
        { SET_SUBMENU, "setting_advanced",    0 },
    };

    static const char *help[] = {
        "X: Toggle ZIP compression for backups",
        "X: Toggle MD5 checksum generation",
        "X: Cycle profile (NONE / MINIMAL / NORMAL / COMPLETE)",
        "X: Cycle language (English / Italiano)",
        "X: Open advanced storage options",
    };

    const int row_count = (int)(sizeof(rows) / sizeof(rows[0]));
    const int item_h = 40;
    int y = 68;

    for (int i = 0; i < row_count; i++) {
        if (i == selected) {
            vita2d_draw_rectangle(10, y - 4, g_screen_w - 20, item_h - 2, COLOR_BG_SELECTED);
        }

        unsigned int label_color = (i == selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_MAIN;

        if (rows[i].kind == SET_TOGGLE) {
            int on = (i == 0) ? ftp_config.enabled :
                     (i == 1) ? ftp_config.compression :
                                ftp_config.checksum;
            draw_checkbox(20, y + 4, on);
            draw_text(45, y + 4, label_color, 0.95f, tr(rows[i].label));
            const char *state = on ? "ON" : "OFF";
            unsigned int state_color = on ? COLOR_GREEN : COLOR_TEXT_DIM;
            int sw = text_width_at(state, 0.9f);
            draw_text(g_screen_w - 20 - sw, y + 6, state_color, 0.9f, state);
        } else {
            draw_text(20, y + 4, label_color, 0.95f, tr(rows[i].label));
        }

        if (rows[i].kind == SET_CYCLE) {
            if (i == 2) { // Backup Profile
                snprintf(buf, sizeof(buf), "%s", profile_names[current_profile]);
                int bw = text_width_at(buf, 0.9f);
                draw_text(g_screen_w - 20 - bw, y + 6, COLOR_ACCENT, 0.9f, buf);
            } else if (i == 3) { // Language
                const char *lang_name = g_languages[g_current_language].name;
                int sw = text_width_at(lang_name, 0.75f);
                draw_text(g_screen_w - 20 - sw, y + 6, COLOR_ACCENT, 0.75f, lang_name);
            }
        } else if (rows[i].kind == SET_USB) {
            if (g_usb_active) {
                const char *state = "ACTIVE";
                int sw = text_width_at(state, 0.85f);
                draw_text(g_screen_w - 20 - sw, y + 6, COLOR_GREEN, 0.85f, state);
                draw_icon_stop(g_screen_w - 36 - sw, y + 4, COLOR_RED);
            } else {
                draw_icon_play(g_screen_w - 28, y + 4, COLOR_ACCENT);
            }
        } else if (i == 6) {
            extern char g_preferred_usb_name[64];
            draw_icon_play(g_screen_w - 28, y + 4, COLOR_ACCENT);
            if (g_preferred_usb_name[0] != '\0') {
                int sw = text_width_at(g_preferred_usb_name, 0.75f);
                draw_text(g_screen_w - 36 - sw, y + 6, COLOR_ACCENT, 0.75f, g_preferred_usb_name);
            } else {
                const char *state = "Not Set";
                int sw = text_width_at(state, 0.75f);
                draw_text(g_screen_w - 36 - sw, y + 6, COLOR_TEXT_DIM, 0.75f, state);
            }
        } else if (rows[i].kind == SET_ACTION) {
            draw_icon_play(g_screen_w - 28, y + 4, COLOR_ACCENT);
        } else if (rows[i].kind == SET_SUBMENU) {
            draw_icon_play(g_screen_w - 28, y + 4, COLOR_TEXT_DIM);
        }

        y += item_h;

        if (rows[i].divider_after) {
            vita2d_draw_rectangle(20, y - 6, g_screen_w - 40, 1, COLOR_BORDER);
            y += 4;
        }
    }

    y += 8;
    draw_text(20, y, COLOR_TEXT_DIM, 0.75f, "PC download: use download_backup.bat with Vita IP, port 1337");

    draw_settings_footer((selected >= 0 && selected < row_count) ?
        help[selected] : "▲/▼ Navigate   X Select   O Back");

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void draw_settings_advanced(int selected) {
    vita2d_start_drawing();
    vita2d_clear_screen();

    draw_panel(0, 0, g_screen_w, 55, COLOR_BG_HEADER);
    draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, "Advanced Storage");

    static const char *labels[] = {
        "Delete Logs:",
        "Reset Settings:",
    };

    static const char *help[] = {
        "X: Delete all log files",
        "X: Reset to default settings",
    };

    const int row_count = 2;
    const int item_h = 44;
    int y = 80;

    for (int i = 0; i < row_count; i++) {
        if (i == selected) {
            vita2d_draw_rectangle(10, y - 4, g_screen_w - 20, item_h - 2, COLOR_BG_SELECTED);
        }

        unsigned int label_color = (i == selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_MAIN;
        draw_text(20, y + 8, label_color, 0.95f, labels[i]);
        draw_icon_play(g_screen_w - 28, y + 8, COLOR_ACCENT);
        y += item_h;
    }

    draw_text(20, y + 20, COLOR_YELLOW, 0.8f,
              "Warning: advanced options modify ux0: mount points.");

    draw_settings_footer((selected >= 0 && selected < row_count) ?
        help[selected] : "O: Back to settings");

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

typedef struct {
    const char *name;
    const char *path;
} StorageDevice;

int draw_storage_selection_menu(const char *devices[], const char *paths[], int count) {
    if (count <= 0) return -1;

    SceCtrlData pad, old_pad;
    int selected = 0;
    int running = 1;
    int frame_count = 0;

    sceCtrlPeekBufferPositive(0, &old_pad, 1);

    while (running) {
        vita2d_start_drawing();
        vita2d_clear_screen();

        draw_panel(0, 0, g_screen_w, 55, COLOR_BG_HEADER);
        draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, "Select USB Storage Device");

        draw_text(20, 70, COLOR_TEXT_DIM, 0.9f, "Choose which storage to mount via USB:");

        int list_y = 100;
        int item_h = 40;
        int visible = (g_screen_h - list_y - 50) / item_h;
        int start = selected - visible / 2;
        if (start < 0) start = 0;
        if (start + visible > count) start = count - visible;
        if (start < 0) start = 0;

        for (int i = 0; i < visible && (start + i) < count; i++) {
            int idx = start + i;
            int y = list_y + i * item_h;

            if (idx == selected) {
                vita2d_draw_rectangle(10, y - 4, g_screen_w - 20, item_h - 2, COLOR_BG_SELECTED);
            }

            unsigned int label_color = (idx == selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_MAIN;
            draw_text(25, y + 8, label_color, 1.0f, devices[idx]);
        }

        if (count > visible) {
            draw_scrollbar(count, visible, selected, g_screen_w - 8, list_y, visible * item_h);
        }

        draw_panel(0, g_screen_h - 35, g_screen_w, 35, COLOR_BG_HEADER);
        draw_text(15, g_screen_h - 26, COLOR_TEXT_DIM, 0.8f, "▲/▼ Navigate  X=Select  O=Cancel");

        vita2d_end_drawing();
        vita2d_swap_buffers();

        sceCtrlPeekBufferPositive(0, &pad, 1);
        sceKernelDelayThread(16000);

        frame_count++;

        unsigned int new_buttons = pad.buttons & ~old_pad.buttons;

        if (new_buttons & SCE_CTRL_UP) {
            selected--;
            if (selected < 0) selected = count - 1;
            sceKernelDelayThread(150000);
        }
        if (new_buttons & SCE_CTRL_DOWN) {
            selected++;
            if (selected >= count) selected = 0;
            sceKernelDelayThread(150000);
        }
        if (new_buttons & SCE_CTRL_CROSS) {
            sceKernelDelayThread(150000);
            return selected;
        }
        if (new_buttons & SCE_CTRL_CIRCLE) {
            sceKernelDelayThread(150000);
            return -1;
        }
        if (new_buttons & SCE_CTRL_TRIANGLE) {
            sceKernelDelayThread(150000);
            return -1;
        }
        if (new_buttons & SCE_CTRL_START) {
            sceKernelDelayThread(150000);
            return -1;
        }

        old_pad = pad;
    }

    return -1;
}

void draw_restore_screen(int selected) {
    int backup_count = list_backups(g_backups, g_backups_capacity);
    
    vita2d_start_drawing();
    vita2d_clear_screen();

    draw_panel(0, 0, g_screen_w, 55, COLOR_BG_HEADER);
    draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, tr("sidebar_restore"));

    if (backup_count == 0) {
        draw_text(30, 80, COLOR_TEXT_DIM, 1.2f, "No backups found.");
        draw_text(30, 110, COLOR_TEXT_DIM, 0.9f, "Create a backup first from the Backup section.");
        vita2d_end_drawing();
        vita2d_swap_buffers();
        return;
    }

    int list_y = 60;
    int item_h = 55;
    int visible = (g_screen_h - list_y - 40) / item_h;
    int start = selected - visible / 2;
    if (start < 0) start = 0;
    if (start + visible > backup_count) start = backup_count - visible;
    if (start < 0) start = 0;

    vita2d_draw_rectangle(0, list_y, g_screen_w,
                          visible * item_h, COLOR_BG_PANEL);

    for (int i = 0; i < visible && (start + i) < backup_count; i++) {
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

    if (backup_count > visible) {
        draw_scrollbar(backup_count, visible, selected,
                       g_screen_w - 8, list_y, visible * item_h);
    }

    draw_panel(0, g_screen_h - 35, g_screen_w, 35, COLOR_BG_HEADER);
    draw_text(15, g_screen_h - 26, COLOR_TEXT_DIM, 0.8f,
              "▲/▼=Navigate  X=Details  O=Back");

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void draw_ftp_settings(int selected) {
    vita2d_start_drawing();
    vita2d_clear_screen();

    char buf[PATH_MAX_SIZE + 64];
    draw_panel(0, 0, g_screen_w, 55, COLOR_BG_HEADER);
    draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, tr("sidebar_ftp"));

    typedef struct {
        const char *label;
        int divider_after;
    } FTPSettingRow;

    static const FTPSettingRow rows[] = {
        { "FTP Enabled", 0 },
        { "FTP Host", 0 },
        { "FTP Port", 0 },
        { "FTP User", 0 },
        { "FTP Password", 0 },
        { "Remote Directory", 1 },
        { "Start FTP Server", 0 },
    };

    static const char *help[] = {
        "X: Toggle FTP upload after backup",
        "X: Edit FTP host address",
        "X: Edit FTP port",
        "X: Edit FTP username",
        "X: Edit FTP password",
        "X: Edit remote directory",
        "X: Start FTP server manually",
    };

    const int row_count = (int)(sizeof(rows) / sizeof(rows[0]));
    const int item_h = 40;
    int y = 68;

    for (int i = 0; i < row_count; i++) {
        if (i == selected) {
            vita2d_draw_rectangle(10, y - 4, g_screen_w - 20, item_h - 2, COLOR_BG_SELECTED);
        }

        unsigned int label_color = (i == selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_MAIN;

        if (i == 0) {
            draw_checkbox(20, y + 4, ftp_config.enabled);
            draw_text(45, y + 4, label_color, 0.95f, rows[i].label);
            const char *state = ftp_config.enabled ? "ON" : "OFF";
            unsigned int state_color = ftp_config.enabled ? COLOR_GREEN : COLOR_TEXT_DIM;
            int sw = text_width_at(state, 0.9f);
            draw_text(g_screen_w - 20 - sw, y + 6, state_color, 0.9f, state);
        } else if (i == 1) {
            draw_text(20, y + 4, label_color, 0.95f, rows[i].label);
            int sw = text_width_at(ftp_config.host, 0.9f);
            draw_text(g_screen_w - 20 - sw, y + 6, COLOR_ACCENT, 0.9f, ftp_config.host);
        } else if (i == 2) {
            draw_text(20, y + 4, label_color, 0.95f, rows[i].label);
            snprintf(buf, sizeof(buf), "%d", ftp_config.port);
            int sw = text_width_at(buf, 0.9f);
            draw_text(g_screen_w - 20 - sw, y + 6, COLOR_ACCENT, 0.9f, buf);
        } else if (i == 3) {
            draw_text(20, y + 4, label_color, 0.95f, rows[i].label);
            int sw = text_width_at(ftp_config.user, 0.9f);
            draw_text(g_screen_w - 20 - sw, y + 6, COLOR_ACCENT, 0.9f, ftp_config.user);
        } else if (i == 4) {
            draw_text(20, y + 4, label_color, 0.95f, rows[i].label);
            const char *pass_display = (ftp_config.pass[0] != '\0') ? "******" : "(empty)";
            int sw = text_width_at(pass_display, 0.9f);
            draw_text(g_screen_w - 20 - sw, y + 6, COLOR_ACCENT, 0.9f, pass_display);
        } else if (i == 5) {
            draw_text(20, y + 4, label_color, 0.95f, rows[i].label);
            int sw = text_width_at(ftp_config.remote_dir, 0.9f);
            draw_text(g_screen_w - 20 - sw, y + 6, COLOR_ACCENT, 0.9f, ftp_config.remote_dir);
        } else {
            draw_text(20, y + 4, label_color, 0.95f, rows[i].label);
            draw_icon_play(g_screen_w - 28, y + 4, COLOR_ACCENT);
        }

        y += item_h;

        if (rows[i].divider_after) {
            vita2d_draw_rectangle(20, y - 6, g_screen_w - 40, 1, COLOR_BORDER);
            y += 4;
        }
    }

    if (g_ftp_active) {
        char ip[32];
        net_get_local_ip(ip, sizeof(ip));
        snprintf(buf, sizeof(buf), "FTP Active: %s:%d", ip, 1337);
        draw_text(20, y + 10, COLOR_GREEN, 0.8f, buf);
    }

    draw_settings_footer((selected >= 0 && selected < row_count) ?
        help[selected] : "▲/▼ Navigate   X Select   O Back");

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void draw_usb_settings(int selected) {
    vita2d_start_drawing();
    vita2d_clear_screen();

    draw_panel(0, 0, g_screen_w, 55, COLOR_BG_HEADER);
    draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, tr("sidebar_usb"));

    typedef struct {
        const char *label;
        int divider_after;
    } USBSettingRow;

    static const USBSettingRow rows[] = {
        { "Start USB Mass Storage", 0 },
        { "Preferred USB Device", 1 },
    };

    static const char *help[] = {
        "X: Start USB mass storage mode",
        "X: Select preferred USB device",
    };

    const int row_count = (int)(sizeof(rows) / sizeof(rows[0]));
    const int item_h = 40;
    int y = 68;

    for (int i = 0; i < row_count; i++) {
        if (i == selected) {
            vita2d_draw_rectangle(10, y - 4, g_screen_w - 20, item_h - 2, COLOR_BG_SELECTED);
        }

        unsigned int label_color = (i == selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_MAIN;

        if (i == 0) {
            draw_text(20, y + 4, label_color, 0.95f, rows[i].label);
            if (g_usb_active) {
                const char *state = "ACTIVE";
                int sw = text_width_at(state, 0.85f);
                draw_text(g_screen_w - 20 - sw, y + 6, COLOR_GREEN, 0.85f, state);
                draw_icon_stop(g_screen_w - 36 - sw, y + 4, COLOR_RED);
            } else {
                draw_icon_play(g_screen_w - 28, y + 4, COLOR_ACCENT);
            }
        } else {
            draw_text(20, y + 4, label_color, 0.95f, rows[i].label);
            draw_icon_play(g_screen_w - 28, y + 4, COLOR_ACCENT);
            if (g_preferred_usb_name[0] != '\0') {
                int sw = text_width_at(g_preferred_usb_name, 0.75f);
                draw_text(g_screen_w - 36 - sw, y + 6, COLOR_ACCENT, 0.75f, g_preferred_usb_name);
            } else {
                const char *state = "Auto";
                int sw = text_width_at(state, 0.75f);
                draw_text(g_screen_w - 36 - sw, y + 6, COLOR_TEXT_DIM, 0.75f, state);
            }
        }

        y += item_h;

        if (rows[i].divider_after) {
            vita2d_draw_rectangle(20, y - 6, g_screen_w - 40, 1, COLOR_BORDER);
            y += 4;
        }
    }

    draw_settings_footer((selected >= 0 && selected < row_count) ?
        help[selected] : "▲/▼ Navigate   X Select   O Back");

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void draw_tools_screen(int selected) {
    vita2d_start_drawing();
    vita2d_clear_screen();

    draw_panel(0, 0, g_screen_w, 55, COLOR_BG_HEADER);
    draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, tr("sidebar_tools"));

    typedef struct {
        const char *label;
        int divider_after;
    } ToolRow;

    static const ToolRow rows[] = {
        { "Change Backup Destination", 0 },
        { "Change Entry Source Path", 1 },
        { "Reset Backup Destination", 0 },
    };

    static const char *help[] = {
        "X: Select backup destination folder",
        "X: Change source path for selected entry",
        "X: Reset destination to ux0:VitaVault",
    };

    const int row_count = (int)(sizeof(rows) / sizeof(rows[0]));
    const int item_h = 40;
    int y = 68;

    for (int i = 0; i < row_count; i++) {
        if (i == selected) {
            vita2d_draw_rectangle(10, y - 4, g_screen_w - 20, item_h - 2, COLOR_BG_SELECTED);
        }

        unsigned int label_color = (i == selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_MAIN;
        draw_text(20, y + 4, label_color, 0.95f, rows[i].label);
        draw_icon_play(g_screen_w - 28, y + 4, COLOR_ACCENT);

        y += item_h;

        if (rows[i].divider_after) {
            vita2d_draw_rectangle(20, y - 6, g_screen_w - 40, 1, COLOR_BORDER);
            y += 4;
        }
    }

    char buf[PATH_MAX_SIZE + 64];
    snprintf(buf, sizeof(buf), "Current destination: %s", g_backup_root);
    draw_text(20, y + 10, COLOR_TEXT_DIM, 0.75f, buf);

    draw_settings_footer((selected >= 0 && selected < row_count) ?
        help[selected] : "▲/▼ Navigate   X Select   O Back");

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void draw_settings_screen(int selected) {
    vita2d_start_drawing();
    vita2d_clear_screen();

    char buf[PATH_MAX_SIZE + 64];
    draw_panel(0, 0, g_screen_w, 55, COLOR_BG_HEADER);
    draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, tr("sidebar_settings"));

    typedef struct {
        const char *label;
        int divider_after;
    } SettingRow;

    static const SettingRow rows[] = {
        { "setting_compression", 0 },
        { "setting_checksum", 1 },
        { "setting_language", 0 },
        { "setting_profile", 0 },
        { "setting_advanced", 0 },
    };

    static const char *help[] = {
        "X: Toggle ZIP compression for backups",
        "X: Toggle MD5 checksum generation",
        "X: Cycle language (English / Italiano)",
        "X: Cycle profile (NONE / MINIMAL / NORMAL / COMPLETE)",
        "X: Open advanced storage options",
    };

    const int row_count = (int)(sizeof(rows) / sizeof(rows[0]));
    const int item_h = 40;
    int y = 68;

    for (int i = 0; i < row_count; i++) {
        if (i == selected) {
            vita2d_draw_rectangle(10, y - 4, g_screen_w - 20, item_h - 2, COLOR_BG_SELECTED);
        }

        unsigned int label_color = (i == selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_MAIN;

        if (i == 0) {
            draw_checkbox(20, y + 4, ftp_config.compression);
            draw_text(45, y + 4, label_color, 0.95f, tr(rows[i].label));
            const char *state = ftp_config.compression ? "ON" : "OFF";
            unsigned int state_color = ftp_config.compression ? COLOR_GREEN : COLOR_TEXT_DIM;
            int sw = text_width_at(state, 0.9f);
            draw_text(g_screen_w - 20 - sw, y + 6, state_color, 0.9f, state);
        } else if (i == 1) {
            draw_checkbox(20, y + 4, ftp_config.checksum);
            draw_text(45, y + 4, label_color, 0.95f, tr(rows[i].label));
            const char *state = ftp_config.checksum ? "ON" : "OFF";
            unsigned int state_color = ftp_config.checksum ? COLOR_GREEN : COLOR_TEXT_DIM;
            int sw = text_width_at(state, 0.9f);
            draw_text(g_screen_w - 20 - sw, y + 6, state_color, 0.9f, state);
        } else if (i == 2) {
            draw_text(20, y + 4, label_color, 0.95f, tr(rows[i].label));
            const char *lang_name = g_languages[g_current_language].name;
            int sw = text_width_at(lang_name, 0.75f);
            draw_text(g_screen_w - 20 - sw, y + 6, COLOR_ACCENT, 0.75f, lang_name);
        } else if (i == 3) {
            draw_text(20, y + 4, label_color, 0.95f, tr(rows[i].label));
            snprintf(buf, sizeof(buf), "%s", profile_names[current_profile]);
            int bw = text_width_at(buf, 0.9f);
            draw_text(g_screen_w - 20 - bw, y + 6, COLOR_ACCENT, 0.9f, buf);
        } else {
            draw_text(20, y + 4, label_color, 0.95f, tr(rows[i].label));
            draw_icon_play(g_screen_w - 28, y + 4, COLOR_TEXT_DIM);
        }

        y += item_h;

        if (rows[i].divider_after) {
            vita2d_draw_rectangle(20, y - 6, g_screen_w - 40, 1, COLOR_BORDER);
            y += 4;
        }
    }

    draw_settings_footer((selected >= 0 && selected < row_count) ?
        help[selected] : "▲/▼ Navigate   X Select   O Back");

    vita2d_end_drawing();
    vita2d_swap_buffers();
}


void draw_language_selection_screen(int selected) {
    vita2d_start_drawing();
    vita2d_clear_screen();

    draw_panel(0, 0, g_screen_w, 55, COLOR_BG_HEADER);
    draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, tr("setting_language"));

    int list_x = 20;
    int list_y = 70;
    int list_w = g_screen_w - 40;
    int item_h = 45;

    if (g_num_languages == 0) {
        draw_text(list_x, list_y + 20, COLOR_TEXT_DIM, 0.9f, "No languages found in ux0:data/VitaVault/lang/");
        draw_settings_footer("O: Back to settings");
    } else {
        int visible = (g_screen_h - list_y - 40) / item_h;
        int start = 0;
        if (selected >= visible) start = selected - visible + 1;

        for (int i = start; i < g_num_languages && i < start + visible; i++) {
            int y = list_y + (i - start) * item_h;

            if (i == selected) {
                vita2d_draw_rectangle(list_x - 5, y - 3, list_w + 10, item_h - 2, COLOR_BG_SELECTED);
            }

            unsigned int text_color = (i == selected) ? COLOR_TEXT_BRIGHT : COLOR_TEXT_MAIN;

            if (i == g_current_language) {
                draw_text(list_x, y + 4, COLOR_GREEN, 0.9f, "> ");
                draw_text(list_x + 25, y + 4, text_color, 0.9f, g_languages[i].name);
            } else {
                draw_text(list_x + 25, y + 4, text_color, 0.9f, g_languages[i].name);
            }
        }

        draw_scrollbar(g_num_languages, visible, selected,
                       list_x + list_w - 8, list_y, visible * item_h);

        draw_settings_footer("▲/▼ Navigate   X Select   O Back");
    }

    vita2d_end_drawing();
    vita2d_swap_buffers();
}
