// The library under test, compiled into ONE sanitized object that every harness links.
// webui.c is included (not edited) so the harnesses can reach the few internals a loopback
// client cannot observe: the window's connection token and "the server has gone idle".
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>
#include "../src/civetweb/civetweb.h"

// The embedded server hands each connection to one civetweb worker thread, which runs every
// handler and only then closes the connection. Registering a connection_closed callback through
// this wrapper tells a harness when the server has completely finished with its connection
// (every event task it spawned is counted by then), instead of guessing with sleeps.
// Behaviour is otherwise untouched.
static atomic_size_t mayhem_conns_closed;

static void mayhem_conn_closed(const struct mg_connection* c) {
    (void)c;
    atomic_fetch_add(&mayhem_conns_closed, 1);
}

static struct mg_context* mayhem_mg_start(const struct mg_callbacks* cb, void* ud,
                                          const char** options) {
    struct mg_callbacks mine;
    memcpy(&mine, cb, sizeof(mine));
    mine.connection_closed = mayhem_conn_closed;
    return mg_start(&mine, ud, options);
}

#define mg_start mayhem_mg_start
#include "../src/webui.c"
#undef mg_start

#define MAYHEM_SERVER_HTML "<html><head><script src=\"webui.js\"></script></head><body>mayhem</body></html>"

// User callbacks bound by the harness, the way a real embedder uses the API. They only read
// what the client sent and answer, so each input's outcome depends on that input alone.
static void mayhem_echo_cb(webui_event_t* e) {
    size_t n = webui_get_count(e);
    char out[256];
    size_t used = 0;
    for (size_t i = 0; i < n && i < 8; i++) {
        size_t len = webui_get_size_at(e, i);
        const char* s = webui_get_string_at(e, i);
        long long v = webui_get_int_at(e, i);
        bool b = webui_get_bool_at(e, i);
        double f = webui_get_float_at(e, i);
        unsigned sum = 0;
        for (size_t j = 0; s != NULL && j < len; j++)
            sum += (unsigned char)s[j];
        int w = snprintf(out + used, sizeof(out) - used, "%zu:%zu:%lld:%d:%d:%u;", i, len,
                         v, b, (int)(f > 0), sum);
        if (w < 0 || (size_t)w >= sizeof(out) - used)
            break;
        used += (size_t)w;
    }
    out[used] = 0;
    webui_return_string(e, out);
}

static void mayhem_all_events_cb(webui_event_t* e) {
    (void)webui_get_size(e);
    (void)webui_get_string(e);
    (void)webui_get_context(e);
}

size_t mayhem_start_server(const char* root_dir) {
    webui_set_config(show_wait_connection, false);
    webui_set_config(multi_client, true);
    webui_set_config(use_cookies, false);
    size_t win = webui_new_window();
    webui_bind(win, "mayhem_echo", mayhem_echo_cb);
    webui_bind(win, "", mayhem_all_events_cb);
    if (root_dir != NULL)
        webui_set_root_folder(win, root_dir);
    if (!webui_show_browser(win, MAYHEM_SERVER_HTML, NoBrowser))
        return 0;
    // The token is normally drawn when the first client requests webui.js; fix it up front so
    // websocket packets can be built before any HTTP request and results do not depend on order.
    _webui_window_t* w = _webui_dereference_win_num(win);
    if (w != NULL && w->token == 0)
        w->token = 0x3c5a9e17;
    return win;
}

uint32_t mayhem_window_token(size_t window) {
    _webui_window_t* win = _webui_dereference_win_num(window);
    return win != NULL ? win->token : 0;
}

size_t mayhem_conns_closed_count(void) { return atomic_load(&mayhem_conns_closed); }

// Block until the server finished the connection that followed `closed_before` (a value of
// mayhem_conns_closed_count()) and every event task of the window.
void mayhem_wait_conn_done(size_t window, size_t closed_before) {
    while (atomic_load(&mayhem_conns_closed) <= closed_before)
        usleep(50);
    _webui_window_t* win = _webui_dereference_win_num(window);
    if (win != NULL)
        _webui_window_wait_for_tasks(win);
}
