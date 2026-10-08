#ifndef ADB_H
#define ADB_H

#include <stddef.h>

/* A connection to one ADB service */
typedef struct AdbConn AdbConn;

int adb_init(void);
void adb_cleanup(void);

/*
 * Connects to the local ADB server (127.0.0.1:5037), selects the device
 * (serial == NULL means "any single device") and starts `service`.
 * On failure returns NULL and writes a message to err.
 */
AdbConn *adb_open_service(const char *serial, const char *service, char *err,
                          size_t err_len);

/* Sends raw bytes to the service (stdin of the remote command). 1 = ok. */
int adb_write(AdbConn *c, const void *buf, size_t len);

/*
 * Reads the service output until the remote side closes the connection.
 * Output is NUL-terminated and trailing whitespace is trimmed.
 * Returns the length, or -1 on error.
 */
int adb_read_all(AdbConn *c, char *out, size_t cap);

void adb_close(AdbConn *c);

#endif
