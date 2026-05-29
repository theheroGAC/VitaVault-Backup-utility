#ifndef BACKUP_H
#define BACKUP_H

#include "types.h"

extern BackupEntry entries[];
extern int ENTRY_COUNT;

void format_size(char *out, int out_size, SceOff bytes);
void get_timestamp(char *out, int size);
void mount_all_partitions();
void create_dir(const char *path);
void build_backup_root(char *out, int size);
SceOff get_free_space(const char *path);
int check_unsafe_permissions();

int count_files_recursive(const char *path, int *file_count, SceOff *total_bytes);
int copy_file(const char *src, const char *dst, CopyContext *ctx, BackupLog *log);
int copy_directory(const char *src, const char *dst, CopyContext *ctx, BackupLog *log);
int restore_entry(const char *src, const char *dst, int *fr, SceOff *br, int *errs);

int do_backup(char *backup_root, int root_size, BackupLog *log);
int check_space_before_backup();
void cleanup_old_backups(int keep_count);

int list_backups(BackupInfo *backups, int max);
void restore_backup(BackupInfo *backup);
void delete_directory(const char *path);

void log_init(BackupLog *log);
void log_write_entry_header(BackupLog *log, int idx, int total, const char *name,
                            const char *src, const char *dst, int fc, SceOff tb);
void log_write_entry_result(BackupLog *log, int has_error);
void log_write(BackupLog *log, const char *text);
void log_close(BackupLog *log);

void save_config();
void load_config();

void apply_profile(ProfileType profile);
void cycle_profile();

#endif