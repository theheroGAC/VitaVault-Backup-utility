#ifndef UI_H
#define UI_H

#include "types.h"

int init_font();
int text_width(const char *text);
int text_width_at(const char *text, float size);

void draw_panel(int x, int y, int w, int h, unsigned int color);
void draw_text(int x, int y, unsigned int color, float size, const char *text);
void draw_progress_bar_gui(int x, int y, int w, int h, int pct);
void draw_checkbox(int x, int y, int checked);
void draw_scrollbar(int total, int visible, int selected, int x, int y, int h);
void ui_set_notification(const char *msg);
void draw_text_screen(const char *title, const char *text);
int draw_confirm_screen(const char *title, const char *message);

void draw_main_menu(int selected);
void draw_space_check(int space_status, SceOff needed, SceOff free_space);
void draw_backup_running(const char *entry_name, int current, int total,
                          SceOff cbytes, SceOff tbytes, int cur_file, int total_files);
void draw_backup_complete(const BackupLog *log);
void draw_manage_list(int selected, int count);
void draw_backup_details(const BackupInfo *b);
void draw_restore_progress(const char *entry, int done, int errors);
void draw_ftp_upload(int done_files, int total_files, SceOff done_bytes, SceOff total_bytes);
void draw_ftp_config();
void draw_file_browser(const char *current_path, char names[][256], int count, int selected);

void ui_fini();

#endif