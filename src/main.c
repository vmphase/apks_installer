#include <stdio.h>

#include "adb.h"
#include "apks.h"
#include "install.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.apks> [device_serial]\n", argv[0]);
        return 1;
    }

    const char *path = argv[1];
    const char *serial = argc > 2 ? argv[2] : NULL;

    char err[256];
    ApksFile apks;
    if (!apks_open(&apks, path, err, sizeof err)) {
        fprintf(stderr, "Error: %s\n", err);
        return 1;
    }

    printf("Opened %s: %zu APKs, %llu bytes total\n", path, apks.count,
           (unsigned long long)apks.total_size);

    if (!adb_init()) {
        fprintf(stderr, "Error: could not initialise Winsock\n");
        apks_close(&apks);
        return 1;
    }

    int ok = install_apks(&apks, serial);

    adb_cleanup();
    apks_close(&apks);
    return ok ? 0 : 1;
}
