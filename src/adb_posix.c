#ifndef _WIN32

#include "adb.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ADB_SERVER_PORT 5037

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0 /* macOS: SIGPIPE is ignored in adb_init instead */
#endif

struct AdbConn {
    int fd;
};

static void set_err(char *err, size_t len, const char *msg) {
    if (err && len > 0)
        snprintf(err, len, "%s", msg);
}

int adb_init(void) {
    signal(SIGPIPE, SIG_IGN);
    return 1;
}

void adb_cleanup(void) {}

static int send_all(int fd, const void *buf, size_t len) {
    const char *p = buf;
    while (len > 0) {
        size_t chunk = len > 65536 ? 65536 : len;
        ssize_t n = send(fd, p, chunk, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return 0;
        p += n;
        len -= (size_t)n;
    }
    return 1;
}

static int recv_exact(int fd, void *buf, size_t len) {
    char *p = buf;
    while (len > 0) {
        ssize_t n = recv(fd, p, len, 0);
        if (n < 0 && errno == EINTR)
            continue;
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
static int send_request(int fd, const char *req, char *err, size_t err_len) {
    char hdr[5];
    size_t len = strlen(req);
    snprintf(hdr, sizeof hdr, "%04x", (unsigned)len);

    if (!send_all(fd, hdr, 4) || !send_all(fd, req, len)) {
        set_err(err, err_len, "failed to send request to adb server");
        return 0;
    }

    char status[4];
    if (!recv_exact(fd, status, 4)) {
        set_err(err, err_len, "adb server closed the connection");
        return 0;
    }

    if (memcmp(status, "OKAY", 4) == 0)
        return 1;

    if (memcmp(status, "FAIL", 4) == 0) {
        char lenhex[5] = {0};
        unsigned mlen = 0;
        if (recv_exact(fd, lenhex, 4))
            mlen = (unsigned)strtoul(lenhex, NULL, 16);

        if (err && err_len > 0) {
            err[0] = '\0';
            unsigned take = mlen < err_len - 1 ? mlen : (unsigned)(err_len - 1);
            if (take > 0 && recv_exact(fd, err, take))
                err[take] = '\0';
            else
                err[0] = '\0';
        }
        return 0;
    }

    set_err(err, err_len, "unexpected reply from adb server");
    return 0;
}

/* Linux only: looks for platform-tools/adb next to our own executable.
 * Elsewhere (macOS) adb is expected on PATH. */
static int find_bundled_adb(char *out, size_t cap) {
#ifdef __linux__
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0)
        return 0;
    exe[n] = '\0';

    char *slash = strrchr(exe, '/');
    if (!slash)
        return 0;
    *slash = '\0';

    snprintf(out, cap, "%s/platform-tools/adb", exe);
    return access(out, X_OK) == 0;
#else
    (void)out;
    (void)cap;
    return 0;
#endif
}

/* Runs `adb start-server` (bundled copy first, then PATH). 1 = success. */
static int start_adb_server(void) {
    char path[PATH_MAX + 32];
    int bundled = find_bundled_adb(path, sizeof path);

    pid_t pid = fork();
    if (pid < 0)
        return 0;

    if (pid == 0) {
        /* Silence adb's "daemon started" chatter. */
        if (!freopen("/dev/null", "w", stdout) ||
            !freopen("/dev/null", "w", stderr))
            _exit(126);
        if (bundled)
            execl(path, "adb", "start-server", (char *)NULL);
        else
            execlp("adb", "adb", "start-server", (char *)NULL);
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            return 0;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int connect_to_server(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ADB_SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

AdbConn *adb_open_service(const char *serial, const char *service, char *err,
                          size_t err_len) {
    int fd = connect_to_server();
    if (fd < 0 && start_adb_server())
        fd = connect_to_server();
    if (fd < 0) {
        set_err(err, err_len,
                "cannot reach the adb server and could not start it "
                "(install adb and make sure it is on PATH)");
        return NULL;
    }

    char transport[160];
    if (serial)
        snprintf(transport, sizeof transport, "host:transport:%s", serial);
    else
        snprintf(transport, sizeof transport, "host:transport-any");

    if (!send_request(fd, transport, err, err_len) ||
        !send_request(fd, service, err, err_len)) {
        close(fd);
        return NULL;
    }

    AdbConn *c = malloc(sizeof *c);
    if (!c) {
        set_err(err, err_len, "out of memory");
        close(fd);
        return NULL;
    }
    c->fd = fd;
    return c;
}

int adb_write(AdbConn *c, const void *buf, size_t len) {
    return send_all(c->fd, buf, len);
}

int adb_read_all(AdbConn *c, char *out, size_t cap) {
    size_t used = 0;
    char tmp[512];

    for (;;) {
        ssize_t n = recv(c->fd, tmp, sizeof tmp, 0);
        if (n < 0 && errno == EINTR)
            continue;
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
    close(c->fd);
    free(c);
}

#endif /* !_WIN32 */
