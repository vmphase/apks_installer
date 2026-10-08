#include "install.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adb.h"

/* Runs a one-shot service and captures its output. */
static int run_command(const char *serial, const char *service, char *out,
                       size_t cap) {
    char err[256];
    AdbConn *c = adb_open_service(serial, service, err, sizeof err);
    if (!c) {
        snprintf(out, cap, "%s", err);
        return 0;
    }

    int n = adb_read_all(c, out, cap);
    adb_close(c);

    if (n < 0) {
        snprintf(out, cap, "connection error while reading reply");
        return 0;
    }
    return strncmp(out, "Success", 7) == 0;
}

/* "Success: created install session [1234567]" -> 1234567 */
static int parse_session_id(const char *reply) {
    const char *p = strchr(reply, '[');
    if (!p)
        return -1;
    char *end;
    long id = strtol(p + 1, &end, 10);
    if (end == p + 1 || *end != ']')
        return -1;
    return (int)id;
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int sink_to_adb(void *opaque, const void *buf, size_t n) {
    return adb_write((AdbConn *)opaque, buf, n);
}

static void abandon_session(const char *serial, int session) {
    char service[128], out[256];
    snprintf(service, sizeof service, "exec:cmd package install-abandon %d",
             session);
    run_command(serial, service, out, sizeof out);
}

static int write_entry(ApksFile *f, const char *serial, int session, size_t idx,
                       const ApkEntry *e) {
    char service[512], err[256], out[512];

    /* -S amount of bytes to read from stdin */
    snprintf(service, sizeof service,
             "exec:cmd package install-write -S %llu %d %zu_%s -",
             (unsigned long long)e->size, session, idx, base_name(e->name));

    AdbConn *c = adb_open_service(serial, service, err, sizeof err);
    if (!c) {
        fprintf(stderr, "    could not start install-write: %s\n", err);
        return 0;
    }

    int ok = apks_stream_entry(f, e, sink_to_adb, c);
    if (!ok) {
        fprintf(stderr, "    streaming failed\n");
    } else {
        int n = adb_read_all(c, out, sizeof out);
        ok = n >= 0 && strncmp(out, "Success", 7) == 0;
        if (!ok)
            fprintf(stderr, "    device said: %s\n",
                    n >= 0 ? out : "(connection error)");
    }

    adb_close(c);
    return ok;
}

int install_apks(ApksFile *f, const char *serial) {
    char service[256], out[512];

    if (f->count == 0) {
        fprintf(stderr, "No .apk entries found in the container.\n");
        return 0;
    }

    snprintf(service, sizeof service,
             "exec:cmd package install-create -r -S %llu",
             (unsigned long long)f->total_size);
    if (!run_command(serial, service, out, sizeof out)) {
        fprintf(stderr, "install-create failed: %s\n", out);
        return 0;
    }

    int session = parse_session_id(out);
    if (session < 0) {
        fprintf(stderr, "could not parse session id from: %s\n", out);
        return 0;
    }
    printf("Created install session %d\n", session);

    /* stream every split into the session */
    for (size_t i = 0; i < f->count; i++) {
        const ApkEntry *e = &f->entries[i];
        printf("[%zu/%zu] %s (%llu bytes)\n", i + 1, f->count, e->name,
               (unsigned long long)e->size);

        if (!write_entry(f, serial, session, i, e)) {
            abandon_session(serial, session);
            return 0;
        }
    }

    snprintf(service, sizeof service, "exec:cmd package install-commit %d",
             session);
    if (!run_command(serial, service, out, sizeof out)) {
        fprintf(stderr, "install-commit failed: %s\n", out);
        abandon_session(serial, session);
        return 0;
    }

    printf("Installed successfully.\n");
    return 1;
}
