/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * CC2X self-test plugin (cc2x_selftest.dll): checks the world API (API 3) in a running game and
 * writes PASS / FAIL lines to cc2x.log. It only reads and observes; the game stays as it is.
 *
 * Use: put cc2x_selftest.dll next to carrier_command.exe, load a save, wait ~20 s, read cc2x.log.
 * The event counters are logged every minute.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "cc2x.h"

static const cc2x_api *A;
static int g_pass, g_fail;
static unsigned g_tick, g_done;
static volatile long g_ev_wp, g_ev_af, g_ev_hangar, g_ev_ctrl, g_ev_spawn;

#define LOG(...) A->log("selftest", CC2X_LOG_INFO, __VA_ARGS__)
static void check(int ok, const char *name, const char *fmt, ...) {
    char why[256] = "";
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(why, sizeof(why), fmt, ap);
        va_end(ap);
    }
    if (ok) ++g_pass; else ++g_fail;
    A->log("selftest", ok ? CC2X_LOG_INFO : CC2X_LOG_ERROR, "%s %s%s%s", ok ? "PASS" : "FAIL", name, why[0] ? ": " : "", why);
}

static double dist3(const double *a, const double *b) {
    return sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

static void test_vehicles(void) {
    int n = A->count(NULL, CC2X_WORLD_VEHICLES), lookups = 0, bad = 0, snap = 0, snap_bad = 0, bodies = 0, body_bad = 0;
    check(n > 0, "world vehicles", "%d", n);
    for (int i = 0; i < n; ++i) {
        void *v = A->ref(NULL, CC2X_WORLD_VEHICLES, i);
        uint32_t id = (uint32_t)A->geti(v, CC2X_VEHICLE_ID, 0);
        ++lookups;
        if (!v || A->find(CC2X_WORLD_VEHICLES, id) != v) ++bad;
        cc2x_vehicle_info info;
        if (A->vehicle(id, &info)) {              /* the snapshot reads the same memory its own way */
            ++snap;
            if (info.team != (uint32_t)A->geti(v, CC2X_VEHICLE_TEAM, 0) ||
                info.definition != (uint32_t)A->geti(v, CC2X_VEHICLE_DEFINITION, 0)) ++snap_bad;
        }
        cc2x_body b;
        if (A->body_get(v, &b)) {
            ++bodies;
            double p[3] = {A->getf(v, CC2X_VEHICLE_POS, 0), A->getf(v, CC2X_VEHICLE_POS, 1), A->getf(v, CC2X_VEHICLE_POS, 2)};
            double qn = sqrt(b.rot[0] * b.rot[0] + b.rot[1] * b.rot[1] + b.rot[2] * b.rot[2] + b.rot[3] * b.rot[3]);
            double vn = sqrt(b.vel[0] * b.vel[0] + b.vel[1] * b.vel[1] + b.vel[2] * b.vel[2]);
            if (dist3(p, b.pos) > 0.01 || fabs(qn - 1.0) > 0.01 || !(vn < 1000.0) || !isfinite(b.spin[0])) ++body_bad;
        }
    }
    check(bad == 0, "find vehicle by id", "%d of %d wrong", bad, lookups);
    check(snap > 0 && snap_bad == 0, "vehicle fields = snapshot", "%d of %d differ", snap_bad, snap);
    check(bodies > 0 && body_bad == 0, "body_get", "%d bodies, %d inconsistent", bodies, body_bad);

    /* aircraft classes and a writable field written back unchanged */
    int air = 0, wing = 0, rotor = 0, mixed = 0;
    void *any = NULL;
    for (int i = 0; i < n; ++i) {
        void *v = A->ref(NULL, CC2X_WORLD_VEHICLES, i);
        int a = (int)A->geti(v, CC2X_VEHICLE_IS_AIR, 0), w = (int)A->geti(v, CC2X_VEHICLE_IS_WING, 0),
            r = (int)A->geti(v, CC2X_VEHICLE_IS_ROTOR, 0);
        air += a; wing += w; rotor += r;
        if ((w || r) && !a) ++mixed;
        if (!any) any = v;
    }
    check(mixed == 0, "aircraft classes", "%d aircraft (%d planes, %d helicopters), %d inconsistent", air, wing, rotor, mixed);
    int64_t ds = A->geti(any, CC2X_VEHICLE_DOCK_STATE, 0);
    check(A->seti(any, CC2X_VEHICLE_DOCK_STATE, 0, ds) == 1 && A->seti(any, CC2X_VEHICLE_ID, 0, 1) == 0 &&
          A->geti(any, CC2X_VEHICLE_DOCK_STATE, 0) == ds, "field write rules", NULL);
    check(A->geti(any, 0x7fff, 0) == 0 && A->ref(any, CC2X_VEHICLE_WAYPOINTS, 100000) == NULL, "unknown field / index", NULL);
}

static void test_tiles(void) {
    int n = A->count(NULL, CC2X_WORLD_TILES), bad = 0, at_bad = 0, islands = 0, airfields = 0, af_bad = 0;
    check(n > 0, "world tiles", "%d", n);
    void *af_any = NULL;
    for (int i = 0; i < n; ++i) {
        void *t = A->ref(NULL, CC2X_WORLD_TILES, i);
        uint32_t id = (uint32_t)A->geti(t, CC2X_TILE_ID, 0);
        if (A->find(CC2X_WORLD_TILES, id) != t) ++bad;
        if (A->tile_at(A->getf(t, CC2X_TILE_POS, 0), A->getf(t, CC2X_TILE_POS, 2)) != t) ++at_bad;
        void *island = A->ref(t, CC2X_TILE_ISLAND, 0);
        if (!island) continue;
        ++islands;
        int na = A->count(island, CC2X_ISLAND_AIRFIELDS);
        if (na != A->geti(t, CC2X_TILE_AIRFIELDS, 0)) ++af_bad;
        for (int k = 0; k < na; ++k) {
            void *af = A->ref(island, CC2X_ISLAND_AIRFIELDS, k);
            ++airfields;
            if ((uint32_t)A->geti(af, CC2X_AIRFIELD_TILE, 0) != id) ++af_bad;
            if (!af_any) af_any = af;
        }
    }
    check(bad == 0, "find tile by id", "%d of %d wrong", bad, n);
    check(at_bad == 0, "tile_at(tile centre)", "%d of %d wrong", at_bad, n);
    check(af_bad == 0, "islands and airfields", "%d islands loaded, %d airfields, %d inconsistent", islands, airfields, af_bad);
    if (!af_any) { LOG("no island airfield loaded: airfield checks skipped"); return; }

    void *af = af_any;
    double m[12];
    for (int k = 0; k < 12; ++k) m[k] = A->getf(af, CC2X_AIRFIELD_TRANSFORM, k);
    double xx = m[0] * m[0] + m[1] * m[1] + m[2] * m[2], xz = m[0] * m[6] + m[1] * m[7] + m[2] * m[8];
    check(fabs(xx - 1.0) < 1e-3 && fabs(xz) < 1e-3, "airfield transform", "|x|^2 %.4f, x.z %.4f", xx, xz);
    double s[3], e[3], bay[3], yaw, l[3] = {0, 0, 0}, o[3];
    for (int k = 0; k < 3; ++k) { s[k] = A->getf(af, CC2X_AIRFIELD_RUNWAY_START, k); e[k] = A->getf(af, CC2X_AIRFIELD_RUNWAY_END, k); }
    double len = dist3(s, e);
    check(len > 200.0 && len < 3000.0, "runway nodes", "length %.0f m", len);
    A->airfield_to_world(af, l, o);
    check(fabs(o[0] - m[9]) < 1e-6 && fabs(o[2] - m[11]) < 1e-6, "airfield_to_world", NULL);
    check(A->airfield_bay(af, 3, bay, &yaw) && fabs(dist3(bay, o) - 28.28) < 0.1 && !A->airfield_bay(af, 4, bay, &yaw),
          "airfield_bay", "bay 3 at %.1f m from the origin", dist3(bay, o));
    int hn = A->count(af, CC2X_AIRFIELD_HANGARS);
    LOG("airfield of tile %d: %d hangar slots, dock queue %d, undock queue %d, state %d, runway vehicle %d", (int)A->geti(af, CC2X_AIRFIELD_TILE, 0),
        hn, A->count(af, CC2X_AIRFIELD_DOCK_QUEUE), A->count(af, CC2X_AIRFIELD_UNDOCK_QUEUE),
        (int)A->geti(af, CC2X_AIRFIELD_STATE, 0), (int)A->geti(af, CC2X_AIRFIELD_RUNWAY_VEHICLE, 0));

    /* the runway: a ray straight down at its middle hits it at the height of its nodes (the
     * airfield origin lies in the hangar row, under the hangar roofs) */
    cc2x_hit hit;
    double mid = (s[1] + e[1]) / 2;
    double from[3] = {(s[0] + e[0]) / 2, mid + 500.0, (s[2] + e[2]) / 2}, down[3] = {0, -1, 0};
    int ok = A->raycast(from, down, 2000.0, CC2X_RAY_STATIC, &hit);
    double nn = ok ? sqrt(hit.normal[0] * hit.normal[0] + hit.normal[1] * hit.normal[1] + hit.normal[2] * hit.normal[2]) : 0;
    check(ok && fabs(hit.pos[1] - mid) < 5.0 && fabs(nn - 1.0) < 0.01 && fabs(hit.distance - (from[1] - hit.pos[1])) < 0.5,
          "raycast at a runway", ok ? "ground %.1f m, runway %.1f m, distance %.1f" : "no hit", ok ? hit.pos[1] : 0, mid, ok ? hit.distance : 0);
    double up[3] = {0, 1, 0};
    check(!A->raycast(from, up, 2000.0, CC2X_RAY_STATIC, &hit), "raycast into the sky", NULL);
}

static void on_tick(void *server, void *user) {
    ++g_tick;
    if (g_tick == 600 && !g_done) {               /* the world is loaded by then */
        g_done = 1;
        LOG("checks start (CC2X %s, API %u)", A->sdk_version, A->api_version);
        test_vehicles();
        test_tiles();
        LOG("checks done: %d passed, %d failed", g_pass, g_fail);
    }
    if (g_tick % 3600 == 0)
        LOG("events so far: waypoints %ld, airfield ticks %ld, hangar choices %ld, controller checks %ld, spawns %ld",
            g_ev_wp, g_ev_af, g_ev_hangar, g_ev_ctrl, g_ev_spawn);
}
static void on_session(void *server, int started, void *user) { g_tick = 0; g_done = 0; g_pass = g_fail = 0; }

/* observers: count, log a few, change nothing */
static void ev_wp(void *v, void *u) {
    if (++g_ev_wp <= 5) LOG("event waypoints: vehicle %d, %d waypoint(s)", (int)A->geti(v, CC2X_VEHICLE_ID, 0), A->count(v, CC2X_VEHICLE_WAYPOINTS));
}
static void ev_af(void *af, void *u) { ++g_ev_af; }
static int ev_hangar(void *af, void *v, void *u) {
    if (++g_ev_hangar <= 5) LOG("event hangar choice: tile %d, vehicle %d", (int)A->geti(af, CC2X_AIRFIELD_TILE, 0), (int)A->geti(v, CC2X_VEHICLE_ID, 0));
    return -1;
}
static int ev_ctrl(int team, void *u) {
    if (++g_ev_ctrl <= 5) LOG("event island controller: team %d (human team: the game says no)", team);
    return 0;
}
static int ev_spawn(void *tile, int ai, void *u) {
    if (++g_ev_spawn <= 10) LOG("event island spawn: tile %d team %d ai %d", (int)A->geti(tile, CC2X_TILE_ID, 0), (int)A->geti(tile, CC2X_TILE_TEAM, 0), ai);
    return 1;
}

CC2X_EXPORT int cc2x_plugin_load(const cc2x_api *api) {
    if (api->api_version < 3) return 0;
    A = api;
    api->on_server_tick(on_tick, NULL);
    api->on_session(on_session, NULL);
    int ok = api->on_waypoints(ev_wp, NULL) & api->on_airfield_tick(ev_af, NULL) & api->on_hangar_choice(ev_hangar, NULL) &
             api->on_island_controller(ev_ctrl, NULL) & api->on_island_spawn(ev_spawn, NULL);
    api->log("selftest", ok ? CC2X_LOG_INFO : CC2X_LOG_ERROR, "%s: all event hooks %s", ok ? "PASS" : "FAIL", ok ? "installed" : "NOT installed");
    return 1;
}
