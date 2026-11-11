// Fuzzes the HTTP side of WebUI's embedded server: request parsing, routing, the built-in
// pages (webui.js, the window page), file serving from a root folder and the WebSocket upgrade
// negotiation. Input: the raw bytes of one request, sent over a fresh loopback connection.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "net.h"
#include "server_root.h"

extern size_t mayhem_start_server(const char* root_dir);
extern size_t mayhem_conns_closed_count(void);
extern void mayhem_wait_conn_done(size_t window, size_t closed_before);
extern size_t webui_get_port(size_t window);

static size_t g_window;
static int g_port;

int LLVMFuzzerInitialize(int* argc, char*** argv) {
    (void)argc;
    (void)argv;
    g_window = mayhem_start_server(mayhem_server_root());
    if (g_window == 0)
        return 1;
    g_port = (int)webui_get_port(g_window);
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0)
        return -1;
    size_t closed_before = mayhem_conns_closed_count();
    int fd = mayhem_tcp_connect(g_port);
    if (fd < 0)
        return -1;
    mayhem_send_all(fd, data, size);
    shutdown(fd, SHUT_WR);
    static char response[1 << 16];
    (void)mayhem_read_all(fd, response, sizeof(response));
    close(fd);
    mayhem_wait_conn_done(g_window, closed_before);
    return 0;
}
