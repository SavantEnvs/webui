// Behavioral self-test of the WebUI server: starts the real server in-process (normal flags, no
// sanitizers) and checks what a browser bridge would see over HTTP and over the binary WebSocket
// protocol. Prints one "PASS: <name>" or "FAIL: <name>" line per assertion for mayhem/test.sh.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "net.h"
#include "server_root.h"

extern size_t mayhem_start_server(const char* root_dir);
extern uint32_t mayhem_window_token(size_t window);
extern size_t webui_get_port(size_t window);

static int g_port;

static void check(int ok, const char* name) { printf("%s: %s\n", ok ? "PASS" : "FAIL", name); }

static size_t http_get(const char* path, char* out, size_t cap) {
    int fd = mayhem_tcp_connect(g_port);
    if (fd < 0)
        return 0;
    char req[512];
    snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n", path);
    mayhem_send_all(fd, req, strlen(req));
    size_t n = mayhem_read_all(fd, out, cap - 1);
    out[n] = 0;
    close(fd);
    return n;
}

static const char* body_of(const char* resp) {
    const char* b = strstr(resp, "\r\n\r\n");
    return b != NULL ? b + 4 : "";
}

// Builds [sign][token][id][cmd][data...]; returns the packet length.
static size_t packet(uint8_t* p, uint32_t token, uint16_t id, uint8_t cmd, const uint8_t* data,
                     size_t len) {
    p[0] = 0xDD;
    memcpy(p + 1, &token, 4);
    p[5] = (uint8_t)id;
    p[6] = (uint8_t)(id >> 8);
    p[7] = cmd;
    if (len > 0)
        memcpy(p + 8, data, len);
    return 8 + len;
}

int main(void) {
    size_t win = mayhem_start_server(mayhem_server_root());
    check(win != 0, "server starts");
    if (win == 0)
        return 1;
    g_port = (int)webui_get_port(win);
    check(g_port > 0, "server listens on a loopback port");

    static char resp[65536];
    http_get("/", resp, sizeof(resp));
    check(strstr(resp, "HTTP/1.1 200") != NULL && strstr(body_of(resp), "mayhem</body>") != NULL,
          "GET / serves the window html");

    http_get("/webui.js", resp, sizeof(resp));
    check(strstr(resp, "HTTP/1.1 200") != NULL && strstr(body_of(resp), "WebSocket") != NULL,
          "GET /webui.js serves the bridge script");

    http_get("/test.txt", resp, sizeof(resp));
    check(strstr(resp, "HTTP/1.1 200") != NULL && strcmp(body_of(resp), "plain text file\n") == 0,
          "GET /test.txt serves the file from the root folder");

    http_get("/data.json", resp, sizeof(resp));
    check(strstr(resp, "application/json") != NULL, "GET /data.json has a json content type");

    http_get("/does-not-exist.txt", resp, sizeof(resp));
    check(strstr(resp, "HTTP/1.1 404") != NULL, "GET of a missing file is 404");

    uint32_t token = mayhem_window_token(win);
    check(token != 0, "window has a connection token");

    int fd = mayhem_tcp_connect(g_port);
    check(fd >= 0 && mayhem_ws_handshake(fd) == 0, "websocket upgrade is accepted");
    if (fd < 0)
        return 1;

    uint8_t pkt[256], rep[4096];
    size_t n = packet(pkt, token, 1, 0xF5, NULL, 0);
    mayhem_ws_send(fd, pkt, n);
    ssize_t r = mayhem_ws_recv(fd, rep, sizeof(rep) - 1);
    if (r > 8)
        rep[r] = 0;
    check(r > 8 && rep[0] == 0xDD && rep[7] == 0xF5 && rep[8] == 0x01 &&
              strstr((char*)rep + 9, "mayhem_echo,") != NULL,
          "CHECK_TK accepts the token and answers with the bind list");

    const uint8_t call[] = "mayhem_echo\0" "5;3\0" "hello\0" "123";
    n = packet(pkt, token, 2, 0xF9, call, sizeof(call));
    mayhem_ws_send(fd, pkt, n);
    r = mayhem_ws_recv(fd, rep, sizeof(rep) - 1);
    if (r > 8)
        rep[r] = 0;
    // "hello" is 5 bytes summing to 532; "123" is 3 bytes, integer 123, true, positive float, summing to 150.
    check(r > 8 && rep[7] == 0xF9 && rep[5] == 2 && rep[6] == 0 &&
              strstr((char*)rep + 8, "0:5:0:1:0:532;1:3:123:1:1:150;") != NULL,
          "CALL_FUNC runs the bound function with its arguments and returns its result");

    const uint8_t call2[] = "mayhem_echo\0" "2\0" "-7";
    n = packet(pkt, token, 3, 0xF9, call2, sizeof(call2));
    mayhem_ws_send(fd, pkt, n);
    r = mayhem_ws_recv(fd, rep, sizeof(rep) - 1);
    if (r > 8)
        rep[r] = 0;
    check(r > 8 && rep[7] == 0xF9 && strstr((char*)rep + 8, "0:2:-7:1:0:100;") != NULL,
          "CALL_FUNC parses a negative integer argument");

    mayhem_ws_close(fd);
    return 0;
}
