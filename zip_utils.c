#include "zip_utils.h"
#include <zip.h>
#include <psp2/io/fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "backup.h"

static CompressionLevel g_compression_level = COMPRESS_NORMAL;

typedef struct {
    CopyContext *ctx;
    const char *entry_name;
    int current_idx;
    int total_idx;
    ProgressCallback cb;
} ZipProgressData;

int zip_set_compression_level(CompressionLevel level) {
    if (level < COMPRESS_NONE || level > COMPRESS_MAXIMUM) return -1;
    g_compression_level = level;
    return 0;
}

CompressionLevel zip_get_compression_level(void) {
    return g_compression_level;
}


static void _zip_prog_cb(zip_t *archive, double progress, void *udata) {
    ZipProgressData *data = (ZipProgressData *)udata;
    if (data && data->cb) {
       
        data->ctx->current_bytes = (SceOff)(progress * (double)data->ctx->total_bytes);
        data->cb(data->entry_name, data->current_idx, data->total_idx,
                 data->ctx->current_bytes, data->ctx->total_bytes,
                 data->ctx->current_file, data->ctx->total_files);
    }
}


static int _zip_cancel_cb(zip_t *archive, void *udata) {
    ZipProgressData *data = (ZipProgressData *)udata;
    return (data && data->ctx->cancel) ? 1 : 0;
}

static int add_to_zip(zip_t *archive, const char *base_path, const char *rel_path, 
                      CopyContext *ctx, ZipProgressData *pdata, CompressionLevel level) {
    char full_path[PATH_MAX_SIZE];
    snprintf(full_path, sizeof(full_path), "%s/%s", base_path, rel_path);

    SceUID dir = sceIoDopen(full_path);
    if (dir >= 0) {
        SceIoDirent ent;
        while (sceIoDread(dir, &ent) > 0) {
            if (!strcmp(ent.d_name, ".") || !strcmp(ent.d_name, "..")) continue;
            char next_rel[PATH_MAX_SIZE];
            snprintf(next_rel, sizeof(next_rel), "%s/%s", rel_path, ent.d_name);
            add_to_zip(archive, base_path, next_rel, ctx, pdata, level);
        }
        sceIoDclose(dir);
    } else {
        zip_source_t *s = zip_source_file(archive, full_path, 0, 0);
        if (s) {
            zip_int64_t idx = zip_file_add(archive, rel_path, s, ZIP_FL_OVERWRITE);
            if (idx >= 0) {
                if (level != COMPRESS_NONE) {
                    zip_set_file_compression(archive, idx, ZIP_CM_DEFLATE, level);
                } else {
                    zip_set_file_compression(archive, idx, ZIP_CM_STORE, 0);
                }
            }
            if (ctx) ctx->current_file++;
        }
    }
    return 0;
}

int zip_directory(const char *src_path, const char *zip_path, CopyContext *ctx,
                  const char *entry_name, int current_idx, int total_idx, 
                  ProgressCallback cb, CompressionLevel level) {
    int err = 0;
    zip_t *archive = zip_open(zip_path, ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!archive) return -1;

    ZipProgressData pdata = { ctx, entry_name, current_idx, total_idx, cb };

    
    zip_register_progress_callback_with_state(archive, 0.005, _zip_prog_cb, NULL, &pdata);
    zip_register_cancel_callback_with_state(archive, _zip_cancel_cb, NULL, &pdata);

    SceUID dir = sceIoDopen(src_path);
    if (dir >= 0) {
        SceIoDirent ent;
        while (sceIoDread(dir, &ent) > 0) {
            if (!strcmp(ent.d_name, ".") || !strcmp(ent.d_name, "..")) continue;
            add_to_zip(archive, src_path, ent.d_name, ctx, &pdata, level);
            
            if (ctx && ctx->cancel) break;
        }
        sceIoDclose(dir);
    }

    
    zip_close(archive);
    return 0;
}