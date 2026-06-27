#ifndef BACKUP_H
#define BACKUP_H

#include <psp2/vshbridge.h>
#include "types.h"

extern BackupEntry *entries;
extern int ENTRY_COUNT;
extern int ENTRIES_CAPACITY;

void init_dynamic_arrays();
void cleanup_dynamic_arrays();
int expand_backups_array();

void format_size(char *out, int out_size, SceOff bytes);
void get_timestamp(char *out, int size);
void mount_all_partitions();
void remount(SceVshMountId id);
void create_dir(const char *path);
void build_backup_root(char *out, int size);
SceOff get_free_space(const char *path);
int entry_source_exists(const BackupEntry *entry);
void get_last_backup_summary(char *out, int out_size);
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
int load_config();

void apply_profile(ProfileType profile);
void cycle_profile();

int delete_logs(void);
int reset_config(void);

typedef struct {
    char path[PATH_MAX_SIZE];
    int only_in_a;
    int only_in_b;
    int different_size;
    SceOff size_a;
    SceOff size_b;
} FileDiff;

typedef struct {
    BackupInfo *backup_a;
    BackupInfo *backup_b;
    FileDiff *diffs;
    int diff_count;
    int total_files_a;
    int total_files_b;
    SceOff total_size_a;
    SceOff total_size_b;
} BackupComparison;

int compare_backups(const char *backup_path_a, const char *backup_path_b, BackupComparison *comp);
void free_backup_comparison(BackupComparison *comp);

int save_backup_state(BackupState *state);
int load_backup_state(BackupState *state);
int clear_backup_state(void);
int can_resume_backup(void);
int resume_backup(char *backup_root, int root_size, BackupLog *log);

#endif