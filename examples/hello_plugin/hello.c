/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Minimal CC2X plugin. Build: python sdk/build.py (-> sdk/dist/cc2x_hello.dll).
 * Install: put cc2x_hello.dll next to carrier_command.exe.
 *
 *   Lua (any screen script, e.g. a Workshop mod):
 *       if cc2x and cc2x.has("hello") then print(cc2x.hello.greet("pilot")) end
 */
#include <stdio.h>
#include "cc2x.h"

static const cc2x_api *api;
static int ticks;

static void on_tick(void *server, void *user) {
    ++ticks;
}

static void on_session(void *server, int started, void *user) {
    api->log("hello", CC2X_LOG_INFO, "session %s after %d ticks", started ? "started" : "ended", ticks);
    ticks = 0;
}

/* cc2x.hello.greet(name) -> "hello, <name>! server ticks: N" */
static int greet(cc2x_lua *L) {
    const char *name = api->lua_tostring(L, 1, NULL);
    char text[128];
    snprintf(text, sizeof(text), "hello, %s! server ticks: %d", name ? name : "?", ticks);
    api->lua_pushstring(L, text);
    return 1;
}

CC2X_EXPORT int cc2x_plugin_load(const cc2x_api *a) {
    if (a->api_version < CC2X_API_VERSION) return 0;
    api = a;
    api->on_server_tick(on_tick, NULL);
    api->on_session(on_session, NULL);
    api->lua_add("hello", "greet", greet);
    api->lua_feature("hello");
    return 1;
}
