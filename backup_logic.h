#ifndef BACKUP_LOGIC_H
#define BACKUP_LOGIC_H

#include "types.h"

typedef enum {
    BACKUP_STATUS_IDLE,
    BACKUP_STATUS_PREPARING,
    BACKUP_STATUS_RUNNING,
    BACKUP_STATUS_PAUSED,
    BACKUP_STATUS_COMPLETED,
    BACKUP_STATUS_ERROR,
    BACKUP_STATUS_CANCELLED
} BackupStatus;

typedef struct {
    BackupStatus status;
    int current_entry;
    int total_entries;
    int current_file;
    int total_files;
    SceOff current_bytes;
    SceOff total_bytes;
    char current_entry_name[64];
    char error_message[256];
} BackupProgress;

int backup_logic_init(void);
void backup_logic_cleanup(void);

int backup_start(const char *backup_root, BackupProgress *progress);
int backup_pause(void);
int backup_resume(void);
int backup_cancel(void);

int backup_get_progress(BackupProgress *progress);
int backup_get_status(void);

#endif
