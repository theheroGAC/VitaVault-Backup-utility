#include "backup_logic.h"
#include "backup.h"
#include "ui.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static BackupProgress g_backup_progress;
static int g_backup_initialized = 0;

int backup_logic_init(void) {
    if (g_backup_initialized) return 0;
    
    memset(&g_backup_progress, 0, sizeof(BackupProgress));
    g_backup_progress.status = BACKUP_STATUS_IDLE;
    g_backup_initialized = 1;
    
    return 0;
}

void backup_logic_cleanup(void) {
    if (!g_backup_initialized) return;
    
    g_backup_initialized = 0;
    memset(&g_backup_progress, 0, sizeof(BackupProgress));
}

int backup_start(const char *backup_root, BackupProgress *progress) {
    if (!g_backup_initialized) return -1;
    
    g_backup_progress.status = BACKUP_STATUS_PREPARING;
    strncpy(g_backup_progress.current_entry_name, "Initializing...", 63);
    
    BackupLog log;
    char backup_root_copy[PATH_MAX_SIZE];
    strncpy(backup_root_copy, backup_root, PATH_MAX_SIZE - 1);
    backup_root_copy[PATH_MAX_SIZE - 1] = '\0';
    
    g_backup_progress.status = BACKUP_STATUS_RUNNING;
    int result = do_backup(backup_root_copy, PATH_MAX_SIZE, &log);
    
    if (result == 0) {
        g_backup_progress.status = BACKUP_STATUS_COMPLETED;
    } else if (result == -2) {
        g_backup_progress.status = BACKUP_STATUS_CANCELLED;
    } else {
        g_backup_progress.status = BACKUP_STATUS_ERROR;
        snprintf(g_backup_progress.error_message, sizeof(g_backup_progress.error_message),
                 "Backup failed with error code: %d", result);
    }
    
    if (progress) {
        memcpy(progress, &g_backup_progress, sizeof(BackupProgress));
    }
    
    return result;
}

int backup_pause(void) {
    if (!g_backup_initialized) return -1;
    
    if (g_backup_progress.status == BACKUP_STATUS_RUNNING) {
        g_backup_progress.status = BACKUP_STATUS_PAUSED;
        return 0;
    }
    
    return -1;
}

int backup_resume(void) {
    if (!g_backup_initialized) return -1;
    
    if (g_backup_progress.status == BACKUP_STATUS_PAUSED) {
        g_backup_progress.status = BACKUP_STATUS_RUNNING;
        return 0;
    }
    
    return -1;
}

int backup_cancel(void) {
    if (!g_backup_initialized) return -1;
    
    g_backup_progress.status = BACKUP_STATUS_CANCELLED;
    return 0;
}

int backup_get_progress(BackupProgress *progress) {
    if (!g_backup_initialized || !progress) return -1;
    
    memcpy(progress, &g_backup_progress, sizeof(BackupProgress));
    return 0;
}

int backup_get_status(void) {
    if (!g_backup_initialized) return BACKUP_STATUS_IDLE;
    
    return g_backup_progress.status;
}
