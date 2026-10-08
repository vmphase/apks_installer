#ifndef APKS_H
#define APKS_H

#include <stddef.h>
#include <stdint.h>

#include "../miniz/miniz.h"

typedef struct {
    mz_uint index;  /* entry index inside the .apks zip */
    char name[260]; /* path inside the zip  */
    uint64_t size;  /* uncompressed size in bytes */
} ApkEntry;

typedef struct {
    mz_zip_archive zip;
    ApkEntry *entries; /* only the embedded .apk files */
    size_t count;
    uint64_t total_size;
} ApksFile;

/* Receives decompressed chunks. Return 1 to continue, 0 to abort. */
typedef int (*ApkSink)(void *opaque, const void *buf, size_t n);

int apks_open(ApksFile *f, const char *path, char *err, size_t err_len);

/* Decompresses one embedded APK and feeds it to `sink` chunk by chunk. */
int apks_stream_entry(ApksFile *f, const ApkEntry *e, ApkSink sink,
                      void *opaque);

void apks_close(ApksFile *f);

#endif
