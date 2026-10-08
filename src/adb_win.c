#ifdef _WIN32

#include "adb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#define ADB_SERVER_PORT 5037

struct AdbConn {
    SOCKET sock;
};

static void set_err(char *err, size_t len, const char *msg) {
    if (err && len > 0)
        snprintf(err, len, "%s", msg);
}

int adb_init(void) {
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
}

void adb_cleanup(void) { WSACleanup(); }

static int send_all(SOCKET s, const void *buf, size_t len) {
    const char *p = buf;
    while (len > 0) {
        int chunk = len > 65536 ? 65536 : (int)len;
        int n = send(s, p, chunk, 0);
        if (n <= 0)
            return 0;
        p += n;
        len -= (size_t)n;
    }
    return 1;
}

static int recv_exact(SOCKET s, void *buf, size_t len) {
    char *p = buf;
    while (len > 0) {
        int n = recv(s, p, (int)len, 0);
        if (n <= 0)
            return 0;
        p += n;
        len -= (size_t)n;
    }
    return 1;
}

/*
 * ADB smart-socket request: 4 hex digits (payload length) + payload.
 * Reply: "OKAY", or "FAIL" + 4 hex digits + message.
 */
static int send_request(SOCKET s, const char *req, char *err, size_t err_len) {
    char hdr[5];
    size_t len = strlen(req);
    snprintf(hdr, sizeof hdr, "%04x", (unsigned)len);

    if (!send_all(s, hdr, 4) || !send_all(s, req, len)) {
        set_err(err, err_len, "failed to send request to adb server");
        return 0;
    }

    char status[4];
    if (!recv_exact(s, status, 4)) {
        set_err(err, err_len, "adb server closed the connection");
        return 0;
    }

    if (memcmp(status, "OKAY", 4) == 0)
        return 1;

    if (memcmp(status, "FAIL", 4) == 0) {
        char lenhex[5] = {0};
        unsigned mlen = 0;
        if (recv_exact(s, lenhex, 4))
            mlen = (unsigned)strtoul(lenhex, NULL, 16);

        if (err && err_len > 0) {
            err[0] = '\0';
            unsigned take = mlen < err_len - 1 ? mlen : (unsigned)(err_len - 1);
            if (take > 0 && recv_exact(s, err, take))
                err[take] = '\0';
            else
                err[0] = '\0';
        }
        return 0;
    }

    set_err(err, err_len, "unexpected reply from adb server");
    return 0;
}

/* Looks for platform-tools\adb.exe next to the apk installer exe. */
static int find_bundled_adb(char *out, size_t cap) {
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof exe);
    if (n == 0 || n >= sizeof exe)
        return 0;

    char *slash = strrchr(exe, '\\');
    if (slash)
        *slash = '\0';

    snprintf(out, cap, "%s\\platform-tools\\adb.exe", exe);
    return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;
}

/* Runs `adb start-server` (bundled copy first, then PATH). 1 = success. */
static int start_adb_server(void) {
    char path[MAX_PATH + 32];
    char cmd[MAX_PATH + 64];

    if (find_bundled_adb(path, sizeof path))
        snprintf(cmd, sizeof cmd, "\"%s\" start-server", path);
    else
        snprintf(cmd, sizeof cmd, "adb start-server");

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;

    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL,
                        NULL, &si, &pi))
        return 0;

    WaitForSingleObject(pi.hProcess, 15000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0;
}

static SOCKET connect_to_server(void) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return INVALID_SOCKET;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ADB_SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(s, (struct sockaddr *)&addr, sizeof addr) == SOCKET_ERROR) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

AdbConn *adb_open_service(const char *serial, const char *service, char *err,
                          size_t err_len) {
    SOCKET s = connect_to_server();
    if (s == INVALID_SOCKET && start_adb_server())
        s = connect_to_server();
    if (s == INVALID_SOCKET) {
        set_err(err, err_len,
                "cannot reach the adb server and could not start it "
                "(put adb.exe in platform-tools\\ next to this exe, or on "
                "PATH)");
        return NULL;
    }

    char transport[160];
    if (serial)
        snprintf(transport, sizeof transport, "host:transport:%s", serial);
    else
        snprintf(transport, sizeof transport, "host:transport-any");

    if (!send_request(s, transport, err, err_len) ||
        !send_request(s, service, err, err_len)) {
        closesocket(s);
        return NULL;
    }

    AdbConn *c = malloc(sizeof *c);
    if (!c) {
        set_err(err, err_len, "out of memory");
        closesocket(s);
        return NULL;
    }
    c->sock = s;
    return c;
}

int adb_write(AdbConn *c, const void *buf, size_t len) {
    return send_all(c->sock, buf, len);
}

int adb_read_all(AdbConn *c, char *out, size_t cap) {
    size_t used = 0;
    char tmp[512];

    for (;;) {
        int n = recv(c->sock, tmp, sizeof tmp, 0);
        if (n < 0)
            return -1;
        if (n == 0)
            break;

        size_t room = cap - 1 - used;
        size_t take = (size_t)n < room ? (size_t)n : room;
        memcpy(out + used, tmp, take);
        used += take;
    }

    out[used] = '\0';
    while (used > 0 && (out[used - 1] == '\n' || out[used - 1] == '\r' ||
                        out[used - 1] == ' '))
        out[--used] = '\0';

    return (int)used;
}

void adb_close(AdbConn *c) {
    if (!c)
        return;
    closesocket(c->sock);
    free(c);
}

#endif /* _WIN32 */
