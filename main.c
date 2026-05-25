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
                        if (len > 0 && current[len-1] != '/' && current[len-1] != ':') strcat(current, "/");
                        strcat(current, dir_names[selected]);
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
                    if (len > 0 && current[len-1] != '/' && current[len-1] != ':') strcat(current, "/");
                    strcat(current, dir_names[selected]);
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

    load_config();
    apply_profile(current_profile);

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

        if (pad.buttons & SCE_CTRL_CIRCLE) {
            entries[selected].enabled = !entries[selected].enabled;
            save_config();
            sceKernelDelayThread(150000);
        }

        
        if ((pad.buttons & SCE_CTRL_SQUARE) && !(pad.buttons & SCE_CTRL_SELECT)) {
            char new_root[PATH_MAX_SIZE];
            if (run_file_browser(g_backup_root, new_root, 1)) {
                strncpy(g_backup_root, new_root, PATH_MAX_SIZE - 1);
                g_backup_root[PATH_MAX_SIZE - 1] = '\0';
                save_config();
                ui_set_notification("Global Destination updated!");
            }
            sceKernelDelayThread(300000);
            continue; 
        }

        
        if ((pad.buttons & SCE_CTRL_SELECT) && (pad.buttons & SCE_CTRL_SQUARE)) {
            char new_path[PATH_MAX_SIZE];
            if (run_file_browser(entries[selected].source, new_path, 0)) {
                strncpy(entries[selected].source, new_path, PATH_MAX_SIZE - 1);
                save_config();
                ui_set_notification("Entry Source Path updated!");
            }
            sceKernelDelayThread(300000);
            continue; 
        }

        
        if ((pad.buttons & SCE_CTRL_SELECT) && (pad.buttons & SCE_CTRL_TRIANGLE)) {
            strcpy(g_backup_root, "ux0:data/VitaVault");
            save_config();
            ui_set_notification("Destination reset to default!");
            sceKernelDelayThread(300000);
            continue;
        }

        
        // SELECT (da solo): Apre Impostazioni
        if ((pad.buttons & SCE_CTRL_SELECT) && !(pad.buttons & SCE_CTRL_SQUARE) && 
            !(pad.buttons & SCE_CTRL_TRIANGLE) && !(pad.buttons & SCE_CTRL_DOWN)) {
            sceKernelDelayThread(200000);
            int set_sel = 0;
            int set_running = 1;
            while (set_running) {
                draw_settings(set_sel);
                sceCtrlPeekBufferPositive(0, &pad, 1);
                
                if (pad.buttons & SCE_CTRL_UP) {
                    set_sel--; if (set_sel < 0) set_sel = 4;
                    sceKernelDelayThread(150000);
                }
                if (pad.buttons & SCE_CTRL_DOWN) {
                    set_sel++; if (set_sel > 4) set_sel = 0;
                    sceKernelDelayThread(150000);
                }
                if (pad.buttons & SCE_CTRL_CROSS) {
                    if (set_sel == 0) ftp_config.enabled = !ftp_config.enabled;
                    else if (set_sel == 1) ftp_config.compression = !ftp_config.compression;
                    else if (set_sel == 2) ftp_config.checksum = !ftp_config.checksum;
                    else if (set_sel == 3) cycle_profile();
                    else if (set_sel == 4) {
                        ftp_server_run();
                        sceKernelDelayThread(200000);
                    }
                    save_config();
                    sceKernelDelayThread(150000);
                }
                if (pad.buttons & SCE_CTRL_CIRCLE) {
                    set_running = 0;
                    sceKernelDelayThread(200000);
                }
                sceKernelDelayThread(16000);
            }
            continue;
        }

        // SELECT + GIU: Cicla Profili (veloce)
        if ((pad.buttons & SCE_CTRL_SELECT) && (pad.buttons & SCE_CTRL_DOWN)) {
            cycle_profile();
            ui_set_notification("Profile changed!");
            sceKernelDelayThread(250000);
            continue;
        }

        // START: Server FTP Immediato
        if (pad.buttons & SCE_CTRL_START) {
            sceKernelDelayThread(200000);
            ftp_server_run();
            sceKernelDelayThread(200000);
            continue;
        }

        
        if ((pad.buttons & SCE_CTRL_TRIANGLE) && !(pad.buttons & SCE_CTRL_SELECT)) {
            g_backup_count = list_backups(g_backups, MAX_BACKUPS);
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

                            if (!draw_confirm_screen("Confirm Restore",
                                "Are you sure you want to restore this backup?\nThis will overwrite your data!")) {
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

                            char sz[32];
                            format_size(sz, sizeof(sz), total_bytes);
                            char buf[256];
                            snprintf(buf, sizeof(buf),
                                "Files restored: %d\nSize: %s\nErrors: %d\n\n"
                                "Press any button.",
                                total_files, sz, total_errors);
                            draw_text_screen("Restore Completed", buf);
                            sceKernelDelayThread(2000000);
                            detail_running = 0;
                        }

                        if (pad.buttons & SCE_CTRL_SELECT) {
                            sceKernelDelayThread(150000);

                            if (!draw_confirm_screen("Confirm Delete",
                                "Are you sure you want to delete this backup?")) {
                                continue;
                            }

                            BackupInfo *b = &g_backups[mgr_selected];
                            char del_path[PATH_MAX_SIZE + 128];
                            snprintf(del_path, sizeof(del_path),
                                     "%s/_DELETED_%s",
                                     g_backup_root, b->timestamp);
                            sceIoRename(b->path, del_path);

                            char buf[256];
                            snprintf(buf, sizeof(buf),
                                "Backup moved to: _DELETED_%s\n\n"
                                "Press any button.", b->timestamp);
                            draw_text_screen("Backup Deleted", buf);
                            sceKernelDelayThread(1500000);

                            g_backup_count = list_backups(g_backups, MAX_BACKUPS);
                            if (mgr_selected >= g_backup_count)
                                mgr_selected = g_backup_count - 1;
                            if (mgr_selected < 0) mgr_selected = 0;
                            detail_running = 0;
                        }
                    }
                }
            }
            continue;
        }

        
        if (pad.buttons & SCE_CTRL_CROSS) {
            sceKernelDelayThread(150000);

            if (!draw_confirm_screen("Confirm Backup",
                "Are you sure you want to start the backup?")) {
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

            if (ftp_config.enabled) {
                net_init();
            }

            
            int done_choice = 0;
            while (done_choice == 0) {
                draw_backup_complete(&log);
                sceCtrlPeekBufferPositive(0, &pad, 1);
                sceKernelDelayThread(10000);

                if (pad.buttons & SCE_CTRL_START) {
                    sceKernelDelayThread(150000);

                    if (ftp_config.enabled) {
                        char ip_info[128];
                        net_get_local_ip(ip_info, sizeof(ip_info));
                        char ftp_msg[700];
                        snprintf(ftp_msg, sizeof(ftp_msg),
                                 "Connecting to:\n%s:%d -> %s\nLocal IP: %s\n\nPlease wait...",
                                 ftp_config.host, ftp_config.port,
                                 ftp_config.remote_dir, ip_info);
                        draw_text_screen("FTP Upload", ftp_msg);
                        sceKernelDelayThread(1000000);

                        int upload_ok = ftp_upload_backup(&ftp_config, backup_root, NULL);
                        int upload_done = 0;
                        while (!upload_done) {
                            const char *result_title = upload_ok ? "FTP Upload Complete" : "FTP Upload Failed";
                            const char *result_msg = upload_ok ?
                                "Upload completed successfully.\nPress any button to continue." :
                                "Could not connect to FTP server.\nCheck your settings and try again.\nPress any button to continue.";
                            draw_text_screen(result_title, result_msg);
                            sceCtrlPeekBufferPositive(0, &pad, 1);
                            sceKernelDelayThread(10000);
                            if (pad.buttons & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START)) {
                                upload_done = 1;
                                sceKernelDelayThread(150000);
                            }
                        }
                    }
                    done_choice = 1;
                }

                if (pad.buttons & SCE_CTRL_CIRCLE) {
                    done_choice = 1;
                    sceKernelDelayThread(150000);
                }
            }
            
            sceKernelDelayThread(100000);
        }
    }

    if (g_ftp_active) ftpvita_fini();
    net_term();
    ui_fini();
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}