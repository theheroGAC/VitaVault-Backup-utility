#ifndef ZIP_UTILS_H
#define ZIP_UTILS_H

#include <zip.h>
#include "types.h"

typedef enum {
    COMPRESS_NONE = 0,
    COMPRESS_FAST = 1,
    COMPRESS_NORMAL = 6,
    COMPRESS_MAXIMUM = 9
} CompressionLevel;

int zip_directory(const char *src_path, const char *zip_path, CopyContext *ctx,
                  const char *entry_name, int current_idx, int total_idx,
                  ProgressCallback cb, CompressionLevel level);

int zip_set_compression_level(CompressionLevel level);
CompressionLevel zip_get_compression_level(void);

#endif