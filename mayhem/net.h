// Minimal loopback TCP / WebSocket client shared by the harnesses.
#ifndef MAYHEM_NET_H
#define MAYHEM_NET_H
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int mayhem_tcp_connect(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr*)&a, sizeof(a)) != 0) {
        close(fd);
        return -1;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    return fd;
}

static int mayhem_send_all(int fd, const void* buf, size_t len) {
    const char* p = (const char*)buf;
    while (len > 0) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n <= 0)
            return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

// Reads until EOF or until `cap` bytes; returns the byte count.
static size_t mayhem_read_all(int fd, char* out, size_t cap) {
    size_t used = 0;
    for (;;) {
        char tmp[4096];
        ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0)
            break;
        if (used < cap) {
            size_t c = (size_t)n < cap - used ? (size_t)n : cap - used;
            memcpy(out + used, tmp, c);
            used += c;
        }
    }
    return used;
}

// WebSocket upgrade; returns 0 once the server answered "101".
static int mayhem_ws_handshake(int fd) {
    static const char req[] =
        "GET /_webui_ws_connect HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\n"
        "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";
    if (mayhem_send_all(fd, req, sizeof(req) - 1) != 0)
        return -1;
    char resp[512];
    size_t used = 0;
    while (used < sizeof(resp) - 1) {
        ssize_t n = recv(fd, resp + used, 1, 0);
        if (n <= 0)
            return -1;
        used++;
        if (used >= 4 && memcmp(resp + used - 4, "\r\n\r\n", 4) == 0)
            break;
    }
    resp[used] = 0;
    return strstr(resp, " 101 ") != NULL ? 0 : -1;
}

// One masked binary frame (len <= 65535).
static int mayhem_ws_send(int fd, const uint8_t* data, size_t len) {
    uint8_t hdr[8];
    size_t h = 0;
    hdr[h++] = 0x82;
    if (len < 126) {
        hdr[h++] = (uint8_t)(0x80 | len);
    } else {
        hdr[h++] = 0x80 | 126;
        hdr[h++] = (uint8_t)(len >> 8);
        hdr[h++] = (uint8_t)len;
    }
    // A zero mask key keeps the payload bytes as they are.
    memset(hdr + h, 0, 4);
    h += 4;
    if (mayhem_send_all(fd, hdr, h) != 0)
        return -1;
    return mayhem_send_all(fd, data, len);
}

// Reads one unmasked server frame into `out`; returns its payload length, or -1 on error.
static ssize_t mayhem_ws_recv(int fd, uint8_t* out, size_t cap) {
    uint8_t h[2];
    size_t got = 0;
    while (got < 2) {
        ssize_t n = recv(fd, h + got, 2 - got, 0);
        if (n <= 0)
            return -1;
        got += (size_t)n;
    }
    size_t len = h[1] & 0x7f;
    if (len == 126) {
        uint8_t e[2];
        for (got = 0; got < 2;) {
            ssize_t n = recv(fd, e + got, 2 - got, 0);
            if (n <= 0)
                return -1;
            got += (size_t)n;
        }
        len = ((size_t)e[0] << 8) | e[1];
    } else if (len == 127) {
        return -1;
    }
    if (len > cap)
        return -1;
    for (got = 0; got < len;) {
        ssize_t n = recv(fd, out + got, len - got, 0);
        if (n <= 0)
            return -1;
        got += (size_t)n;
    }
    return (ssize_t)len;
}

// Orderly close frame, so the server leaves its read loop at once.
static void mayhem_ws_close(int fd) {
    static const uint8_t frame[6] = {0x88, 0x80, 0, 0, 0, 0};
    mayhem_send_all(fd, frame, sizeof(frame));
    close(fd);
}
#endif
