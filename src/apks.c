#include "apks.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_apk_extension(const char *filename) {
    size_t len = strlen(filename);
    if (len < 4)
        return 0;
    const char *ext = filename + len - 4;
    return (tolower((unsigned char)ext[0]) == '.' &&
            tolower((unsigned char)ext[1]) == 'a' &&
            tolower((unsigned char)ext[2]) == 'p' &&
            tolower((unsigned char)ext[3]) == 'k');
}

static const char *zip_error(mz_zip_archive *zip) {
    return mz_zip_get_error_string(mz_zip_get_last_error(zip));
}

int apks_open(ApksFile *f, const char *path, char *err, size_t err_len) {
    memset(f, 0, sizeof *f);

    if (!mz_zip_reader_init_file(&f->zip, path, 0)) {
        snprintf(err, err_len, "cannot open %s: %s", path, zip_error(&f->zip));
        return 0;
    }

    mz_uint n = mz_zip_reader_get_num_files(&f->zip);
    f->entries = calloc(n ? n : 1, sizeof *f->entries);
    if (!f->entries) {
        snprintf(err, err_len, "out of memory");
        mz_zip_reader_end(&f->zip);
        return 0;
    }

    for (mz_uint i = 0; i < n; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&f->zip, i, &st))
            continue;
        if (mz_zip_reader_is_file_a_directory(&f->zip, i) ||
            !is_apk_extension(st.m_filename))
            continue;

        ApkEntry *e = &f->entries[f->count++];
        e->index = i;
        e->size = st.m_uncomp_size;
        snprintf(e->name, sizeof e->name, "%s", st.m_filename);
        f->total_size += e->size;
    }

    return 1;
}

typedef struct {
    ApkSink sink;
    void *opaque;
} SinkAdapter;

static size_t on_chunk(void *opaque, mz_uint64 file_ofs, const void *buf,
                       size_t n) {
    (void)file_ofs;
    SinkAdapter *a = opaque;
    /* return value != n is a write error. */
    return a->sink(a->opaque, buf, n) ? n : 0;
}

int apks_stream_entry(ApksFile *f, const ApkEntry *e, ApkSink sink,
                      void *opaque) {
    SinkAdapter a = {sink, opaque};
    return mz_zip_reader_extract_to_callback(&f->zip, e->index, on_chunk, &a, 0)
               ? 1
               : 0;
}

void apks_close(ApksFile *f) {
    free(f->entries);
    f->entries = NULL;
    f->count = 0;
    mz_zip_reader_end(&f->zip);
}
