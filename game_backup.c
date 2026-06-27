#include <psp2/types.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "game_backup.h"
#include "types.h"
#include "backup.h"
#include "ui.h"
#include <vita2d.h>

GameEntry *games = NULL;
int GAME_COUNT = 0;
int GAMES_CAPACITY = 0;

void init_games_array() {
    GAMES_CAPACITY = 100;
    games = (GameEntry *)malloc(GAMES_CAPACITY * sizeof(GameEntry));
    if (games) {
        GAME_COUNT = 0;
        memset(games, 0, GAMES_CAPACITY * sizeof(GameEntry));
    }
}

void cleanup_games_array() {
    if (games) {
        for (int i = 0; i < GAME_COUNT; i++) {
            if (games[i].icon) {
                vita2d_free_texture(games[i].icon);
                games[i].icon = NULL;
            }
        }
        free(games);
        games = NULL;
        GAME_COUNT = 0;
        GAMES_CAPACITY = 0;
    }
}

int expand_games_array() {
    int new_capacity = GAMES_CAPACITY * 2;
    GameEntry *new_games = (GameEntry *)realloc(games, new_capacity * sizeof(GameEntry));
    if (new_games) {
        games = new_games;
        memset(games + GAMES_CAPACITY, 0, (new_capacity - GAMES_CAPACITY) * sizeof(GameEntry));
        GAMES_CAPACITY = new_capacity;
        return 1;
    }
    return 0;
}

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t key_table_start;
    uint32_t value_table_start;
    uint32_t n_entries;
} SfoHeader;

typedef struct {
    uint16_t key_offset;
    uint16_t data_fmt;
    uint32_t data_len;
    uint32_t data_max_len;
    uint32_t data_offset;
} SfoEntry;

static int extract_title_from_sfo(const char *sfo_path, char *title, int title_size) {
    SceUID fd = sceIoOpen(sfo_path, SCE_O_RDONLY, 0);
    if (fd < 0) return -1;

    SfoHeader header;
    if (sceIoRead(fd, &header, sizeof(SfoHeader)) != sizeof(SfoHeader)) {
        sceIoClose(fd);
        return -1;
    }

    if (header.magic != 0x46535000) {
        sceIoClose(fd);
        return -1;
    }

    SfoEntry *entries = malloc(header.n_entries * sizeof(SfoEntry));
    if (!entries) {
        sceIoClose(fd);
        return -1;
    }
    sceIoRead(fd, entries, header.n_entries * sizeof(SfoEntry));

    int found = 0;
    for (uint32_t i = 0; i < header.n_entries; i++) {
        char key[64];
        sceIoLseek(fd, header.key_table_start + entries[i].key_offset, SCE_SEEK_SET);
        sceIoRead(fd, key, sizeof(key));
        
        if (strcmp(key, "TITLE") == 0) {
            sceIoLseek(fd, header.value_table_start + entries[i].data_offset, SCE_SEEK_SET);
            int to_read = (entries[i].data_len < title_size) ? entries[i].data_len : title_size - 1;
            sceIoRead(fd, title, to_read);
            title[to_read] = '\0';
            found = 1;
            break;
        }
    }

    free(entries);
    sceIoClose(fd);
    return found ? 0 : -1;
}

static int scan_app_directory() {
    const char *app_path = "ux0:app";
    SceUID dir = sceIoDopen(app_path);
    if (dir < 0) return 0;
    SceIoDirent ent;
    while (sceIoDread(dir, &ent) > 0) {
        if (strcmp(ent.d_name, ".") == 0 || strcmp(ent.d_name, "..") == 0) continue;
        if (!SCE_S_ISDIR(ent.d_stat.st_mode)) continue;
        if (strlen(ent.d_name) != 9) continue;
        
        if (GAME_COUNT >= GAMES_CAPACITY) {
            if (!expand_games_array()) break;
        }
        
        GameEntry *game = &games[GAME_COUNT];
        memset(game, 0, sizeof(GameEntry));
        strncpy(game->title_id, ent.d_name, sizeof(game->title_id) - 1);
        snprintf(game->app_path, PATH_MAX_SIZE, "%s/%s", app_path, ent.d_name);
        char sfo_path[PATH_MAX_SIZE];
        snprintf(sfo_path, sizeof(sfo_path), "%s/%s/sce_sys/param.sfo", app_path, ent.d_name);
        if (extract_title_from_sfo(sfo_path, game->name, sizeof(game->name)) < 0) {
            strncpy(game->name, ent.d_name, sizeof(game->name) - 1);
        }
        GAME_COUNT++;
    }
    sceIoDclose(dir);
    return GAME_COUNT;
}

static void scan_addcont_directory() {
    const char *addcont_path = "ux0:addcont";
    SceUID dir = sceIoDopen(addcont_path);
    if (dir < 0) return;
    SceIoDirent ent;
    while (sceIoDread(dir, &ent) > 0) {
        if (strcmp(ent.d_name, ".") == 0 || strcmp(ent.d_name, "..") == 0) continue;
        if (!SCE_S_ISDIR(ent.d_stat.st_mode)) continue;
        for (int i = 0; i < GAME_COUNT; i++) {
            if (strcmp(games[i].title_id, ent.d_name) == 0) {
                snprintf(games[i].addcont_path, PATH_MAX_SIZE, "%s/%s", addcont_path, ent.d_name);
                games[i].has_addcont = 1;
                break;
            }
        }
    }
    sceIoDclose(dir);
}

static void scan_patch_directory() {
    const char *patch_path = "ux0:patch";
    SceUID dir = sceIoDopen(patch_path);
    if (dir < 0) return;
    SceIoDirent ent;
    while (sceIoDread(dir, &ent) > 0) {
        if (strcmp(ent.d_name, ".") == 0 || strcmp(ent.d_name, "..") == 0) continue;
        if (!SCE_S_ISDIR(ent.d_stat.st_mode)) continue;
        for (int i = 0; i < GAME_COUNT; i++) {
            if (strcmp(games[i].title_id, ent.d_name) == 0) {
                snprintf(games[i].patch_path, PATH_MAX_SIZE, "%s/%s", patch_path, ent.d_name);
                games[i].has_patch = 1;
                break;
            }
        }
    }
    sceIoDclose(dir);
}

static void scan_game_data() {
    const char *savedata_path = "ux0:user/00/savedata";
    const char *trophy_path = "ux0:user/00/trophy";
    for (int i = 0; i < GAME_COUNT; i++) {
        char check_path[PATH_MAX_SIZE];
        snprintf(check_path, sizeof(check_path), "%s/%s", savedata_path, games[i].title_id);
        SceUID dir = sceIoDopen(check_path);
        if (dir >= 0) {
            snprintf(games[i].savedata_path, PATH_MAX_SIZE, "%s", check_path);
            games[i].has_savedata = 1;
            sceIoDclose(dir);
        }
        snprintf(check_path, sizeof(check_path), "%s/%s", trophy_path, games[i].title_id);
        dir = sceIoDopen(check_path);
        if (dir >= 0) {
            snprintf(games[i].trophy_path, PATH_MAX_SIZE, "%s", check_path);
            games[i].has_trophy = 1;
            sceIoDclose(dir);
        }
    }
}

int scan_games() {
    if (!games) {
        init_games_array();
    }
    if (!games) return 0;
    
    GAME_COUNT = 0;
    memset(games, 0, GAMES_CAPACITY * sizeof(GameEntry));
    scan_app_directory();
    scan_addcont_directory();
    scan_patch_directory();
    scan_game_data();
    return GAME_COUNT;
}

extern ProgressCallback g_progress_cb;
extern int g_prog_eidx;
extern int g_prog_total_entries;
extern const char *g_prog_entry_name;

int backup_game(int game_index, const char *backup_root, ProgressCallback cb) {
    if (game_index < 0 || game_index >= GAME_COUNT) return -1;
    GameEntry *game = &games[game_index];
    create_dir(backup_root);
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));
    char timestamp_dir[PATH_MAX_SIZE + 128];
    snprintf(timestamp_dir, sizeof(timestamp_dir), "%s/%s", backup_root, timestamp);
    create_dir(timestamp_dir);
    char game_backup_dir[PATH_MAX_SIZE + 256];
    snprintf(game_backup_dir, sizeof(game_backup_dir), "%s/%s", timestamp_dir, game->title_id);
    int total_files = 0;
    SceOff total_bytes = 0;
    if (strlen(game->app_path) > 0) count_files_recursive(game->app_path, &total_files, &total_bytes);
    if (game->has_addcont) count_files_recursive(game->addcont_path, &total_files, &total_bytes);
    if (game->has_patch) count_files_recursive(game->patch_path, &total_files, &total_bytes);
    if (game->has_savedata) count_files_recursive(game->savedata_path, &total_files, &total_bytes);
    if (game->has_trophy) count_files_recursive(game->trophy_path, &total_files, &total_bytes);
    if (total_files == 0) return -1;
    g_progress_cb = draw_backup_running;
    g_prog_total_entries = 1;
    g_prog_eidx = 1;
    g_prog_entry_name = game->name;
    sceIoMkdir(game_backup_dir, 0777);
    CopyContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.total_files = total_files;
    ctx.total_bytes = total_bytes;
    if (strlen(game->app_path) > 0) {
        char dst[PATH_MAX_SIZE + 512];
        snprintf(dst, sizeof(dst), "%s/app", game_backup_dir);
        sceIoMkdir(dst, 0777);
        copy_directory(game->app_path, dst, &ctx, NULL);
    }
    if (game->has_addcont && strlen(game->addcont_path) > 0) {
        char dst[PATH_MAX_SIZE + 512];
        snprintf(dst, sizeof(dst), "%s/addcont", game_backup_dir);
        sceIoMkdir(dst, 0777);
        copy_directory(game->addcont_path, dst, &ctx, NULL);
    }
    if (game->has_patch && strlen(game->patch_path) > 0) {
        char dst[PATH_MAX_SIZE + 512];
        snprintf(dst, sizeof(dst), "%s/patch", game_backup_dir);
        sceIoMkdir(dst, 0777);
        copy_directory(game->patch_path, dst, &ctx, NULL);
    }
    if (game->has_savedata && strlen(game->savedata_path) > 0) {
        char dst[PATH_MAX_SIZE + 512];
        snprintf(dst, sizeof(dst), "%s/savedata", game_backup_dir);
        sceIoMkdir(dst, 0777);
        copy_directory(game->savedata_path, dst, &ctx, NULL);
    }
    if (game->has_trophy && strlen(game->trophy_path) > 0) {
        char dst[PATH_MAX_SIZE + 512];
        snprintf(dst, sizeof(dst), "%s/trophy", game_backup_dir);
        sceIoMkdir(dst, 0777);
        copy_directory(game->trophy_path, dst, &ctx, NULL);
    }
    g_progress_cb = NULL;
    return ctx.has_error ? -1 : 0;
}
