/*
    VitaVault - PSVita Backup Utility
    
    Copyright (C) 2026 theheroGAC
    Licensed under the GNU GPLv3. See LICENSE for details.
  
*/

#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <vita2d.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ftpvita.h>
#include <psp2/ime_dialog.h>

#include "types.h"
#include "backup.h"
#include "ui.h"
#include "ftp.h"
#include "usb.h"
#include "language.h"
#include "game_backup.h"
#include "zip_utils.h"

int ime_get_text(const char *title, const char *initial_text, char *out_text, int max_len) {
    SceImeDialogParam param;
    sceImeDialogParamInit(&param);

    SceWChar16 title_w[64];
    SceWChar16 text_w[64];
    SceWChar16 out_w[64];

    for (int i = 0; i < 64; i++) {
        title_w[i] = (SceWChar16)title[i];
        if (title[i] == '\0') break;
    }
    for (int i = 0; i < 64; i++) {
        text_w[i] = (SceWChar16)initial_text[i];
        if (initial_text[i] == '\0') break;
    }

    param.sdkVersion = 0x03570011;
    param.title = title_w;
    param.inputTextBuffer = out_w;
    param.maxTextLength = max_len;
    param.initialText = text_w;
    param.type = SCE_IME_TYPE_DEFAULT;
    param.option = SCE_IME_OPTION_NO_ASSISTANCE;
    out_w[0] = 0;

    if (sceImeDialogInit(&param) < 0) return 0;

    while (1) {
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);

        SceCommonDialogStatus status = sceImeDialogGetStatus();
        if (status == SCE_COMMON_DIALOG_STATUS_FINISHED) break;
        if (status == SCE_COMMON_DIALOG_STATUS_NONE) break;

        sceKernelDelayThread(16000);
    }

    SceImeDialogResult result;
    memset(&result, 0, sizeof(SceImeDialogResult));
    sceImeDialogGetResult(&result);

    if (result.button == SCE_IME_DIALOG_BUTTON_ENTER) {
        for (int i = 0; i < max_len; i++) {
            out_text[i] = (char)out_w[i];
            if (out_w[i] == '\0') break;
        }
        return 1;
    }
    return 0;
}

static const char *partitions[] = {"ux0:", "ur0:", "uma0:", "imc0:", "os0:", "pd0:", "vd0:", "tm0:", "ud0:"};

int run_file_browser(const char *start_path, char *out_path, int for_destination) {
    char current[PATH_MAX_SIZE];
    int is_root_selection = 0;

    if (start_path && start_path[0] != '\0' && strcmp(start_path, "DEVICE_ROOT") != 0) {
        strncpy(current, start_path, PATH_MAX_SIZE - 1);
        current[PATH_MAX_SIZE - 1] = '\0';
    } else {
        strcpy(current, "ux0:");
    }

    char dir_names[200][256];
    int count = 0;
    int selected = 0;
    SceCtrlData pad;

    while (1) {
        count = 0;

        if (is_root_selection) {
            for (int i = 0; i < (int)(sizeof(partitions)/sizeof(partitions[0])); i++) {
                if (for_destination) {
                    if (strcmp(partitions[i], "os0:") == 0 || 
                        strcmp(partitions[i], "vd0:") == 0 ||
                        strcmp(partitions[i], "tm0:") == 0 ||
                        strcmp(partitions[i], "ud0:") == 0) {
                        continue;
                    }
                }

                SceUID dfd = sceIoDopen(partitions[i]);
                if (dfd >= 0) {
                    sceIoDclose(dfd);
                    strncpy(dir_names[count++], partitions[i], 255);
                }
            }
        } else {
            SceUID dfd = sceIoDopen(current);
            if (dfd < 0) {
                is_root_selection = 1;
                continue;
            }

            strcpy(dir_names[count++], "..");

            if (dfd >= 0) {
                SceIoDirent ent;
                while (sceIoDread(dfd, &ent) > 0 && count < 200) {
                    if (SCE_S_ISDIR(ent.d_stat.st_mode)) {
                        if (strcmp(ent.d_name, ".") == 0 || strcmp(ent.d_name, "..") == 0) continue;
                        strncpy(dir_names[count++], ent.d_name, 255);
                    }
                }
                sceIoDclose(dfd);
            }
        }

        int browsing = 1;
        while (browsing) {
            draw_file_browser(is_root_selection ? "Partitions" : current, dir_names, count, selected);
            sceCtrlPeekBufferPositive(0, &pad, 1);
            
            if (pad.buttons & SCE_CTRL_UP) {
                selected--; if (selected < 0) selected = count - 1;
                sceKernelDelayThread(150000);
            }
            if (pad.buttons & SCE_CTRL_DOWN) {
                selected++; if (selected >= count) selected = 0;
                sceKernelDelayThread(150000);
            }
            if (pad.buttons & SCE_CTRL_CROSS) {
                if (is_root_selection) {
                    strncpy(current, dir_names[selected], PATH_MAX_SIZE - 1);
                    is_root_selection = 0;
                } else {
                    if (selected == 0) {
                        char *slash = strrchr(current, '/');
                        if (slash) *slash = '\0';
                        else is_root_selection = 1; 
                    } else {
                        int len = strlen(current);
                        if (len > 0 && current[len-1] != '/' && current[len-1] != ':') {
                            snprintf(current + len, PATH_MAX_SIZE - len, "/%s", dir_names[selected]);
                        } else {
                            snprintf(current + len, PATH_MAX_SIZE - len, "%s", dir_names[selected]);
                        }
                    }
                }
                selected = 0;
                browsing = 0;
                sceKernelDelayThread(200000);
            }
            if (pad.buttons & SCE_CTRL_TRIANGLE) {
                sceKernelDelayThread(150000);
                char folder_name[64];
                char new_path[PATH_MAX_SIZE + 64];
                int folder_idx = 1;
                SceIoStat stat;

                while (1) {
                    snprintf(folder_name, sizeof(folder_name), "BACKUP%d", folder_idx);
                    snprintf(new_path, sizeof(new_path), "%s/%s", current, folder_name);
                    
                    if (sceIoGetstat(new_path, &stat) < 0) break;
                    folder_idx++;
                }

                create_dir(new_path);
                char msg[128];
                snprintf(msg, sizeof(msg), "Created: %s", folder_name);
                ui_set_notification(msg);
                strncpy(current, new_path, PATH_MAX_SIZE - 1);
                selected = 0;
                
                browsing = 0;

                sceKernelDelayThread(150000);
            }
            if (pad.buttons & SCE_CTRL_START) {
                if (selected > 0) {
                    int len = strlen(current);
                    if (len > 0 && current[len-1] != '/' && current[len-1] != ':') {
                        snprintf(current + len, PATH_MAX_SIZE - len, "/%s", dir_names[selected]);
                    } else {
                        snprintf(current + len, PATH_MAX_SIZE - len, "%s", dir_names[selected]);
                    }
                }

                if (for_destination && (strncmp(current, "os0:", 4) == 0 || strncmp(current, "vd0:", 4) == 0 || strncmp(current, "tm0:", 4) == 0)) {
                    ui_set_notification("Error: Cannot use system partition as target!");
                    sceKernelDelayThread(300000);
                    continue;
                }

                strcpy(out_path, current);
                sceKernelDelayThread(300000);
                return 1;
            }
            if (pad.buttons & SCE_CTRL_CIRCLE) {
                sceKernelDelayThread(300000);
                return 0;
            }
            sceKernelDelayThread(16000);
        }
    }
}

int main() {
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);

    vita2d_init();
    vita2d_set_clear_color(COLOR_BG_MAIN);
    init_font();

    init_dynamic_arrays();
    load_config();
    language_init();
    usb_init();
    scan_games();

    if (ftp_config.enabled) {
        net_init();
    }

    SceCtrlData pad;
    int selected = 0;
    int running = 1;

    while (running) {

        draw_main_menu(selected);
        sceCtrlPeekBufferPositive(0, &pad, 1);
        sceKernelDelayThread(10000);
        if (pad.buttons & SCE_CTRL_LEFT) {
            g_sidebar_selected--;
            if (g_sidebar_selected < 0) g_sidebar_selected = 6;
            selected = 0;
            sceKernelDelayThread(150000);
        }
        if (pad.buttons & SCE_CTRL_RIGHT) {
            g_sidebar_selected++;
            if (g_sidebar_selected > 6) g_sidebar_selected = 0;
            selected = 0;
            sceKernelDelayThread(150000);
        }
        if (g_sidebar_selected == 0) {
            if (pad.buttons & SCE_CTRL_UP) {
                selected--;
                if (selected < 0) selected = ENTRY_COUNT - 1;
                sceKernelDelayThread(150000);
            }
            if (pad.buttons & SCE_CTRL_DOWN) {
                selected++;
                if (selected >= ENTRY_COUNT) selected = 0;
                sceKernelDelayThread(150000);
            }
        } else if (g_sidebar_selected == 1) {
            int backup_count = list_backups(g_backups, g_backups_capacity);
            if (backup_count > 0) {
                if (pad.buttons & SCE_CTRL_UP) {
                    selected--;
                    if (selected < 0) selected = backup_count - 1;
                    sceKernelDelayThread(150000);
                }

                if (pad.buttons & SCE_CTRL_DOWN) {
                    selected++;
                    if (selected >= backup_count) selected = 0;
                    sceKernelDelayThread(150000);
                }
            }
        } else if (g_sidebar_selected == 2) {
            // Games mode - show game list
            if (GAME_COUNT > 0) {
                if (pad.buttons & SCE_CTRL_UP) {
                    selected--;
                    if (selected < 0) selected = GAME_COUNT - 1;
                    sceKernelDelayThread(150000);
                }

                if (pad.buttons & SCE_CTRL_DOWN) {
                    selected++;
                    if (selected >= GAME_COUNT) selected = 0;
                    sceKernelDelayThread(150000);
                }
            }
        } else if (g_sidebar_selected == 3) {
            // Tools mode - 3 options
            if (pad.buttons & SCE_CTRL_UP) {
                selected--;
                if (selected < 0) selected = 2;
                sceKernelDelayThread(150000);
            }

            if (pad.buttons & SCE_CTRL_DOWN) {
                selected++;
                if (selected > 2) selected = 0;
                sceKernelDelayThread(150000);
            }
        } else if (g_sidebar_selected == 4) {
            // FTP mode - 7 options
            if (pad.buttons & SCE_CTRL_UP) {
                selected--;
                if (selected < 0) selected = 6;
                sceKernelDelayThread(150000);
            }

            if (pad.buttons & SCE_CTRL_DOWN) {
                selected++;
                if (selected > 6) selected = 0;
                sceKernelDelayThread(150000);
            }
        } else if (g_sidebar_selected == 5) {
            // USB mode - 2 options
            if (pad.buttons & SCE_CTRL_UP) {
                selected--;
                if (selected < 0) selected = 1;
                sceKernelDelayThread(150000);
            }

            if (pad.buttons & SCE_CTRL_DOWN) {
                selected++;
                if (selected > 1) selected = 0;
                sceKernelDelayThread(150000);
            }
        } else if (g_sidebar_selected == 6) {
            // Settings mode - 5 options
            if (pad.buttons & SCE_CTRL_UP) {
                selected--;
                if (selected < 0) selected = 4;
                sceKernelDelayThread(150000);
            }

            if (pad.buttons & SCE_CTRL_DOWN) {
                selected++;
                if (selected > 4) selected = 0;
                sceKernelDelayThread(150000);
            }
        }

        if (pad.buttons & SCE_CTRL_CIRCLE) {
            // O button - back (no action in main menu, could be used for submenus)
            sceKernelDelayThread(150000);
        }
        if (pad.buttons & SCE_CTRL_TRIANGLE) {
            if (g_sidebar_selected == 0) {
                g_backup_count = list_backups(g_backups, g_backups_capacity);
                int mgr_selected = 0;
                int mgr_running = 1;

                while (mgr_running) {
                    draw_manage_list(mgr_selected, g_backup_count);

                    if (g_backup_count == 0) {
                        sceKernelDelayThread(500000);
                        mgr_running = 0;
                        continue;
                    }

                    sceCtrlPeekBufferPositive(0, &pad, 1);
                    sceKernelDelayThread(10000);

                    if (pad.buttons & SCE_CTRL_UP) {
                        mgr_selected--;
                        if (mgr_selected < 0) mgr_selected = g_backup_count - 1;
                        sceKernelDelayThread(150000);
                    }
                    if (pad.buttons & SCE_CTRL_DOWN) {
                        mgr_selected++;
                        if (mgr_selected >= g_backup_count) mgr_selected = 0;
                        sceKernelDelayThread(150000);
                    }
                    if (pad.buttons & SCE_CTRL_CIRCLE) {
                        sceKernelDelayThread(150000);
                        mgr_running = 0;
                    }

                    if (pad.buttons & SCE_CTRL_CROSS) {
                        sceKernelDelayThread(150000);
                        int detail_running = 1;

                        while (detail_running) {
                            draw_backup_details(&g_backups[mgr_selected]);
                            sceCtrlPeekBufferPositive(0, &pad, 1);
                            sceKernelDelayThread(10000);

                            if (pad.buttons & SCE_CTRL_CIRCLE) {
                                sceKernelDelayThread(150000);
                                detail_running = 0;
                            }

                            if (pad.buttons & SCE_CTRL_CROSS) {
                                sceKernelDelayThread(150000);

                            if (!draw_confirm_screen(tr("conf_restore_title"), tr("conf_restore_msg"))) {
                                    continue;
                                }

                                BackupInfo *b = &g_backups[mgr_selected];
                                int total_files = 0, total_errors = 0;
                                SceOff total_bytes = 0;

                                SceUID sd = sceIoDopen(b->path);
                                if (sd >= 0) {
                                    SceIoDirent se;
                                    memset(&se, 0, sizeof(se));
                                    while (sceIoDread(sd, &se) > 0) {
                                        if (strcmp(se.d_name, ".") == 0 ||
                                            strcmp(se.d_name, "..") == 0) {
                                            memset(&se, 0, sizeof(se));
                                            continue;
                                        }
                                        if (SCE_S_ISDIR(se.d_stat.st_mode)) {
                                            const char *restore_to = NULL;
                                            for (int e = 0; e < ENTRY_COUNT; e++) {
                                                if (strcmp(se.d_name, entries[e].name) == 0) {
                                                    restore_to = entries[e].source;
                                                    break;
                                                }
                                            }
                                            if (restore_to) {
                                                char ep[PATH_MAX_SIZE];
                                                int needed = snprintf(ep, sizeof(ep), "%s/%s",
                                                                      b->path, se.d_name);
                                                if (needed >= (int)sizeof(ep)) {
                                                    continue;
                                                }
                                                draw_restore_progress(se.d_name,
                                                                      total_files,
                                                                      total_errors);
                                                int fr = 0;
                                                SceOff br = 0;
                                                int errs = 0;
                                                restore_entry(ep, restore_to,
                                                              &fr, &br, &errs);
                                                total_files += fr;
                                                total_bytes += br;
                                                total_errors += errs;
                                            }
                                        }
                                        memset(&se, 0, sizeof(se));
                                    }
                                    sceIoDclose(sd);
                                }

                                char msg[128];
                                snprintf(msg, sizeof(msg), "Restore complete: %d files, %d errors",
                                         total_files, total_errors);
                                ui_set_notification(msg);
                                sceKernelDelayThread(2000000);
                                detail_running = 0;
                            }

                            if (pad.buttons & SCE_CTRL_SELECT) {
                                sceKernelDelayThread(150000);

                                if (!draw_confirm_screen(tr("conf_delete_title"), tr("conf_delete_msg"))) {
                                    continue;
                                }

                                BackupInfo *b = &g_backups[mgr_selected];
                                delete_directory(b->path);

                                char buf[256];
                                snprintf(buf, sizeof(buf),
                                    "Backup deleted.\n\n"
                                    "Press any button.");
                                draw_text_screen("Backup Deleted", buf);
                                sceKernelDelayThread(1500000);

                                g_backup_count = list_backups(g_backups, g_backups_capacity);
                                if (mgr_selected >= g_backup_count)
                                    mgr_selected = g_backup_count - 1;
                                if (mgr_selected < 0) mgr_selected = 0;
                                detail_running = 0;
                            }
                        }
                    }
                }
            }
            sceKernelDelayThread(150000);
        }

        if (pad.buttons & SCE_CTRL_SQUARE) {
            // SQUARE button - toggle entry in Backup mode
            if (g_sidebar_selected == 0) {
                entries[selected].enabled = !entries[selected].enabled;
                save_config();
            }
            sceKernelDelayThread(150000);
        }

        if (pad.buttons & SCE_CTRL_CROSS) {
            if (g_sidebar_selected == 0) {
                // Backup mode - start backup
                sceKernelDelayThread(150000);

                int active = 0;
                for (int i = 0; i < ENTRY_COUNT; i++)
                    if (entries[i].enabled) active++;

                if (active == 0) {
                    draw_text_screen(tr("noti_no_entries_title"), tr("noti_no_entries_msg"));
                    sceKernelDelayThread(2000000);
                    continue;
                }

                if (!draw_confirm_screen(tr("conf_backup_title"), tr("conf_backup_msg"))) {
                    continue;
                }

                SceOff needed = 0;
                SceOff free_space = get_free_space("ux0:");
                for (int i = 0; i < ENTRY_COUNT; i++) {
                    if (!entries[i].enabled) continue;
                    int fc = 0;
                    SceOff fs = 0;
                    if (count_files_recursive(entries[i].source, &fc, &fs) == 0)
                        needed += fs;
                }

                int space_status = 0;
                if (free_space == 0) space_status = 1;
                else if (needed >= free_space) space_status = 2;

                int choice = 0;
                if (space_status == 2) {
                    while (choice == 0) {
                        draw_space_check(space_status, needed, free_space);
                        sceCtrlPeekBufferPositive(0, &pad, 1);
                        sceKernelDelayThread(10000);
                        if (pad.buttons & SCE_CTRL_CROSS) {
                            choice = 1;
                            sceKernelDelayThread(150000);
                        }
                        if (pad.buttons & SCE_CTRL_CIRCLE) {
                            choice = 2;
                            sceKernelDelayThread(150000);
                        }
                    }
                    if (choice == 2) continue;
                }

                create_dir("ux0:data");
                create_dir(g_backup_root);

                char backup_root[PATH_MAX_SIZE + 128];
                BackupLog log;
                do_backup(backup_root, sizeof(backup_root), &log);

                if (log.errors == 0) cleanup_old_backups(5);

                if (ftp_config.enabled && log.errors == 0) {
                    net_init();
                    ftp_post_backup_screen(backup_root);
                } else {
                    int done_choice = 0;
                    while (done_choice == 0) {
                        draw_backup_complete(&log);
                        sceCtrlPeekBufferPositive(0, &pad, 1);
                        sceKernelDelayThread(10000);

                        if (pad.buttons & SCE_CTRL_SQUARE) {
                            sceKernelDelayThread(150000);

                            if (g_preferred_usb_device[0] != '\0') {
                                int res = usb_start_mass_storage_with_device(g_preferred_usb_device);
                                if (res == 0) {
                                    const char *folder = strrchr(backup_root, '/');
                                    if (folder) folder++;
                                    else {
                                        folder = strrchr(backup_root, ':');
                                        folder = folder ? folder + 1 : backup_root;
                                    }

                                    char folder_name_display[64];
                                    strncpy(folder_name_display, folder, sizeof(folder_name_display) - 1);
                                    folder_name_display[sizeof(folder_name_display) - 1] = '\0';

                                    char usb_msg[512];
                                    snprintf(usb_msg, sizeof(usb_msg),
                                        "USB Mass Storage active.\n\n"
                                        "Device: %s\n\n"
                                        "On PC open the Vita drive:\n"
                                        "data/VitaVault/%s\n\n"
                                        "Use download_backup.bat option 5,\n"
                                        "or copy the folder manually.\n\n"
                                        "Press O to disconnect USB.",
                                        g_preferred_usb_name[0] != '\0' ? g_preferred_usb_name : g_preferred_usb_device, folder_name_display);

                                    int usb_loop = 1;
                                    while (usb_loop) {
                                        draw_text_screen("USB Transfer to PC", usb_msg);
                                        sceCtrlPeekBufferPositive(0, &pad, 1);
                                        sceKernelDelayThread(10000);
                                        if (pad.buttons & SCE_CTRL_CIRCLE) {
                                            usb_stop_mass_storage();
                                            usb_loop = 0;
                                            sceKernelDelayThread(150000);
                                        }
                                    }
                                } else {
                                    char err_msg[64];
                                    if (res == (int)0x80800001)
                                        snprintf(err_msg, sizeof(err_msg), "USB modules missing in VPK");
                                    else if (res == (int)0x80800002)
                                        snprintf(err_msg, sizeof(err_msg), "USB module copy failed");
                                    else if (res < 0)
                                        snprintf(err_msg, sizeof(err_msg), "Memory not found");
                                    else
                                        snprintf(err_msg, sizeof(err_msg), "USB Error: 0x%08X", res);
                                    ui_set_notification(err_msg);
                                    sceKernelDelayThread(300000);
                                }
                            } else {
                                const char *device_names[8];
                                const char *device_paths[8];
                                int device_count = 0;

                                device_names[device_count] = "Memory Card";
                                device_paths[device_count] = "sdstor0:xmc-lp-ign-userext";
                                device_count++;

                                device_names[device_count] = "SD2VITA";
                                device_paths[device_count] = "sdstor0:gcd-lp-ign-entire";
                                device_count++;

                                device_names[device_count] = "PSVSD";
                                device_paths[device_count] = "sdstor0:uma-lp-act-entire";
                                device_count++;

                                device_names[device_count] = "Game Card";
                                device_paths[device_count] = "sdstor0:gcd-lp-ign-entire";
                                device_count++;

                                if (device_count == 0) {
                                    ui_set_notification("No storage devices found!");
                                    sceKernelDelayThread(300000);
                                } else {
                                    int selected_usb = draw_storage_selection_menu(device_names, device_paths, device_count);
                                    if (selected_usb >= 0) {
                                        int res = usb_start_mass_storage_with_device(device_paths[selected_usb]);
                                        if (res == 0) {
                                            const char *folder = strrchr(backup_root, '/');
                                            if (folder) folder++;
                                            else {
                                                folder = strrchr(backup_root, ':');
                                                folder = folder ? folder + 1 : backup_root;
                                            }

                                            char folder_name_display[64];
                                            strncpy(folder_name_display, folder, sizeof(folder_name_display) - 1);
                                            folder_name_display[sizeof(folder_name_display) - 1] = '\0';

                                            char usb_msg[512];
                                            snprintf(usb_msg, sizeof(usb_msg),
                                                "USB Mass Storage active.\n\n"
                                                "Device: %s\n\n"
                                                "On PC open the Vita drive:\n"
                                                "data\\VitaVault\\%s\n\n"
                                                "Use download_backup.bat option 5,\n"
                                                "or copy the folder manually.\n\n"
                                                "Press O to disconnect USB.",
                                                device_names[selected_usb], folder_name_display);

                                            int usb_loop = 1;
                                            while (usb_loop) {
                                                draw_text_screen("USB Transfer to PC", usb_msg);
                                                sceCtrlPeekBufferPositive(0, &pad, 1);
                                                sceKernelDelayThread(10000);
                                                if (pad.buttons & SCE_CTRL_CIRCLE) {
                                                    usb_stop_mass_storage();
                                                    usb_loop = 0;
                                                    sceKernelDelayThread(150000);
                                                }
                                            }
                                        } else {
                                            char err_msg[64];
                                            if (res == (int)0x80800001)
                                                snprintf(err_msg, sizeof(err_msg), "USB modules missing in VPK");
                                            else if (res == (int)0x80800002)
                                                snprintf(err_msg, sizeof(err_msg), "USB module copy failed");
                                            else
                                                snprintf(err_msg, sizeof(err_msg), "USB Error: 0x%08X", res);
                                            ui_set_notification(err_msg);
                                            sceKernelDelayThread(300000);
                                        }
                                    }
                                }
                            }
                        }

                        if (pad.buttons & SCE_CTRL_START || pad.buttons & SCE_CTRL_CIRCLE) {
                            done_choice = 1;
                            sceKernelDelayThread(150000);
                        }
                    }
                }
            } else if (g_sidebar_selected == 1) {
                // Restore mode - show backup details and restore
                int backup_count = list_backups(g_backups, g_backups_capacity);
                if (backup_count > 0 && selected < backup_count) {
                    int detail_running = 1;
                    while (detail_running) {
                        draw_backup_details(&g_backups[selected]);
                        sceCtrlPeekBufferPositive(0, &pad, 1);
                        sceKernelDelayThread(10000);

                        if (pad.buttons & SCE_CTRL_CIRCLE) {
                            sceKernelDelayThread(150000);
                            detail_running = 0;
                        }

                        if (pad.buttons & SCE_CTRL_CROSS) {
                            sceKernelDelayThread(150000);

                            if (!draw_confirm_screen(tr("conf_restore_title"), tr("conf_restore_msg"))) {
                                continue;
                            }

                            BackupInfo *b = &g_backups[selected];
                            int total_files = 0, total_errors = 0;
                            SceOff total_bytes = 0;

                            SceUID sd = sceIoDopen(b->path);
                            if (sd >= 0) {
                                SceIoDirent se;
                                memset(&se, 0, sizeof(se));
                                while (sceIoDread(sd, &se) > 0) {
                                    if (strcmp(se.d_name, ".") == 0 ||
                                        strcmp(se.d_name, "..") == 0) {
                                        memset(&se, 0, sizeof(se));
                                        continue;
                                    }
                                    if (SCE_S_ISDIR(se.d_stat.st_mode)) {
                                        const char *restore_to = NULL;
                                        for (int e = 0; e < ENTRY_COUNT; e++) {
                                            if (strcmp(se.d_name, entries[e].name) == 0) {
                                                restore_to = entries[e].source;
                                                break;
                                            }
                                        }
                                        if (restore_to) {
                                            char ep[PATH_MAX_SIZE];
                                            int needed = snprintf(ep, sizeof(ep), "%s/%s",
                                                                  b->path, se.d_name);
                                            if (needed >= (int)sizeof(ep)) {
                                                continue;
                                            }
                                            draw_restore_progress(se.d_name,
                                                                  total_files,
                                                                  total_errors);
                                            int fr = 0;
                                            SceOff br = 0;
                                            int errs = 0;
                                            restore_entry(ep, restore_to,
                                                          &fr, &br, &errs);
                                            total_files += fr;
                                            total_bytes += br;
                                            total_errors += errs;
                                        }
                                    }
                                    memset(&se, 0, sizeof(se));
                                }
                                sceIoDclose(sd);
                            }

                            char msg[128];
                            snprintf(msg, sizeof(msg), "Restore complete: %d files, %d errors",
                                     total_files, total_errors);
                            ui_set_notification(msg);
                            sceKernelDelayThread(2000000);
                            detail_running = 0;
                        }

                        if (pad.buttons & SCE_CTRL_SELECT) {
                            sceKernelDelayThread(150000);

                            if (!draw_confirm_screen(tr("conf_delete_title"), tr("conf_delete_msg"))) {
                                continue;
                            }

                            BackupInfo *b = &g_backups[selected];
                            delete_directory(b->path);

                            char buf[256];
                            snprintf(buf, sizeof(buf),
                                "Backup deleted.\n\n"
                                "Press any button.");
                            draw_text_screen("Backup Deleted", buf);
                            sceKernelDelayThread(1500000);

                            backup_count = list_backups(g_backups, g_backups_capacity);
                            if (selected >= backup_count)
                                selected = backup_count - 1;
                            if (selected < 0) selected = 0;
                            detail_running = 0;
                        }
                    }
                }
            } else if (g_sidebar_selected == 2) {
                // Games mode
                if (GAME_COUNT > 0 && selected >= 0 && selected < GAME_COUNT) {
                    int result = backup_game(selected, g_backup_root, NULL);
                    if (result == 0) {
                        char msg[160];
                        snprintf(msg, sizeof(msg), "Backed up: %s", games[selected].name);
                        ui_set_notification(msg);
                    } else {
                        ui_set_notification("Game backup failed");
                    }
                    sceKernelDelayThread(500000);
                }
            } else if (g_sidebar_selected == 3) {
                // Tools mode
                if (selected == 0) {
                    // Change backup destination
                    char new_root[PATH_MAX_SIZE];
                    if (run_file_browser(g_backup_root, new_root, 1)) {
                        strncpy(g_backup_root, new_root, PATH_MAX_SIZE - 1);
                        g_backup_root[PATH_MAX_SIZE - 1] = '\0';
                        save_config();
                        ui_set_notification(tr("noti_dest_updated"));
                    }
                } else if (selected == 1) {
                    // Change entry source path
                    char new_path[PATH_MAX_SIZE];
                    if (run_file_browser(entries[0].source, new_path, 0)) {
                        strncpy(entries[0].source, new_path, PATH_MAX_SIZE - 1);
                        save_config();
                        ui_set_notification(tr("noti_path_updated"));
                    }
                } else if (selected == 2) {
                    // Reset backup destination
                    strcpy(g_backup_root, "ux0:VitaVault");
                    save_config();
                    ui_set_notification(tr("noti_dest_reset"));
                }
                sceKernelDelayThread(300000);
            } else if (g_sidebar_selected == 4) {
                // FTP mode
                if (selected == 0) {
                    ftp_config.enabled = !ftp_config.enabled;
                    save_config();
                } else if (selected == 6) {
                    ftp_server_run();
                }
                sceKernelDelayThread(150000);
            } else if (g_sidebar_selected == 5) {
                // USB mode
                if (selected == 0) {
                    if (g_usb_active) {
                        int res = usb_stop_mass_storage();
                        if (res == 0) {
                            ui_set_notification(tr("noti_usb_stopped"));
                        } else {
                            ui_set_notification(tr("noti_usb_error"));
                        }
                    } else {
                        if (g_ftp_active) {
                            ui_set_notification(tr("noti_stop_ftp_first"));
                        } else if (g_preferred_usb_device[0] != '\0') {
                            int res = usb_start_mass_storage_with_device(g_preferred_usb_device);
                            if (res == 0) {
                                int usb_loop = 1;
                                while (usb_loop) {
                                    vita2d_start_drawing();
                                    vita2d_clear_screen();
                                    draw_panel(0, 0, 960, 55, COLOR_BG_HEADER);
                                    draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, "USB Mass Storage");
                                    draw_text(30, 120, COLOR_ACCENT, 1.2f, "USB Connected to PC");
                                    draw_text(30, 160, COLOR_TEXT_MAIN, 1.0f, "Device:");
                                    draw_text(30, 190, COLOR_ACCENT, 1.0f, g_preferred_usb_name[0] != '\0' ? g_preferred_usb_name : g_preferred_usb_device);
                                    draw_text(30, 240, COLOR_TEXT_MAIN, 1.0f, "Do not disconnect the cable during transfer.");
                                    draw_text(30, 450, COLOR_TEXT_DIM, 0.8f, "Press CIRCLE to disconnect.");
                                    vita2d_end_drawing();
                                    vita2d_swap_buffers();

                                    sceCtrlPeekBufferPositive(0, &pad, 1);
                                    if (pad.buttons & SCE_CTRL_CIRCLE) {
                                        usb_stop_mass_storage();
                                        usb_loop = 0;
                                    }
                                    sceKernelDelayThread(16000);
                                }
                            } else {
                                char err_msg[64];
                                if (res == (int)0x80800001)
                                    snprintf(err_msg, sizeof(err_msg), "%s", tr("noti_modules_missing"));
                                else if (res == (int)0x80800002)
                                    snprintf(err_msg, sizeof(err_msg), "%s", tr("noti_module_copy"));
                                else if (res < 0)
                                    snprintf(err_msg, sizeof(err_msg), "%s", tr("noti_memory_not_found"));
                                else
                                    snprintf(err_msg, sizeof(err_msg), "USB Error: 0x%08X", res);
                                ui_set_notification(err_msg);
                            }
                        } else {
                            const char *device_names[8];
                            const char *device_paths[8];
                            int device_count = 0;

                            device_names[device_count] = tr("dev_memory");
                            device_paths[device_count] = "sdstor0:xmc-lp-ign-userext";
                            device_count++;

                            if (checkFolderExist("ux0:data/sd2vita.txt")) {
                                device_names[device_count] = tr("dev_sd2vita");
                                device_paths[device_count] = "sdstor0:gcd-lp-ign-entire";
                                device_count++;
                            }

                            if (checkFolderExist("xmc0:")) {
                                device_names[device_count] = tr("dev_memory");
                                device_paths[device_count] = "sdstor0:xmc-lp-ign-userext";
                                device_count++;
                            }

                            if (checkFolderExist("imc0:")) {
                                device_names[device_count] = tr("dev_memory");
                                device_paths[device_count] = "sdstor0:int-lp-ign-userext";
                                device_count++;
                            }

                            if (checkFolderExist("uma0:")) {
                                device_names[device_count] = tr("dev_memory");
                                device_paths[device_count] = "sdstor0:uma-lp-act-entire";
                                device_count++;
                            }

                            if (checkFolderExist("grw0:")) {
                                device_names[device_count] = tr("dev_game");
                                device_paths[device_count] = "sdstor0:gcd-lp-ign-entire";
                                device_count++;
                            }

                            if (device_count == 0) {
                                ui_set_notification(tr("noti_no_devices"));
                            } else {
                                int selected_device = draw_storage_selection_menu(device_names, device_paths, device_count);
                                if (selected_device >= 0) {
                                    int res = usb_start_mass_storage_with_device(device_paths[selected_device]);
                                    if (res == 0) {
                                        int usb_loop = 1;
                                        while (usb_loop) {
                                            vita2d_start_drawing();
                                            vita2d_clear_screen();
                                            draw_panel(0, 0, 960, 55, COLOR_BG_HEADER);
                                            draw_text(15, 14, COLOR_TEXT_BRIGHT, 1.4f, "USB Mass Storage");
                                            draw_text(30, 120, COLOR_ACCENT, 1.2f, "USB Connected to PC");
                                            draw_text(30, 160, COLOR_TEXT_MAIN, 1.0f, "Device:");
                                            draw_text(30, 190, COLOR_ACCENT, 1.0f, device_names[selected_device]);
                                            draw_text(30, 240, COLOR_TEXT_MAIN, 1.0f, "Do not disconnect the cable during transfer.");
                                            draw_text(30, 450, COLOR_TEXT_DIM, 0.8f, "Press CIRCLE to disconnect.");
                                            vita2d_end_drawing();
                                            vita2d_swap_buffers();

                                            sceCtrlPeekBufferPositive(0, &pad, 1);
                                            if (pad.buttons & SCE_CTRL_CIRCLE) {
                                                usb_stop_mass_storage();
                                                usb_loop = 0;
                                            }
                                            sceKernelDelayThread(16000);
                                        }
                                    } else {
                                        char err_msg[64];
                                        if (res == (int)0x80800001)
                                            snprintf(err_msg, sizeof(err_msg), "USB modules missing in VPK");
                                        else if (res == (int)0x80800002)
                                            snprintf(err_msg, sizeof(err_msg), "USB module copy failed");
                                        else
                                            snprintf(err_msg, sizeof(err_msg), "USB Error: 0x%08X", res);
                                        ui_set_notification(err_msg);
                                    }
                                }
                            }
                        }
                    }
                } else if (selected == 1) {
                    // Select preferred USB device
                    const char *device_names[8];
                    const char *device_paths[8];
                    int device_count = 0;

                    device_names[device_count] = tr("dev_memory");
                    device_paths[device_count] = "sdstor0:xmc-lp-ign-userext";
                    device_count++;

                    device_names[device_count] = tr("dev_sd2vita");
                    device_paths[device_count] = "sdstor0:gcd-lp-ign-entire";
                    device_count++;

                    device_names[device_count] = tr("dev_psvsd");
                    device_paths[device_count] = "sdstor0:uma-lp-act-entire";
                    device_count++;

                    device_names[device_count] = tr("dev_game");
                    device_paths[device_count] = "sdstor0:gcd-lp-ign-entire";
                    device_count++;

                    device_names[device_count] = tr("storage_auto");
                    device_paths[device_count] = "";
                    device_count++;

                    int selected_device = draw_storage_selection_menu(device_names, device_paths, device_count);
                    if (selected_device >= 0) {
                        if (selected_device == device_count - 1) {
                            g_preferred_usb_device[0] = '\0';
                            g_preferred_usb_name[0] = '\0';
                            ui_set_notification(tr("noti_usb_pref_auto"));
                        } else {
                            strncpy(g_preferred_usb_device, device_paths[selected_device], sizeof(g_preferred_usb_device) - 1);
                            g_preferred_usb_device[sizeof(g_preferred_usb_device) - 1] = '\0';
                            strncpy(g_preferred_usb_name, device_names[selected_device], sizeof(g_preferred_usb_name) - 1);
                            g_preferred_usb_name[sizeof(g_preferred_usb_name) - 1] = '\0';
                            char msg[128];
                            snprintf(msg, sizeof(msg), "USB preference: %s", device_names[selected_device]);
                            ui_set_notification(msg);
                        }
                        save_config();
                    }
                }
                sceKernelDelayThread(200000);
            } else if (g_sidebar_selected == 6) {
                // Settings mode
                if (selected == 0) {
                    // Cicla tra i livelli di compressione
                    int current_level = ftp_config.compression_level;
                    if (current_level == COMPRESS_NONE) ftp_config.compression_level = COMPRESS_FAST;
                    else if (current_level == COMPRESS_FAST) ftp_config.compression_level = COMPRESS_NORMAL;
                    else if (current_level == COMPRESS_NORMAL) ftp_config.compression_level = COMPRESS_MAXIMUM;
                    else if (current_level == COMPRESS_MAXIMUM) ftp_config.compression_level = COMPRESS_NONE;
                    else ftp_config.compression_level = COMPRESS_NORMAL; // Default
                    save_config();
                } else if (selected == 1) {
                    ftp_config.checksum = !ftp_config.checksum;
                    save_config();
                } else if (selected == 2) {
                    // Language selection screen
                    if (g_num_languages == 0) {
                        ui_set_notification("No languages found. Add .txt files to ux0:data/VitaVault/lang/");
                        sceKernelDelayThread(2000000);
                    } else {
                        int lang_selected = g_current_language;
                        int lang_running = 1;
                        int wait_for_release = 1; // Wait for button release first
                        
                        while (lang_running) {
                            draw_language_selection_screen(lang_selected);
                            sceCtrlPeekBufferPositive(0, &pad, 1);
                            sceKernelDelayThread(100000);
                            
                            if (wait_for_release) {
                                if (!(pad.buttons & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_UP | SCE_CTRL_DOWN))) {
                                    wait_for_release = 0;
                                }
                                continue;
                            }
                            
                            if (pad.buttons & SCE_CTRL_UP) {
                                lang_selected--;
                                if (lang_selected < 0) lang_selected = g_num_languages - 1;
                                sceKernelDelayThread(200000);
                            }
                            if (pad.buttons & SCE_CTRL_DOWN) {
                                lang_selected++;
                                if (lang_selected >= g_num_languages) lang_selected = 0;
                                sceKernelDelayThread(200000);
                            }
                            if (pad.buttons & SCE_CTRL_CROSS) {
                                g_current_language = lang_selected;
                                language_load(g_current_language);
                                save_config();
                                lang_running = 0;
                                sceKernelDelayThread(300000);
                            }
                            if (pad.buttons & SCE_CTRL_CIRCLE) {
                                lang_running = 0;
                                sceKernelDelayThread(300000);
                            }
                        }
                    }
                } else if (selected == 3) {
                    cycle_profile();
                } else if (selected == 4) {
                    // Advanced settings
                    int adv_selected = 0;
                    int adv_running = 1;
                    while (adv_running) {
                        draw_settings_advanced(adv_selected);
                        sceCtrlPeekBufferPositive(0, &pad, 1);
                        
                        if (pad.buttons & SCE_CTRL_UP) {
                            adv_selected--;
                            if (adv_selected < 0) adv_selected = 1;
                            sceKernelDelayThread(150000);
                        }
                        if (pad.buttons & SCE_CTRL_DOWN) {
                            adv_selected++;
                            if (adv_selected > 1) adv_selected = 0;
                            sceKernelDelayThread(150000);
                        }
                        if (pad.buttons & SCE_CTRL_CROSS) {
                            if (adv_selected == 0) {
                                int deleted = delete_logs();
                                char msg[64];
                                snprintf(msg, sizeof(msg), "Deleted %d log files", deleted);
                                ui_set_notification(msg);
                            } else if (adv_selected == 1) {
                                int res = reset_config();
                                ui_set_notification(res == 0 ? "Settings reset" : "Reset failed");
                            }
                            sceKernelDelayThread(200000);
                        }
                        if (pad.buttons & SCE_CTRL_CIRCLE) {
                            adv_running = 0;
                            sceKernelDelayThread(150000);
                        }
                        sceKernelDelayThread(16000);
                    }
                }
                sceKernelDelayThread(150000);
            }
        }

        
        if ((pad.buttons & SCE_CTRL_SQUARE) && !(pad.buttons & SCE_CTRL_SELECT) && g_sidebar_selected != 0) {
            char new_root[PATH_MAX_SIZE];
            if (run_file_browser(g_backup_root, new_root, 1)) {
                strncpy(g_backup_root, new_root, PATH_MAX_SIZE - 1);
                g_backup_root[PATH_MAX_SIZE - 1] = '\0';
                save_config();
                ui_set_notification(tr("noti_dest_updated"));
            }
            sceKernelDelayThread(300000);
            continue;
        }

        
        if ((pad.buttons & SCE_CTRL_SELECT) && (pad.buttons & SCE_CTRL_SQUARE)) {
            char new_path[PATH_MAX_SIZE];
            if (run_file_browser(entries[selected].source, new_path, 0)) {
                strncpy(entries[selected].source, new_path, PATH_MAX_SIZE - 1);
                save_config();
                ui_set_notification(tr("noti_path_updated"));
            }
            sceKernelDelayThread(300000);
            continue; 
        }

        
        if ((pad.buttons & SCE_CTRL_SELECT) && (pad.buttons & SCE_CTRL_TRIANGLE)) {
            strcpy(g_backup_root, "ux0:VitaVault");
            save_config();
            ui_set_notification(tr("noti_dest_reset"));
            sceKernelDelayThread(300000);
            continue;
        }



        if ((pad.buttons & SCE_CTRL_SELECT) && (pad.buttons & SCE_CTRL_DOWN)) {
            cycle_profile();
            ui_set_notification("Profile changed!");
            sceKernelDelayThread(250000);
            continue;
        }

        
        if (pad.buttons & SCE_CTRL_START) {
            sceKernelDelayThread(200000);
            ftp_server_run();
            sceKernelDelayThread(200000);
            continue;
        }
    }

    if (g_ftp_active) ftp_server_stop();
    net_term();
    cleanup_games_array();
    cleanup_dynamic_arrays();
    ui_fini();
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}