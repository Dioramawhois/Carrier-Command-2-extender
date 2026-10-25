/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The world for Lua mods: a snapshot of the world API taken every server tick, game events since
 * the last poll, commands queued for the next server tick, and standing rules for the game's own
 * decisions. Also the API 1 snapshots of plugins (api->vehicles / api->tiles).
 *
 * Built only on the public plugin API (like a plugin), so everything here is also possible for a
 * plugin. Lua runs on the screens' thread, the world on the server thread: Lua never touches a
 * game object, it reads copies and sends commands.
 */
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal.h"

static const cc2x_api *A;
#define LOG(level, ...) api_log("cc2x", level, __VA_ARGS__)

/* ============================================================================ snapshot */

#define MAX_VEH 2048
#define MAX_TILES 512
#define MAX_AF 256
#define MAX_WP 16
#define MAX_HANGARS 8
#define MAX_QUEUE 16

typedef struct {
    uint32_t id, definition, team, hp, attached_to, dock_state, dock_vehicle, dock_tile, flight_state, control_mode;
    int32_t dock_airfield;
    uint8_t team_is_ai, is_air, is_wing, is_rotor, has_body, control_taxi, nwp;
    float fuel, control_speed, control_power;
    double pos[3], rot[4], vel[3], spin[3], control_target[3];
    uint32_t wp_type[MAX_WP], wp_param[MAX_WP];
} Veh;

typedef struct {
    uint32_t id, team;
    int32_t airfields;              /* -1 = not known yet */
    uint8_t island;                 /* island controller loaded */
    double pos[3];
} Tile;

typedef struct {
    uint32_t tile, index, state, mode;
    int32_t runway_vehicle;
    uint8_t nhangars, ndock, nundock, has_runway;
    double origin[3], yaw, runway_start[3], runway_end[3], bay[4][4];
    uint32_t hangars[MAX_HANGARS], dock[MAX_QUEUE], undock[MAX_QUEUE];
} Airfield;

typedef struct {
    uint32_t tick;
    int nveh, ntiles, naf;
    Veh veh[MAX_VEH];
    Tile tiles[MAX_TILES];
    Airfield af[MAX_AF];
} Snapshot;

static Snapshot g_back, g_front;    /* the server builds g_back, then copies it under g_snap_cs */
static CRITICAL_SECTION g_snap_cs;
static uint32_t g_tick;

static double yaw_of_rot(const double q[4]) {   /* heading of the nose (local +z) */
    double x = q[0], y = q[1], z = q[2], w = q[3];
    double nx = 2 * (x * z + w * y), nz = 1 - 2 * (x * x + y * y);
    return atan2(nx, nz);
}

static void take_vehicle(void *v, Veh *o) {
    memset(o, 0, sizeof(*o));
    o->id = (uint32_t)A->geti(v, CC2X_VEHICLE_ID, 0);
    o->definition = (uint32_t)A->geti(v, CC2X_VEHICLE_DEFINITION, 0);
    o->team = (uint32_t)A->geti(v, CC2X_VEHICLE_TEAM, 0);
    o->team_is_ai = (uint8_t)A->geti(v, CC2X_VEHICLE_TEAM_IS_AI, 0);
    o->is_air = (uint8_t)A->geti(v, CC2X_VEHICLE_IS_AIR, 0);
    o->is_wing = (uint8_t)A->geti(v, CC2X_VEHICLE_IS_WING, 0);
    o->is_rotor = (uint8_t)A->geti(v, CC2X_VEHICLE_IS_ROTOR, 0);
    o->hp = (uint32_t)A->geti(v, CC2X_VEHICLE_HP, 0);
    o->fuel = (float)A->getf(v, CC2X_VEHICLE_FUEL, 0);
    o->attached_to = (uint32_t)A->geti(v, CC2X_VEHICLE_ATTACHED_TO, 0);
    o->dock_state = (uint32_t)A->geti(v, CC2X_VEHICLE_DOCK_STATE, 0);
    o->dock_vehicle = (uint32_t)A->geti(v, CC2X_VEHICLE_DOCK_VEHICLE, 0);
    o->dock_tile = (uint32_t)A->geti(v, CC2X_VEHICLE_DOCK_TILE, 0);
    o->dock_airfield = (int32_t)A->geti(v, CC2X_VEHICLE_DOCK_AIRFIELD, 0);
    o->flight_state = (uint32_t)A->geti(v, CC2X_VEHICLE_FLIGHT_STATE, 0);
    o->control_mode = (uint32_t)A->geti(v, CC2X_VEHICLE_CONTROL_MODE, 0);
    o->control_speed = (float)A->getf(v, CC2X_VEHICLE_CONTROL_SPEED, 0);
    o->control_power = (float)A->getf(v, CC2X_VEHICLE_CONTROL_POWER, 0);
    o->control_taxi = (uint8_t)A->geti(v, CC2X_VEHICLE_CONTROL_TAXI, 0);
    for (int k = 0; k < 3; ++k) o->control_target[k] = A->getf(v, CC2X_VEHICLE_CONTROL_TARGET, k);
    cc2x_body b;
    if (A->body_get(v, &b)) {
        o->has_body = 1;
        memcpy(o->pos, b.pos, sizeof(o->pos));
        memcpy(o->rot, b.rot, sizeof(o->rot));
        memcpy(o->vel, b.vel, sizeof(o->vel));
        memcpy(o->spin, b.spin, sizeof(o->spin));
    } else {
        for (int k = 0; k < 3; ++k) o->pos[k] = A->getf(v, CC2X_VEHICLE_POS, k);
        o->rot[3] = 1.0;
    }
    int n = A->count(v, CC2X_VEHICLE_WAYPOINTS);
    for (int i = 0; i < n && i < MAX_WP; ++i) {
        void *wp = A->ref(v, CC2X_VEHICLE_WAYPOINTS, i);
        o->wp_type[i] = (uint32_t)A->geti(wp, CC2X_WAYPOINT_TYPE, 0);
        o->wp_param[i] = (uint32_t)A->geti(wp, CC2X_WAYPOINT_PARAM, 0);
        o->nwp++;
    }
}

static void take_airfield(void *af, uint32_t index, Airfield *o) {
    memset(o, 0, sizeof(*o));
    o->tile = (uint32_t)A->geti(af, CC2X_AIRFIELD_TILE, 0);
    o->index = index;
    o->state = (uint32_t)A->geti(af, CC2X_AIRFIELD_STATE, 0);
    o->mode = (uint32_t)A->geti(af, CC2X_AIRFIELD_MODE, 0);
    o->runway_vehicle = (int32_t)A->geti(af, CC2X_AIRFIELD_RUNWAY_VEHICLE, 0);
    double zero[3] = {0, 0, 0};
    A->airfield_to_world(af, zero, o->origin);
    o->yaw = cc2x_yaw_of(A->getf(af, CC2X_AIRFIELD_TRANSFORM, 6), A->getf(af, CC2X_AIRFIELD_TRANSFORM, 8));
    double s = 0, e = 0;
    for (int k = 0; k < 3; ++k) {
        o->runway_start[k] = A->getf(af, CC2X_AIRFIELD_RUNWAY_START, k);
        o->runway_end[k] = A->getf(af, CC2X_AIRFIELD_RUNWAY_END, k);
        s += fabs(o->runway_start[k]);
        e += fabs(o->runway_end[k]);
    }
    o->has_runway = s > 0 && e > 0;
    for (int b = 0; b < 4; ++b) A->airfield_bay(af, b, o->bay[b], &o->bay[b][3]);
    int n = A->count(af, CC2X_AIRFIELD_HANGARS);
    for (int i = 0; i < n && i < MAX_HANGARS; ++i) o->hangars[o->nhangars++] = (uint32_t)A->geti(af, CC2X_AIRFIELD_HANGARS, i);
    n = A->count(af, CC2X_AIRFIELD_DOCK_QUEUE);
    for (int i = 0; i < n && i < MAX_QUEUE; ++i) o->dock[o->ndock++] = (uint32_t)A->geti(af, CC2X_AIRFIELD_DOCK_QUEUE, i);
    n = A->count(af, CC2X_AIRFIELD_UNDOCK_QUEUE);
    for (int i = 0; i < n && i < MAX_QUEUE; ++i) o->undock[o->nundock++] = (uint32_t)A->geti(af, CC2X_AIRFIELD_UNDOCK_QUEUE, i);
}

static void take_snapshot(void) {
    Snapshot *s = &g_back;
    s->tick = g_tick;
    s->nveh = s->ntiles = s->naf = 0;
    int n = A->count(NULL, CC2X_WORLD_VEHICLES);
    for (int i = 0; i < n && s->nveh < MAX_VEH; ++i) {
        void *v = A->ref(NULL, CC2X_WORLD_VEHICLES, i);
        if (v) take_vehicle(v, &s->veh[s->nveh++]);
    }
    n = A->count(NULL, CC2X_WORLD_TILES);
    for (int i = 0; i < n && s->ntiles < MAX_TILES; ++i) {
        void *t = A->ref(NULL, CC2X_WORLD_TILES, i);
        if (!t) continue;
        Tile *o = &s->tiles[s->ntiles++];
        o->id = (uint32_t)A->geti(t, CC2X_TILE_ID, 0);
        o->team = (uint32_t)A->geti(t, CC2X_TILE_TEAM, 0);
        o->airfields = (int32_t)A->geti(t, CC2X_TILE_AIRFIELDS, 0);
        for (int k = 0; k < 3; ++k) o->pos[k] = A->getf(t, CC2X_TILE_POS, k);
        void *island = A->ref(t, CC2X_TILE_ISLAND, 0);
        o->island = island != NULL;
        int na = island ? A->count(island, CC2X_ISLAND_AIRFIELDS) : 0;
        for (int a = 0; a < na && s->naf < MAX_AF; ++a) {
            void *af = A->ref(island, CC2X_ISLAND_AIRFIELDS, a);
            if (af) take_airfield(af, (uint32_t)a, &s->af[s->naf++]);
        }
    }
    EnterCriticalSection(&g_snap_cs);
    g_front.tick = s->tick;
    g_front.nveh = s->nveh;
    g_front.ntiles = s->ntiles;
    g_front.naf = s->naf;
    memcpy(g_front.veh, s->veh, sizeof(Veh) * s->nveh);
    memcpy(g_front.tiles, s->tiles, sizeof(Tile) * s->ntiles);
    memcpy(g_front.af, s->af, sizeof(Airfield) * s->naf);
    LeaveCriticalSection(&g_snap_cs);
}

static void clear_snapshot(void) {
    EnterCriticalSection(&g_snap_cs);
    g_front.tick = 0;
    g_front.nveh = g_front.ntiles = g_front.naf = 0;
    LeaveCriticalSection(&g_snap_cs);
}

/* ---- API 1 snapshot records for plugins (any thread) */
static void to_info(const Veh *v, cc2x_vehicle_info *o) {
    o->id = v->id; o->definition = v->definition; o->team = v->team; o->dock_state = v->dock_state;
    o->attached_to = v->attached_to; o->hp = v->hp; o->fuel = v->fuel;
    o->x = v->pos[0]; o->y = v->pos[1]; o->z = v->pos[2];
}
static void to_tile_info(const Tile *t, cc2x_tile_info *o) {
    o->id = t->id; o->team = t->team; o->x = t->pos[0]; o->z = t->pos[2]; o->airfields = t->island ? t->airfields : -1;
}

int snap_vehicles(cc2x_vehicle_info *out, int max) {
    EnterCriticalSection(&g_snap_cs);
    int n = g_front.nveh;
    for (int i = 0; out && i < n && i < max; ++i) to_info(&g_front.veh[i], &out[i]);
    LeaveCriticalSection(&g_snap_cs);
    return n;
}
int snap_vehicle(uint32_t id, cc2x_vehicle_info *out) {
    int found = 0;
    EnterCriticalSection(&g_snap_cs);
    for (int i = 0; i < g_front.nveh && !found; ++i)
        if (g_front.veh[i].id == id) { if (out) to_info(&g_front.veh[i], out); found = 1; }
    LeaveCriticalSection(&g_snap_cs);
    return found;
}
int snap_tiles(cc2x_tile_info *out, int max) {
    EnterCriticalSection(&g_snap_cs);
    int n = g_front.ntiles;
    for (int i = 0; out && i < n && i < max; ++i) to_tile_info(&g_front.tiles[i], &out[i]);
    LeaveCriticalSection(&g_snap_cs);
    return n;
}
int snap_tile(uint32_t id, cc2x_tile_info *out) {
    int found = 0;
    EnterCriticalSection(&g_snap_cs);
    for (int i = 0; i < g_front.ntiles && !found; ++i)
        if (g_front.tiles[i].id == id) { if (out) to_tile_info(&g_front.tiles[i], out); found = 1; }
    LeaveCriticalSection(&g_snap_cs);
    return found;
}

/* ============================================================================ events */

enum { EV_SESSION = 1, EV_VEHICLE_ADDED, EV_VEHICLE_REMOVED, EV_WAYPOINTS, EV_ISLAND_SPAWN, EV_RAYCAST, EV_LANDING };
static const char *const EV_NAMES[] = {"", "session", "vehicle_added", "vehicle_removed", "waypoints", "island_spawn",
                                       "raycast", "landing"};

typedef struct {
    uint32_t seq;
    int type;
    uint32_t a, b, c;
    int64_t tag;
    double v[7];
} Ev;

#define EV_MAX 1024
#define MAX_READERS 64
static Ev g_ev[EV_MAX];
static uint32_t g_ev_next = 1;      /* sequence number of the next event */
static CRITICAL_SECTION g_ev_cs;
typedef struct { uintptr_t key; uint32_t next; } Reader;
static Reader g_readers[MAX_READERS];
static int g_nreaders;

static void emit(int type, uint32_t a, uint32_t b, uint32_t c, int64_t tag, const double *v, int nv) {
    EnterCriticalSection(&g_ev_cs);
    Ev *e = &g_ev[g_ev_next % EV_MAX];
    memset(e, 0, sizeof(*e));
    e->seq = g_ev_next++;
    e->type = type;
    e->a = a; e->b = b; e->c = c;
    e->tag = tag;
    for (int k = 0; k < nv && k < 7; ++k) e->v[k] = v[k];
    LeaveCriticalSection(&g_ev_cs);
}

/* vehicles that appeared / disappeared since the last tick (ids sorted, merged) */
static uint32_t g_ids[2][MAX_VEH];
static int g_nids[2], g_cur, g_have_ids;

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static void diff_vehicles(void) {
    int now = g_cur ^ 1, n = 0;
    for (int i = 0; i < g_back.nveh; ++i) g_ids[now][n++] = g_back.veh[i].id;
    qsort(g_ids[now], (size_t)n, sizeof(uint32_t), cmp_u32);
    g_nids[now] = n;
    if (g_have_ids) {
        const uint32_t *p = g_ids[g_cur], *c = g_ids[now];
        int i = 0, j = 0, np = g_nids[g_cur];
        while (i < np || j < n) {
            if (j >= n || (i < np && p[i] < c[j])) emit(EV_VEHICLE_REMOVED, p[i++], 0, 0, 0, NULL, 0);
            else if (i >= np || c[j] < p[i]) {
                for (int k = 0; k < g_back.nveh; ++k)
                    if (g_back.veh[k].id == c[j]) {
                        emit(EV_VEHICLE_ADDED, c[j], g_back.veh[k].definition, g_back.veh[k].team, 0, NULL, 0);
                        break;
                    }
                ++j;
            } else { ++i; ++j; }
        }
    }
    g_cur = now;
    g_have_ids = 1;
}

/* ============================================================================ rules for the game's decisions */

#define MAX_TEAMS 64
#define MAX_HANGAR_RULES 64
static uint8_t g_ctrl_team[MAX_TEAMS];          /* 1 = islands of this team get a controller */
static uint8_t g_spawn_off[MAX_TEAMS];          /* 1 = no defence spawns on islands of this team */
static struct { uint32_t vehicle; int slot; } g_hangar_rule[MAX_HANGAR_RULES];
static CRITICAL_SECTION g_rule_cs;

static int rule_controller(int team, void *user) {
    (void)user;
    return team >= 0 && team < MAX_TEAMS && g_ctrl_team[team];
}
static int rule_spawn(void *tile, int ai_team, void *user) {
    (void)user;
    int64_t team = A->geti(tile, CC2X_TILE_TEAM, 0);
    emit(EV_ISLAND_SPAWN, (uint32_t)A->geti(tile, CC2X_TILE_ID, 0), (uint32_t)team, (uint32_t)ai_team, 0, NULL, 0);
    return !(team >= 0 && team < MAX_TEAMS && g_spawn_off[team]);
}
static int rule_hangar(void *af, void *vehicle, void *user) {
    (void)af; (void)user;
    uint32_t id = (uint32_t)A->geti(vehicle, CC2X_VEHICLE_ID, 0);
    int slot = -1;
    EnterCriticalSection(&g_rule_cs);
    for (int i = 0; i < MAX_HANGAR_RULES; ++i)
        if (g_hangar_rule[i].vehicle == id) slot = g_hangar_rule[i].slot;
    LeaveCriticalSection(&g_rule_cs);
    return slot;
}
static void on_waypoints(void *vehicle, void *user) {
    (void)user;
    emit(EV_WAYPOINTS, (uint32_t)A->geti(vehicle, CC2X_VEHICLE_ID, 0), (uint32_t)A->count(vehicle, CC2X_VEHICLE_WAYPOINTS),
         0, 0, NULL, 0);
}

/* subscriptions are made on the server thread, where the hooked game code runs */
enum { SUB_WAYPOINTS = 1, SUB_SPAWN = 2, SUB_CONTROLLER = 4, SUB_HANGAR = 8 };
static volatile LONG g_subs_wanted, g_subs_done;

static void subscribe_wanted(void) {
    LONG want = g_subs_wanted & ~g_subs_done;
    if (!want) return;
    if ((want & SUB_WAYPOINTS) && A->on_waypoints(on_waypoints, NULL)) InterlockedOr(&g_subs_done, SUB_WAYPOINTS);
    if ((want & SUB_SPAWN) && A->on_island_spawn(rule_spawn, NULL)) InterlockedOr(&g_subs_done, SUB_SPAWN);
    if ((want & SUB_CONTROLLER) && A->on_island_controller(rule_controller, NULL)) InterlockedOr(&g_subs_done, SUB_CONTROLLER);
    if ((want & SUB_HANGAR) && A->on_hangar_choice(rule_hangar, NULL)) InterlockedOr(&g_subs_done, SUB_HANGAR);
    InterlockedAnd(&g_subs_wanted, g_subs_done);     /* a hook that failed is not tried again (logged) */
}

/* ============================================================================ commands */

enum {
    CMD_SET_VEHICLE = 1, CMD_SET_AIRFIELD, CMD_SET_WAYPOINT, CMD_CLEAR_WAYPOINTS, CMD_BODY, CMD_BRAKES, CMD_GEAR,
    CMD_LANDING, CMD_QUEUE_PUSH, CMD_QUEUE_REMOVE, CMD_RAYCAST
};
typedef struct {
    int type, field, index, what;
    uint32_t id, id2, af;
    int64_t tag;
    double v[8];
} Cmd;

#define CMD_MAX 1024
static Cmd g_cmd[CMD_MAX];
static int g_cmd_head, g_cmd_count;
static CRITICAL_SECTION g_cmd_cs;

static int queue_cmd(const Cmd *c) {
    int ok = 0;
    EnterCriticalSection(&g_cmd_cs);
    if (g_cmd_count < CMD_MAX) {
        g_cmd[(g_cmd_head + g_cmd_count) % CMD_MAX] = *c;
        g_cmd_count++;
        ok = 1;
    }
    LeaveCriticalSection(&g_cmd_cs);
    return ok;
}

static void *airfield_of(uint32_t tile_id, uint32_t index) {
    void *t = A->find(CC2X_WORLD_TILES, tile_id);
    void *island = t ? A->ref(t, CC2X_TILE_ISLAND, 0) : NULL;
    return island ? A->ref(island, CC2X_ISLAND_AIRFIELDS, (int)index) : NULL;
}

static void run_cmd(const Cmd *c) {
    void *v = (c->type == CMD_RAYCAST || c->type == CMD_SET_AIRFIELD || c->type == CMD_QUEUE_PUSH ||
               c->type == CMD_QUEUE_REMOVE) ? NULL : A->find(CC2X_WORLD_VEHICLES, c->id);
    switch (c->type) {
    case CMD_SET_VEHICLE: if (v) A->setf(v, c->field, c->index, c->v[0]); break;
    case CMD_SET_AIRFIELD: {
        void *af = airfield_of(c->id, c->af);
        if (af) A->setf(af, c->field, c->index, c->v[0]);
        break;
    }
    case CMD_SET_WAYPOINT: {
        void *wp = v ? A->ref(v, CC2X_VEHICLE_WAYPOINTS, c->index) : NULL;
        if (wp && A->setf(wp, c->field, 0, c->v[0])) A->waypoints_sync(v);
        break;
    }
    case CMD_CLEAR_WAYPOINTS: if (v && A->list_clear(v, CC2X_VEHICLE_WAYPOINTS)) A->waypoints_sync(v); break;
    case CMD_BODY: {
        cc2x_body b;
        if (!v || !A->body_get(v, &b)) break;
        double *dst = c->what == CC2X_BODY_POS ? b.pos : c->what == CC2X_BODY_VEL ? b.vel : c->what == CC2X_BODY_SPIN ? b.spin : b.rot;
        memcpy(dst, c->v, sizeof(double) * (c->what == CC2X_BODY_ROT ? 4 : 3));
        A->body_set(v, &b, c->what);
        break;
    }
    case CMD_BRAKES: if (v) A->vehicle_brakes(v, (float)c->v[0], (float)c->v[1]); break;
    case CMD_GEAR: if (v) A->vehicle_gear(v, c->v[0] != 0); break;
    case CMD_LANDING: {
        void *t = A->find(CC2X_WORLD_TILES, c->id2);
        void *island = t ? A->ref(t, CC2X_TILE_ISLAND, 0) : NULL;
        int ok = v && island && A->request_landing(island, v);
        emit(EV_LANDING, c->id, c->id2, (uint32_t)ok, 0, NULL, 0);
        break;
    }
    case CMD_QUEUE_PUSH:
    case CMD_QUEUE_REMOVE: {
        void *af = airfield_of(c->id, c->af);
        if (!af) break;
        if (c->type == CMD_QUEUE_PUSH) { A->list_push(af, c->field, c->id2); break; }
        int n = A->count(af, c->field);
        for (int i = 0; i < n; ++i)
            if ((uint32_t)A->geti(af, c->field, i) == c->id2) { A->list_remove(af, c->field, i); break; }
        break;
    }
    case CMD_RAYCAST: {
        cc2x_hit h;
        memset(&h, 0, sizeof(h));
        int hit = A->raycast(c->v, c->v + 3, c->v[6], (int)c->v[7], &h);
        double out[7] = {h.pos[0], h.pos[1], h.pos[2], h.normal[0], h.normal[1], h.normal[2], h.distance};
        emit(EV_RAYCAST, (uint32_t)hit, 0, 0, c->tag, out, 7);
        break;
    }
    }
}

static void on_tick(void *server, void *user) {
    (void)server; (void)user;
    ++g_tick;
    subscribe_wanted();
    for (;;) {
        Cmd c;
        int have = 0;
        EnterCriticalSection(&g_cmd_cs);
        if (g_cmd_count) {
            c = g_cmd[g_cmd_head];
            g_cmd_head = (g_cmd_head + 1) % CMD_MAX;
            g_cmd_count--;
            have = 1;
        }
        LeaveCriticalSection(&g_cmd_cs);
        if (!have) break;
        run_cmd(&c);
    }
    take_snapshot();
    diff_vehicles();
}

static void on_session(void *server, int started, void *user) {
    (void)server; (void)user;
    g_tick = 0;
    g_have_ids = 0;
    EnterCriticalSection(&g_cmd_cs);
    g_cmd_count = 0;
    LeaveCriticalSection(&g_cmd_cs);
    if (!started) clear_snapshot();
    emit(EV_SESSION, (uint32_t)started, 0, 0, 0, NULL, 0);
}

/* ============================================================================ Lua functions */

#define L_NUM(i, out) A->lua_tonumber(L, i, out)
static int64_t arg_int(cc2x_lua *L, int i, int64_t def) { int64_t v; return A->lua_tointeger(L, i, &v) ? v : def; }
static int ret_bool(cc2x_lua *L, int b) { A->lua_pushboolean(L, b); return 1; }
static int ret_nil(cc2x_lua *L, const char *why) { A->lua_pushnil(L); A->lua_pushstring(L, why); return 2; }

static void set_i(cc2x_lua *L, const char *k, int64_t v) { A->lua_pushinteger(L, v); A->lua_setfield(L, k); }
static void set_n(cc2x_lua *L, const char *k, double v) { A->lua_pushnumber(L, v); A->lua_setfield(L, k); }
static void set_b(cc2x_lua *L, const char *k, int v) { A->lua_pushboolean(L, v); A->lua_setfield(L, k); }
static void set_s(cc2x_lua *L, const char *k, const char *v) { A->lua_pushstring(L, v); A->lua_setfield(L, k); }
static void set_ids(cc2x_lua *L, const char *k, const uint32_t *ids, int n) {
    A->lua_newtable(L);
    for (int i = 0; i < n; ++i) { A->lua_pushinteger(L, ids[i]); A->lua_appendi(L, i + 1); }
    A->lua_setfield(L, k);
}

static void push_vehicle(cc2x_lua *L, const Veh *v) {
    A->lua_newtable(L);
    set_i(L, "id", v->id);
    set_i(L, "definition", v->definition);
    set_i(L, "team", v->team);
    set_b(L, "team_is_ai", v->team_is_ai);
    set_b(L, "is_air", v->is_air);
    set_b(L, "is_wing", v->is_wing);
    set_b(L, "is_rotor", v->is_rotor);
    set_i(L, "hp", v->hp);
    set_n(L, "fuel", v->fuel);
    set_n(L, "x", v->pos[0]); set_n(L, "y", v->pos[1]); set_n(L, "z", v->pos[2]);
    set_n(L, "qx", v->rot[0]); set_n(L, "qy", v->rot[1]); set_n(L, "qz", v->rot[2]); set_n(L, "qw", v->rot[3]);
    set_n(L, "yaw", yaw_of_rot(v->rot));
    set_n(L, "vx", v->vel[0]); set_n(L, "vy", v->vel[1]); set_n(L, "vz", v->vel[2]);
    set_n(L, "speed", sqrt(v->vel[0] * v->vel[0] + v->vel[1] * v->vel[1] + v->vel[2] * v->vel[2]));
    set_n(L, "spin_x", v->spin[0]); set_n(L, "spin_y", v->spin[1]); set_n(L, "spin_z", v->spin[2]);
    set_i(L, "attached_to", v->attached_to);
    set_i(L, "dock_state", v->dock_state);
    set_i(L, "dock_vehicle", v->dock_vehicle);
    set_i(L, "dock_tile", v->dock_tile);
    set_i(L, "dock_airfield", v->dock_airfield);
    set_i(L, "flight_state", v->flight_state);
    set_i(L, "control_mode", v->control_mode);
    set_n(L, "control_x", v->control_target[0]);
    set_n(L, "control_y", v->control_target[1]);
    set_n(L, "control_z", v->control_target[2]);
    set_n(L, "control_speed", v->control_speed);
    set_n(L, "control_power", v->control_power);
    set_b(L, "control_taxi", v->control_taxi);
    A->lua_newtable(L);
    for (int i = 0; i < v->nwp; ++i) {
        A->lua_newtable(L);
        set_i(L, "type", v->wp_type[i]);
        set_i(L, "param", v->wp_param[i]);
        A->lua_appendi(L, i + 1);
    }
    A->lua_setfield(L, "waypoints");
}

static void push_tile(cc2x_lua *L, const Tile *t) {
    A->lua_newtable(L);
    set_i(L, "id", t->id);
    set_i(L, "team", t->team);
    set_n(L, "x", t->pos[0]); set_n(L, "y", t->pos[1]); set_n(L, "z", t->pos[2]);
    set_b(L, "island", t->island);
    if (t->airfields >= 0) set_i(L, "airfields", t->airfields);
}

static void push_airfield(cc2x_lua *L, const Airfield *a) {
    A->lua_newtable(L);
    set_i(L, "tile", a->tile);
    set_i(L, "index", a->index);
    set_n(L, "x", a->origin[0]); set_n(L, "y", a->origin[1]); set_n(L, "z", a->origin[2]);
    set_n(L, "yaw", a->yaw);
    if (a->has_runway) {
        set_n(L, "runway_start_x", a->runway_start[0]); set_n(L, "runway_start_y", a->runway_start[1]);
        set_n(L, "runway_start_z", a->runway_start[2]);
        set_n(L, "runway_end_x", a->runway_end[0]); set_n(L, "runway_end_y", a->runway_end[1]);
        set_n(L, "runway_end_z", a->runway_end[2]);
    }
    set_i(L, "state", a->state);
    set_i(L, "mode", a->mode);
    set_i(L, "runway_vehicle", a->runway_vehicle);
    set_ids(L, "hangars", a->hangars, a->nhangars);
    set_ids(L, "dock_queue", a->dock, a->ndock);
    set_ids(L, "undock_queue", a->undock, a->nundock);
    A->lua_newtable(L);
    for (int b = 0; b < 4; ++b) {
        A->lua_newtable(L);
        set_n(L, "x", a->bay[b][0]); set_n(L, "y", a->bay[b][1]); set_n(L, "z", a->bay[b][2]); set_n(L, "yaw", a->bay[b][3]);
        A->lua_appendi(L, b + 1);
    }
    A->lua_setfield(L, "bays");
}

/* copies for the Lua thread (Lua functions run on one thread) */
static Snapshot g_lua;

static int lf_vehicles(cc2x_lua *L) {
    EnterCriticalSection(&g_snap_cs);
    int n = g_front.nveh;
    memcpy(g_lua.veh, g_front.veh, sizeof(Veh) * n);
    LeaveCriticalSection(&g_snap_cs);
    int64_t team = arg_int(L, 1, -1);
    A->lua_newtable(L);
    for (int i = 0, k = 0; i < n; ++i)
        if (team < 0 || g_lua.veh[i].team == (uint32_t)team) { push_vehicle(L, &g_lua.veh[i]); A->lua_appendi(L, ++k); }
    return 1;
}
static int lf_vehicle(cc2x_lua *L) {
    int64_t id = arg_int(L, 1, -1);
    int found = 0;
    EnterCriticalSection(&g_snap_cs);
    for (int i = 0; i < g_front.nveh && !found; ++i)
        if (g_front.veh[i].id == (uint32_t)id) { g_lua.veh[0] = g_front.veh[i]; found = 1; }
    LeaveCriticalSection(&g_snap_cs);
    if (!found) { A->lua_pushnil(L); return 1; }
    push_vehicle(L, &g_lua.veh[0]);
    return 1;
}
static int lf_tiles(cc2x_lua *L) {
    EnterCriticalSection(&g_snap_cs);
    int n = g_front.ntiles;
    memcpy(g_lua.tiles, g_front.tiles, sizeof(Tile) * n);
    LeaveCriticalSection(&g_snap_cs);
    A->lua_newtable(L);
    for (int i = 0; i < n; ++i) { push_tile(L, &g_lua.tiles[i]); A->lua_appendi(L, i + 1); }
    return 1;
}
static int lf_tile(cc2x_lua *L) {
    int64_t id = arg_int(L, 1, -1);
    int found = 0;
    EnterCriticalSection(&g_snap_cs);
    for (int i = 0; i < g_front.ntiles && !found; ++i)
        if (g_front.tiles[i].id == (uint32_t)id) { g_lua.tiles[0] = g_front.tiles[i]; found = 1; }
    LeaveCriticalSection(&g_snap_cs);
    if (!found) { A->lua_pushnil(L); return 1; }
    push_tile(L, &g_lua.tiles[0]);
    return 1;
}
/* cc2x.airfields([tile_id]): the airfields of loaded islands */
static int lf_airfields(cc2x_lua *L) {
    int64_t tile = arg_int(L, 1, -1);
    EnterCriticalSection(&g_snap_cs);
    int n = g_front.naf;
    memcpy(g_lua.af, g_front.af, sizeof(Airfield) * n);
    LeaveCriticalSection(&g_snap_cs);
    A->lua_newtable(L);
    for (int i = 0, k = 0; i < n; ++i)
        if (tile < 0 || g_lua.af[i].tile == (uint32_t)tile) { push_airfield(L, &g_lua.af[i]); A->lua_appendi(L, ++k); }
    return 1;
}
static int lf_tick(cc2x_lua *L) {
    EnterCriticalSection(&g_snap_cs);
    uint32_t t = g_front.tick;
    LeaveCriticalSection(&g_snap_cs);
    A->lua_pushinteger(L, t);
    return 1;
}

/* cc2x.events(): what happened since this Lua state's last call */
static int lf_events(cc2x_lua *L) {
    uintptr_t key = lua_state_key(L);
    static Ev buf[EV_MAX];
    int n = 0;
    uint32_t lost = 0;
    EnterCriticalSection(&g_ev_cs);
    Reader *r = NULL;
    for (int i = 0; i < g_nreaders; ++i)
        if (g_readers[i].key == key) r = &g_readers[i];
    if (!r) {
        if (g_nreaders == MAX_READERS) g_nreaders = 0;   /* states come and go: recycle */
        r = &g_readers[g_nreaders++];
        r->key = key;
        r->next = g_ev_next;                             /* a new reader starts now */
    }
    if (g_ev_next - r->next > EV_MAX) {
        lost = g_ev_next - r->next - EV_MAX;
        r->next = g_ev_next - EV_MAX;
    }
    for (uint32_t s = r->next; s != g_ev_next; ++s) buf[n++] = g_ev[s % EV_MAX];
    r->next = g_ev_next;
    LeaveCriticalSection(&g_ev_cs);

    A->lua_newtable(L);
    int k = 0;
    if (lost) {
        A->lua_newtable(L);
        set_s(L, "type", "overflow");
        set_i(L, "lost", lost);
        A->lua_appendi(L, ++k);
    }
    for (int i = 0; i < n; ++i) {
        const Ev *e = &buf[i];
        A->lua_newtable(L);
        set_s(L, "type", EV_NAMES[e->type]);
        switch (e->type) {
        case EV_SESSION: set_b(L, "started", e->a); break;
        case EV_VEHICLE_ADDED: set_i(L, "vehicle", e->a); set_i(L, "definition", e->b); set_i(L, "team", e->c); break;
        case EV_VEHICLE_REMOVED: set_i(L, "vehicle", e->a); break;
        case EV_WAYPOINTS: set_i(L, "vehicle", e->a); set_i(L, "count", e->b); break;
        case EV_ISLAND_SPAWN: set_i(L, "tile", e->a); set_i(L, "team", e->b); set_b(L, "ai_team", e->c); break;
        case EV_LANDING: set_i(L, "vehicle", e->a); set_i(L, "tile", e->b); set_b(L, "queued", e->c); break;
        case EV_RAYCAST:
            set_i(L, "tag", e->tag);
            set_b(L, "hit", e->a);
            if (e->a) {
                set_n(L, "x", e->v[0]); set_n(L, "y", e->v[1]); set_n(L, "z", e->v[2]);
                set_n(L, "nx", e->v[3]); set_n(L, "ny", e->v[4]); set_n(L, "nz", e->v[5]);
                set_n(L, "distance", e->v[6]);
            }
            break;
        }
        A->lua_appendi(L, ++k);
    }
    return 1;
}

/* cc2x.listen(name): turn on an event that needs a game hook ("waypoints", "island_spawn") */
static int lf_listen(cc2x_lua *L) {
    const char *n = A->lua_tostring(L, 1, NULL);
    LONG bit = !n ? 0 : !strcmp(n, "waypoints") ? SUB_WAYPOINTS : !strcmp(n, "island_spawn") ? SUB_SPAWN : 0;
    if (!bit) return ret_nil(L, "unknown event");
    InterlockedOr(&g_subs_wanted, bit);
    return ret_bool(L, 1);
}

/* ---- rules */
static int lf_island_controller(cc2x_lua *L) {      /* (team, on) */
    int64_t team = arg_int(L, 1, -1);
    if (team < 0 || team >= MAX_TEAMS) return ret_nil(L, "bad team");
    g_ctrl_team[team] = (uint8_t)A->lua_toboolean(L, 2);
    InterlockedOr(&g_subs_wanted, SUB_CONTROLLER);
    return ret_bool(L, 1);
}
static int lf_island_spawn(cc2x_lua *L) {           /* (team, allowed) */
    int64_t team = arg_int(L, 1, -1);
    if (team < 0 || team >= MAX_TEAMS) return ret_nil(L, "bad team");
    g_spawn_off[team] = (uint8_t)!A->lua_toboolean(L, 2);
    InterlockedOr(&g_subs_wanted, SUB_SPAWN);
    return ret_bool(L, 1);
}
static int lf_hangar(cc2x_lua *L) {                 /* (vehicle_id, slot | nil) */
    int64_t id = arg_int(L, 1, 0), slot = arg_int(L, 2, -1);
    if (id <= 0) return ret_nil(L, "bad vehicle id");
    int ok = 0;
    EnterCriticalSection(&g_rule_cs);
    for (int i = 0; i < MAX_HANGAR_RULES && !ok; ++i)
        if (g_hangar_rule[i].vehicle == (uint32_t)id) {
            if (slot < 0) g_hangar_rule[i].vehicle = 0;
            else g_hangar_rule[i].slot = (int)slot;
            ok = 1;
        }
    for (int i = 0; i < MAX_HANGAR_RULES && !ok && slot >= 0; ++i)
        if (!g_hangar_rule[i].vehicle) { g_hangar_rule[i].vehicle = (uint32_t)id; g_hangar_rule[i].slot = (int)slot; ok = 1; }
    LeaveCriticalSection(&g_rule_cs);
    if (slot >= 0) InterlockedOr(&g_subs_wanted, SUB_HANGAR);
    return ok || slot < 0 ? ret_bool(L, 1) : ret_nil(L, "too many hangar rules");
}

/* ---- commands: field names Lua may write */
typedef struct { const char *name; int field, index; } Name;
static const Name VEHICLE_FIELDS[] = {
    {"dock_state", CC2X_VEHICLE_DOCK_STATE, 0}, {"dock_vehicle", CC2X_VEHICLE_DOCK_VEHICLE, 0},
    {"dock_tile", CC2X_VEHICLE_DOCK_TILE, 0}, {"dock_airfield", CC2X_VEHICLE_DOCK_AIRFIELD, 0},
    {"flight_state", CC2X_VEHICLE_FLIGHT_STATE, 0}, {"control_mode", CC2X_VEHICLE_CONTROL_MODE, 0},
    {"control_x", CC2X_VEHICLE_CONTROL_TARGET, 0}, {"control_y", CC2X_VEHICLE_CONTROL_TARGET, 1},
    {"control_z", CC2X_VEHICLE_CONTROL_TARGET, 2}, {"control_speed", CC2X_VEHICLE_CONTROL_SPEED, 0},
    {"control_power", CC2X_VEHICLE_CONTROL_POWER, 0}, {"control_taxi", CC2X_VEHICLE_CONTROL_TAXI, 0},
    {"attachment_flying", CC2X_VEHICLE_ATTACHMENT_FLYING, -1}, {NULL, 0, 0}};
static const Name AIRFIELD_FIELDS[] = {
    {"state", CC2X_AIRFIELD_STATE, 0}, {"mode", CC2X_AIRFIELD_MODE, 0},
    {"runway_vehicle", CC2X_AIRFIELD_RUNWAY_VEHICLE, 0}, {"hangar", CC2X_AIRFIELD_HANGARS, -1}, {NULL, 0, 0}};

static const Name *lookup(const Name *t, const char *n) {
    for (; n && t->name; ++t)
        if (!strcmp(t->name, n)) return t;
    return NULL;
}
static double arg_value(cc2x_lua *L, int i) {
    double d;
    if (A->lua_type(L, i) == CC2X_LUA_BOOLEAN) return A->lua_toboolean(L, i);
    return L_NUM(i, &d) ? d : 0.0;
}

/* cc2x.set_vehicle(id, field, value [, index]) */
static int lf_set_vehicle(cc2x_lua *L) {
    const Name *f = lookup(VEHICLE_FIELDS, A->lua_tostring(L, 2, NULL));
    if (!f) return ret_nil(L, "unknown or read-only field");
    Cmd c = {.type = CMD_SET_VEHICLE, .id = (uint32_t)arg_int(L, 1, 0), .field = f->field,
             .index = f->index < 0 ? (int)arg_int(L, 4, 0) : f->index};
    c.v[0] = arg_value(L, 3);
    return ret_bool(L, queue_cmd(&c));
}
/* cc2x.set_airfield(tile_id, airfield_index, field, value [, index]) */
static int lf_set_airfield(cc2x_lua *L) {
    const Name *f = lookup(AIRFIELD_FIELDS, A->lua_tostring(L, 3, NULL));
    if (!f) return ret_nil(L, "unknown or read-only field");
    Cmd c = {.type = CMD_SET_AIRFIELD, .id = (uint32_t)arg_int(L, 1, 0), .af = (uint32_t)arg_int(L, 2, 0),
             .field = f->field, .index = f->index < 0 ? (int)arg_int(L, 5, 0) : f->index};
    c.v[0] = arg_value(L, 4);
    return ret_bool(L, queue_cmd(&c));
}
/* cc2x.set_waypoint(vehicle_id, index, type [, param]) - index 0 = the first waypoint */
static int lf_set_waypoint(cc2x_lua *L) {
    Cmd c = {.type = CMD_SET_WAYPOINT, .id = (uint32_t)arg_int(L, 1, 0), .index = (int)arg_int(L, 2, 0),
             .field = CC2X_WAYPOINT_TYPE};
    c.v[0] = arg_value(L, 3);
    int ok = queue_cmd(&c);
    if (ok && A->lua_type(L, 4) == CC2X_LUA_NUMBER) {
        c.field = CC2X_WAYPOINT_PARAM;
        c.v[0] = arg_value(L, 4);
        ok = queue_cmd(&c);
    }
    return ret_bool(L, ok);
}
static int lf_clear_waypoints(cc2x_lua *L) {
    Cmd c = {.type = CMD_CLEAR_WAYPOINTS, .id = (uint32_t)arg_int(L, 1, 0)};
    return ret_bool(L, queue_cmd(&c));
}
static int body_cmd(cc2x_lua *L, int what, int n) {
    Cmd c = {.type = CMD_BODY, .id = (uint32_t)arg_int(L, 1, 0), .what = what};
    for (int k = 0; k < n; ++k)
        if (!L_NUM(2 + k, &c.v[k])) return ret_nil(L, "numbers expected");
    return ret_bool(L, queue_cmd(&c));
}
static int lf_set_position(cc2x_lua *L) { return body_cmd(L, CC2X_BODY_POS, 3); }
static int lf_set_velocity(cc2x_lua *L) { return body_cmd(L, CC2X_BODY_VEL, 3); }
static int lf_set_spin(cc2x_lua *L) { return body_cmd(L, CC2X_BODY_SPIN, 3); }
/* cc2x.set_rotation(id, yaw, pitch, roll) */
static int lf_set_rotation(cc2x_lua *L) {
    double ypr[3];
    for (int k = 0; k < 3; ++k)
        if (!L_NUM(2 + k, &ypr[k])) return ret_nil(L, "numbers expected");
    Cmd c = {.type = CMD_BODY, .id = (uint32_t)arg_int(L, 1, 0), .what = CC2X_BODY_ROT};
    cc2x_quat_from_ypr(ypr[0], ypr[1], ypr[2], c.v);
    return ret_bool(L, queue_cmd(&c));
}
static int lf_brakes(cc2x_lua *L) {
    Cmd c = {.type = CMD_BRAKES, .id = (uint32_t)arg_int(L, 1, 0)};
    c.v[0] = arg_value(L, 2);
    c.v[1] = A->lua_type(L, 3) == CC2X_LUA_NONE ? 1.0 : arg_value(L, 3);
    return ret_bool(L, queue_cmd(&c));
}
static int lf_gear(cc2x_lua *L) {
    Cmd c = {.type = CMD_GEAR, .id = (uint32_t)arg_int(L, 1, 0)};
    c.v[0] = A->lua_toboolean(L, 2);
    return ret_bool(L, queue_cmd(&c));
}
/* cc2x.request_landing(vehicle_id, tile_id): result as event "landing" */
static int lf_request_landing(cc2x_lua *L) {
    Cmd c = {.type = CMD_LANDING, .id = (uint32_t)arg_int(L, 1, 0), .id2 = (uint32_t)arg_int(L, 2, 0)};
    return ret_bool(L, queue_cmd(&c));
}
/* cc2x.queue_push / queue_remove(tile_id, airfield_index, "dock" | "undock", vehicle_id) */
static int queue_op(cc2x_lua *L, int type) {
    const char *q = A->lua_tostring(L, 3, NULL);
    int list = !q ? 0 : !strcmp(q, "dock") ? CC2X_AIRFIELD_DOCK_QUEUE : !strcmp(q, "undock") ? CC2X_AIRFIELD_UNDOCK_QUEUE : 0;
    if (!list) return ret_nil(L, "queue must be \"dock\" or \"undock\"");
    Cmd c = {.type = type, .id = (uint32_t)arg_int(L, 1, 0), .af = (uint32_t)arg_int(L, 2, 0), .field = list,
             .id2 = (uint32_t)arg_int(L, 4, 0)};
    return ret_bool(L, queue_cmd(&c));
}
static int lf_queue_push(cc2x_lua *L) { return queue_op(L, CMD_QUEUE_PUSH); }
static int lf_queue_remove(cc2x_lua *L) { return queue_op(L, CMD_QUEUE_REMOVE); }
/* cc2x.raycast(tag, x, y, z, dx, dy, dz, max_dist [, "static" | "dynamic" | "all"]): result as event */
static int lf_raycast(cc2x_lua *L) {
    Cmd c = {.type = CMD_RAYCAST, .tag = arg_int(L, 1, 0)};
    for (int k = 0; k < 7; ++k)
        if (!L_NUM(2 + k, &c.v[k])) return ret_nil(L, "numbers expected");
    const char *f = A->lua_tostring(L, 9, NULL);
    c.v[7] = !f || !strcmp(f, "static") ? CC2X_RAY_STATIC : !strcmp(f, "dynamic") ? CC2X_RAY_DYNAMIC
                                                                                   : CC2X_RAY_STATIC | CC2X_RAY_DYNAMIC;
    return ret_bool(L, queue_cmd(&c));
}

/* ============================================================================ setup */

void lua_world_init(const cc2x_api *api) {
    A = api;
    InitializeCriticalSection(&g_snap_cs);
    InitializeCriticalSection(&g_ev_cs);
    InitializeCriticalSection(&g_cmd_cs);
    InitializeCriticalSection(&g_rule_cs);
    A->on_server_tick(on_tick, NULL);
    A->on_session(on_session, NULL);
    static const struct { const char *name; cc2x_lua_fn fn; } FNS[] = {
        {"vehicles", lf_vehicles}, {"vehicle", lf_vehicle}, {"tiles", lf_tiles}, {"tile", lf_tile},
        {"airfields", lf_airfields}, {"tick", lf_tick}, {"events", lf_events}, {"listen", lf_listen},
        {"island_controller", lf_island_controller}, {"island_spawn", lf_island_spawn}, {"hangar", lf_hangar},
        {"set_vehicle", lf_set_vehicle}, {"set_airfield", lf_set_airfield}, {"set_waypoint", lf_set_waypoint},
        {"clear_waypoints", lf_clear_waypoints}, {"set_position", lf_set_position},
        {"set_velocity", lf_set_velocity}, {"set_spin", lf_set_spin}, {"set_rotation", lf_set_rotation},
        {"brakes", lf_brakes}, {"gear", lf_gear}, {"request_landing", lf_request_landing},
        {"queue_push", lf_queue_push}, {"queue_remove", lf_queue_remove}, {"raycast", lf_raycast},
    };
    for (size_t i = 0; i < sizeof(FNS) / sizeof(FNS[0]); ++i) A->lua_add(NULL, FNS[i].name, FNS[i].fn);
}
