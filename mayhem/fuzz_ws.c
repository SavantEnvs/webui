// Fuzzes WebUI's binary WebSocket protocol (sign, token, id, cmd, data): packet parsing, function
// calls with their argument framing, multi packet reassembly, events and client bookkeeping.
// Input: a sequence of [u16 little-endian length][packet bytes] frames sent over one fresh
// WebSocket connection to the in-process server. The window token is written over bytes 1..4 of
// every packet unless it starts with 0x00 (see below).
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>
#include "net.h"

extern size_t mayhem_start_server(const char* root_dir);
extern uint32_t mayhem_window_token(size_t window);
extern size_t mayhem_conns_closed_count(void);
extern void mayhem_wait_conn_done(size_t window, size_t closed_before);
extern size_t webui_get_port(size_t window);

static size_t g_window;
static int g_port;
static uint32_t g_token;

int LLVMFuzzerInitialize(int* argc, char*** argv) {
    (void)argc;
    (void)argv;
    g_window = mayhem_start_server(NULL);
    if (g_window == 0)
        return 1;
    g_port = (int)webui_get_port(g_window);
    g_token = mayhem_window_token(g_window);
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 2)
        return -1;
    size_t closed_before = mayhem_conns_closed_count();
    int fd = mayhem_tcp_connect(g_port);
    if (fd < 0)
        return -1;
    if (mayhem_ws_handshake(fd) != 0) {
        close(fd);
        return -1;
    }

    size_t pos = 0;
    uint8_t buf[65536];
    while (pos + 2 <= size) {
        size_t len = (size_t)data[pos] | ((size_t)data[pos + 1] << 8);
        pos += 2;
        if (len > size - pos)
            len = size - pos;
        if (len == 0)
            continue;
        memcpy(buf, data + pos, len);
        pos += len;
        // A leading 0x00 byte sends the chunk exactly as given; anything else is made to
        // carry the protocol signature and the window token, as a real bridge's packets do.
        if (len >= 5 && buf[0] != 0x00) {
            buf[0] = 0xDD;
            buf[1] = (uint8_t)g_token;
            buf[2] = (uint8_t)(g_token >> 8);
            buf[3] = (uint8_t)(g_token >> 16);
            buf[4] = (uint8_t)(g_token >> 24);
        }
        if (mayhem_ws_send(fd, buf, len) != 0)
            break;
    }
    mayhem_ws_close(fd);
    mayhem_wait_conn_done(g_window, closed_before);
    return 0;
}
