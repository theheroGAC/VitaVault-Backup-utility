#ifndef TYPES_H
#define TYPES_H

#include <psp2/kernel/processmgr.h>
#include <psp2/io/dirent.h>
#include <stdint.h>

#define COLOR_BG_MAIN       RGBA8(25,  25,  50,  255)
#define COLOR_BG_PANEL      RGBA8(35,  35,  55,  255)
#define COLOR_BG_HEADER     RGBA8(60,  60,  120, 255)
#define COLOR_BG_SELECTED   RGBA8(70,  90,  160, 255)
#define COLOR_ACCENT        RGBA8(100, 180, 255, 255)
#define COLOR_TEXT_MAIN     RGBA8(220, 220, 240, 255)
#define COLOR_TEXT_DIM      RGBA8(140, 140, 160, 255)
#define COLOR_TEXT_BRIGHT   RGBA8(255, 255, 255, 255)
#define COLOR_GREEN         RGBA8(80,  200, 80,  255)
#define COLOR_RED           RGBA8(200, 60,  60,  255)
#define COLOR_YELLOW        RGBA8(220, 200, 50,  255)
#define COLOR_ORANGE        RGBA8(220, 150, 30,  255)
#define COLOR_BAR_BG        RGBA8(40,  40,  60,  255)
#define COLOR_BAR_FILL      RGBA8(80,  160, 220, 255)
#define COLOR_BORDER        RGBA8(80,  80,  120, 255)

#define CONFIG_PATH     "ux0:data/VitaVault/config.cfg"
#define PATH_MAX_SIZE   1024
#define MAX_BACKUPS     50
#define MAX_ENTRIES     20
#define MAX_LINE        512

#define FTP_DEFAULT_HOST "192.168.1.100"
#define FTP_DEFAULT_PORT 21
#define FTP_DEFAULT_USER "anonymous"
#define FTP_DEFAULT_PASS "vita@ftp"
#define FTP_DEFAULT_DIR  "/VitaVault"

typedef struct BackupEntry {
    char name[64];
    char source[PATH_MAX_SIZE];
    int enabled;
} BackupEntry;

typedef void (*ProgressCallback)(const char *entry_name, int current_entry,
                                  int total_entries, SceOff cbytes,
                                  SceOff tbytes, int cur_file, int total_files);

typedef struct CopyContext {
    int current_file;
    int total_files;
    SceOff current_bytes;
    SceOff total_bytes;
    int has_error;
    int cancel;
} CopyContext;

typedef struct BackupLog {
    char path[PATH_MAX_SIZE + 128];
    SceUID fd;
    int total_entries;
    int total_files;
    SceOff total_bytes;
    int errors;
    char start_time[64];
} BackupLog;

typedef struct {
    char path[PATH_MAX_SIZE + 128];
    char timestamp[64];
    SceOff total_size;
    int total_entries;
    int entry_count;
} BackupInfo;

typedef struct {
    char host[256];
    int port;
    char user[64];
    char pass[64];
    char remote_dir[256];
    int enabled;
} FTPConfig;



typedef enum {
    PROFILE_NONE,      
    PROFILE_MINIMAL,   
    PROFILE_NORMAL,    
    PROFILE_COMPLETE   
} ProfileType;

extern const char *profile_names[];



extern BackupEntry entries[];
extern char g_backup_root[PATH_MAX_SIZE];
extern int ENTRY_COUNT;
extern ProfileType current_profile;
extern FTPConfig ftp_config;
extern BackupInfo g_backups[MAX_BACKUPS];
extern int g_backup_count;
extern char g_last_backup_path[PATH_MAX_SIZE + 128];
extern char g_last_log_path[PATH_MAX_SIZE + 128];

#endif