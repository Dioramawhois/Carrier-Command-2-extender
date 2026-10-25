/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * CC2X world API (API 3): the game's live objects as named fields and lists, vehicle physics, the
 * game's PhysX scene query, a few game functions, and game events that need a hook.
 *
 * CC2X alone leaves the game vanilla: an event's hook is installed only when a plugin subscribes,
 * and its dispatcher does exactly what the game did unless a plugin's callback asks otherwise.
 * Everything here runs on the game's logic server thread.
 */
#include <windows.h>
#include <math.h>
#include <string.h>
#include "internal.h"

#define THISCALL __attribute__((thiscall))
#define AT(T, p, o) (*(T *)((char *)(p) + (o)))
#define LOG(level, ...) api_log("cc2x", level, __VA_ARGS__)

/* ============================================================================ layout */

typedef struct {
    const char *build;
    /* game functions */
    uint32_t f_vehicle_by_id, f_tile_by_id, f_tile_at, f_request_landing, f_waypoints_clear, f_deque_grow;
    uint32_t vt_rigid_dynamic;                    /* physx NpRigidDynamic: a vehicle body we may move */
    /* hook sites, see the events */
    uint32_t h_waypoints, h_hangar, h_hangar_found, h_hangar_none, h_controller[2], h_spawn, h_af_tick;
    uint8_t h_waypoints_bytes[6], h_hangar_bytes[5], h_controller_bytes[2][7];
    uint32_t f_spawn, f_af_tick, vt_rules_is_ai;
    /* server */
    uint32_t s_vehicle_map, s_vehicles, s_tile_map, s_tiles, s_broadcast;
    /* vehicle */
    uint32_t v_id, v_definition, v_bodies, v_wheels, v_team, v_attached, v_dock_vehicle, v_dock_tile,
        v_dock_airfield, v_fuel, v_waypoints, v_sync_count, v_attachments, v_flight_state, v_dock_state, v_hp,
        v_team_object, v_control_mode, v_control_target, v_control_speed, v_control_power, v_control_taxi,
        v_sync_data;
    uint32_t vt_is_air, vt_is_wing, vt_is_rotor, vt_brakes, vt_gear;
    uint32_t body_actor, body_pos, team_is_ai, wheel_size, attachment_flying, waypoint_size, wp_type, wp_param;
    /* tile, island, airfield */
    uint32_t t_id, t_pos, t_team, t_saved_count, t_saved_set, t_island, i_airfields;
    uint32_t a_env, a_xform, a_hangars, a_hangar_count, a_mode, a_state, a_runway_vehicle, a_end_node,
        a_start_node, a_path, a_dock_queue, a_undock_queue, a_tile, env_nodes, node_size, node_pos;
} Layout;

static const Layout LAYOUTS[] = {{
    .build = "1.5.18-win32",
    .f_vehicle_by_id = 0x005ee6a0, .f_tile_by_id = 0x00631240, .f_tile_at = 0x00631040,
    .f_request_landing = 0x0062b4a0, .f_waypoints_clear = 0x00854870, .f_deque_grow = 0x00860c90,
    .vt_rigid_dynamic = 0x00cdd478,
    .h_waypoints = 0x007781d4, .h_waypoints_bytes = {0xff, 0xb0, 0x50, 0x01, 0x00, 0x00},
    .h_hangar = 0x0062ae8d, .h_hangar_found = 0x0062aefc, .h_hangar_none = 0x0062aead,
    .h_hangar_bytes = {0x8b, 0x57, 0x6c, 0x33, 0xc0},
    .h_controller = {0x0063014f, 0x00634981},
    .h_controller_bytes = {{0x8b, 0x01, 0x8b, 0x40, 0x10, 0xff, 0xd0}, {0x8b, 0x07, 0x8b, 0x40, 0x10, 0xff, 0xd0}},
    .h_spawn = 0x00634f3d, .f_spawn = 0x0062ca30, .h_af_tick = 0x0062b8f3, .f_af_tick = 0x00628600,
    .vt_rules_is_ai = 0x10,
    .s_vehicle_map = 0x344, .s_vehicles = 0x37c, .s_tile_map = 0x42c, .s_tiles = 0x440, .s_broadcast = 0x514,
    .v_id = 0x80, .v_definition = 0x84, .v_bodies = 0x8c, .v_wheels = 0xac, .v_team = 0x108, .v_attached = 0x10c,
    .v_dock_vehicle = 0x114, .v_dock_tile = 0x118, .v_dock_airfield = 0x11c, .v_fuel = 0x120, .v_waypoints = 0x140,
    .v_sync_count = 0x150, .v_attachments = 0x15c, .v_flight_state = 0x170, .v_dock_state = 0x174, .v_hp = 0x180,
    .v_team_object = 0x1a8, .v_control_mode = 0x1d0, .v_control_target = 0x1d8, .v_control_speed = 0x1f8,
    .v_control_power = 0x1fc, .v_control_taxi = 0x200, .v_sync_data = 0x230,
    .vt_is_air = 0x44, .vt_is_wing = 0x48, .vt_is_rotor = 0x4c, .vt_brakes = 0x94, .vt_gear = 0x98,
    .body_actor = 0xf0, .body_pos = 0x48, .team_is_ai = 0x19, .wheel_size = 0x70, .attachment_flying = 0xc0,
    .waypoint_size = 0x40, .wp_type = 0x04, .wp_param = 0x20,
    .t_id = 0x00, .t_pos = 0x48, .t_team = 0x60, .t_saved_count = 0xcc, .t_saved_set = 0xd0, .t_island = 0x100,
    .i_airfields = 0x154,
    .a_env = 0x00, .a_xform = 0x08, .a_hangars = 0x68, .a_hangar_count = 0x6c, .a_mode = 0x74, .a_state = 0x78,
    .a_runway_vehicle = 0x7c, .a_end_node = 0xb4, .a_start_node = 0xb8, .a_path = 0xbc, .a_dock_queue = 0xcc,
    .a_undock_queue = 0xdc, .a_tile = 0xf0, .env_nodes = 0x180, .node_size = 0x40, .node_pos = 0x34,
}};
static const Layout *W;

/* ============================================================================ helpers */

typedef struct { uint32_t data, cap, head, count; } Deque;   /* the game's ring buffer */

static Deque *dq(const void *obj, uint32_t off) { return (Deque *)((char *)obj + off); }
static int dq_ok(const Deque *q) { return q->data && q->cap && q->head < q->cap && q->count <= q->cap; }
static int dq_count(const Deque *q) { return dq_ok(q) ? (int)q->count : 0; }
static char *dq_item(const Deque *q, int i, uint32_t size) {
    if (!dq_ok(q) || i < 0 || (uint32_t)i >= q->count) return NULL;
    uint32_t s = q->head + (uint32_t)i;
    if (s >= q->cap) s -= q->cap;
    return (char *)(uintptr_t)q->data + size * s;
}
static void *dq_ptr(const Deque *q, int i) {
    char *p = dq_item(q, i, 4);
    return p ? *(void **)p : NULL;
}

static void *vfn(const void *obj, uint32_t off) { return *(void **)(*(char *const *)obj + off); }
typedef char (THISCALL *BoolFn)(const void *self);
static int vbool(const void *obj, uint32_t off) { return ((BoolFn)vfn(obj, off))(obj) != 0; }

static char *srv(void) { return W ? api_server() : NULL; }

/* ============================================================================ objects */

typedef void *(THISCALL *ByIdFn)(void *self, uint32_t id);
typedef void *(THISCALL *AtFn)(void *self, double *pos);

static void *w_find(int list, uint32_t id) {
    char *s = srv();
    if (!s) return NULL;
    if (list == CC2X_WORLD_VEHICLES) return ((ByIdFn)va(W->f_vehicle_by_id))(s + W->s_vehicle_map, id);
    if (list == CC2X_WORLD_TILES) return ((ByIdFn)va(W->f_tile_by_id))(s + W->s_tile_map, id);
    return NULL;
}

static void *w_tile_at(double x, double z) {
    char *s = srv();
    double p[3] = {x, 0.0, z};
    return s ? ((AtFn)va(W->f_tile_at))(s + W->s_tile_map, p) : NULL;
}

static char *main_body(const void *v) { return dq_ptr(dq(v, W->v_bodies), 0); }

static void to_world(const void *af, const double l[3], double w[3]) {
    const double *m = (const double *)((const char *)af + W->a_xform);
    for (int k = 0; k < 3; ++k) w[k] = l[0] * m[k] + l[1] * m[3 + k] + l[2] * m[6 + k] + m[9 + k];
}

static int runway_node(const void *af, uint32_t index_off, double w[3]) {
    char *env = AT(char *, af, W->a_env);
    char *n = env ? dq_item(dq(env, W->env_nodes), (int)AT(uint32_t, af, index_off), W->node_size) : NULL;
    if (!n) return 0;
    const float *p = (const float *)(n + W->node_pos);
    double l[3] = {p[0], p[1], p[2]};
    to_world(af, l, w);
    return 1;
}

static int w_count(const void *o, int list) {
    char *s = srv();
    if (!W) return 0;
    if (list == CC2X_WORLD_VEHICLES) return s ? dq_count(dq(s, W->s_vehicles)) : 0;
    if (list == CC2X_WORLD_TILES) return s ? dq_count(dq(s, W->s_tiles)) : 0;
    if (!o) return 0;
    switch (list) {
    case CC2X_VEHICLE_ATTACHMENTS: return AT(void *, o, W->v_attachments) ? (int)AT(uint32_t, o, W->v_attachments + 4) : 0;
    case CC2X_VEHICLE_WHEELS: return AT(void *, o, W->v_wheels) ? (int)AT(uint32_t, o, W->v_wheels + 4) : 0;
    case CC2X_VEHICLE_WAYPOINTS: return dq_count(dq(o, W->v_waypoints));
    case CC2X_ISLAND_AIRFIELDS: return dq_count(dq(o, W->i_airfields));
    case CC2X_AIRFIELD_HANGARS: return AT(void *, o, W->a_hangars) ? (int)AT(uint32_t, o, W->a_hangar_count) : 0;
    case CC2X_AIRFIELD_TAXI_PATH: return dq_count(dq(o, W->a_path));
    case CC2X_AIRFIELD_DOCK_QUEUE: return dq_count(dq(o, W->a_dock_queue));
    case CC2X_AIRFIELD_UNDOCK_QUEUE: return dq_count(dq(o, W->a_undock_queue));
    }
    return 0;
}

static void *w_ref(const void *o, int f, int i) {
    char *s = srv();
    if (!W) return NULL;
    if (f == CC2X_WORLD_VEHICLES) return s ? dq_ptr(dq(s, W->s_vehicles), i) : NULL;
    if (f == CC2X_WORLD_TILES) return s ? dq_ptr(dq(s, W->s_tiles), i) : NULL;
    if (!o) return NULL;
    switch (f) {
    case CC2X_VEHICLE_WAYPOINTS: return dq_item(dq(o, W->v_waypoints), i, W->waypoint_size);
    case CC2X_TILE_ISLAND: return AT(void *, o, W->t_island);
    case CC2X_ISLAND_AIRFIELDS: return dq_ptr(dq(o, W->i_airfields), i);
    }
    return NULL;
}

/* ---- fields: stored in the object (readable, some writable) or computed (read only) */
enum { T_U8 = 1, T_U32, T_I32, T_F32, T_F64 };
typedef struct { char *p; int type, rw; } Slot;

static int vec(int i) { return i >= 0 && i < 3; }

static int stored(const void *o_, int f, int i, Slot *s) {
    char *o = (char *)o_;
#define AT_(off, t, w) (s->p = o + (off), s->type = (t), s->rw = (w), 1)
    switch (f) {
    case CC2X_VEHICLE_ID: return AT_(W->v_id, T_U32, 0);
    case CC2X_VEHICLE_DEFINITION: return AT_(W->v_definition, T_U32, 0);
    case CC2X_VEHICLE_TEAM: return AT_(W->v_team, T_U32, 0);
    case CC2X_VEHICLE_HP: return AT_(W->v_hp, T_U32, 0);
    case CC2X_VEHICLE_FUEL: return AT_(W->v_fuel, T_F32, 0);
    case CC2X_VEHICLE_ATTACHED_TO: return AT_(W->v_attached, T_U32, 0);
    case CC2X_VEHICLE_DOCK_STATE: return AT_(W->v_dock_state, T_U32, 1);
    case CC2X_VEHICLE_DOCK_VEHICLE: return AT_(W->v_dock_vehicle, T_U32, 1);
    case CC2X_VEHICLE_DOCK_TILE: return AT_(W->v_dock_tile, T_U32, 1);
    case CC2X_VEHICLE_DOCK_AIRFIELD: return AT_(W->v_dock_airfield, T_I32, 1);
    case CC2X_VEHICLE_FLIGHT_STATE: return AT_(W->v_flight_state, T_U32, 1);
    case CC2X_VEHICLE_CONTROL_MODE: return AT_(W->v_control_mode, T_U32, 1);
    case CC2X_VEHICLE_CONTROL_TARGET: return vec(i) && AT_(W->v_control_target + 8 * i, T_F64, 1);
    case CC2X_VEHICLE_CONTROL_SPEED: return AT_(W->v_control_speed, T_F32, 1);
    case CC2X_VEHICLE_CONTROL_POWER: return AT_(W->v_control_power, T_F32, 1);
    case CC2X_VEHICLE_CONTROL_TAXI: return AT_(W->v_control_taxi, T_U8, 1);
    case CC2X_VEHICLE_ATTACHMENT_FLYING: {
        char **att = AT(char **, o, W->v_attachments);
        if (!att || i < 0 || (uint32_t)i >= AT(uint32_t, o, W->v_attachments + 4) || !att[i]) return 0;
        o = att[i];
        return AT_(W->attachment_flying, T_U8, 1);
    }
    case CC2X_VEHICLE_WHEEL_CONTACT: {
        char *w = AT(char *, o, W->v_wheels);
        if (!w || i < 0 || (uint32_t)i >= AT(uint32_t, o, W->v_wheels + 4)) return 0;
        o = w;
        return AT_(W->wheel_size * (uint32_t)i, T_F32, 0);
    }
    case CC2X_WAYPOINT_TYPE: return AT_(W->wp_type, T_U32, 1);
    case CC2X_WAYPOINT_PARAM: return AT_(W->wp_param, T_U32, 1);
    case CC2X_TILE_ID: return AT_(W->t_id, T_U32, 0);
    case CC2X_TILE_TEAM: return AT_(W->t_team, T_U32, 0);
    case CC2X_TILE_POS: return vec(i) && AT_(W->t_pos + 8 * i, T_F64, 0);
    case CC2X_AIRFIELD_TILE: return AT_(W->a_tile, T_U32, 0);
    case CC2X_AIRFIELD_TRANSFORM: return i >= 0 && i < 12 && AT_(W->a_xform + 8 * i, T_F64, 0);
    case CC2X_AIRFIELD_STATE: return AT_(W->a_state, T_U32, 1);
    case CC2X_AIRFIELD_MODE: return AT_(W->a_mode, T_U32, 1);
    case CC2X_AIRFIELD_RUNWAY_VEHICLE: return AT_(W->a_runway_vehicle, T_I32, 1);
    case CC2X_AIRFIELD_HANGARS: {
        char *h = AT(char *, o, W->a_hangars);
        if (!h || i < 0 || (uint32_t)i >= AT(uint32_t, o, W->a_hangar_count)) return 0;
        o = h;
        return AT_(4 * (uint32_t)i, T_U32, 1);
    }
    case CC2X_AIRFIELD_DOCK_QUEUE:
    case CC2X_AIRFIELD_UNDOCK_QUEUE: {
        char *p = dq_item(dq(o, f == CC2X_AIRFIELD_DOCK_QUEUE ? W->a_dock_queue : W->a_undock_queue), i, 4);
        if (!p) return 0;
        o = p;
        return AT_(0, T_U32, 1);
    }
    }
#undef AT_
    return 0;
}

static int computed(const void *o, int f, int i, double *out) {
    switch (f) {
    case CC2X_VEHICLE_TEAM_IS_AI: {
        char *team = AT(char *, o, W->v_team_object);
        *out = team && AT(uint8_t, team, W->team_is_ai);
        return 1;
    }
    case CC2X_VEHICLE_IS_AIR: *out = vbool(o, W->vt_is_air); return 1;
    case CC2X_VEHICLE_IS_WING: *out = vbool(o, W->vt_is_wing); return 1;
    case CC2X_VEHICLE_IS_ROTOR: *out = vbool(o, W->vt_is_rotor); return 1;
    case CC2X_VEHICLE_POS: {
        char *b = main_body(o);
        if (!b || !vec(i)) return 0;
        *out = AT(double, b, W->body_pos + 8 * i);
        return 1;
    }
    case CC2X_TILE_AIRFIELDS: {
        char *island = AT(char *, o, W->t_island);
        if (island) *out = dq_count(dq(island, W->i_airfields));
        else *out = AT(uint8_t, o, W->t_saved_set) ? AT(int32_t, o, W->t_saved_count) : -1;
        return 1;
    }
    case CC2X_AIRFIELD_RUNWAY_START:
    case CC2X_AIRFIELD_RUNWAY_END: {
        double w[3];
        if (!vec(i) || !runway_node(o, f == CC2X_AIRFIELD_RUNWAY_START ? W->a_start_node : W->a_end_node, w)) return 0;
        *out = w[i];
        return 1;
    }
    }
    return 0;
}

static int field_get(const void *o, int f, int i, double *out) {
    Slot s;
    if (!W || !o) return 0;
    if (!stored(o, f, i, &s)) return computed(o, f, i, out);
    switch (s.type) {
    case T_U8: *out = *(uint8_t *)s.p; break;
    case T_U32: *out = *(uint32_t *)s.p; break;
    case T_I32: *out = *(int32_t *)s.p; break;
    case T_F32: *out = *(float *)s.p; break;
    default: *out = *(double *)s.p; break;
    }
    return 1;
}

static int field_set(void *o, int f, int i, double v) {
    Slot s;
    if (!W || !o || !stored(o, f, i, &s) || !s.rw) return 0;
    switch (s.type) {
    case T_U8: *(uint8_t *)s.p = (uint8_t)(int64_t)v; break;
    case T_U32: *(uint32_t *)s.p = (uint32_t)(int64_t)v; break;
    case T_I32: *(int32_t *)s.p = (int32_t)(int64_t)v; break;
    case T_F32: *(float *)s.p = (float)v; break;
    default: *(double *)s.p = v; break;
    }
    return 1;
}

static int64_t w_geti(const void *o, int f, int i) { double d; return field_get(o, f, i, &d) ? (int64_t)d : 0; }
static double w_getf(const void *o, int f, int i) { double d; return field_get(o, f, i, &d) ? d : 0.0; }
static int w_seti(void *o, int f, int i, int64_t v) { return field_set(o, f, i, (double)v); }
static int w_setf(void *o, int f, int i, double v) { return field_set(o, f, i, v); }

/* ---- id lists */
typedef void (THISCALL *GrowFn)(void *self, uint32_t cap);
typedef void (THISCALL *VoidFn)(void *self);

static Deque *id_list(void *o, int list) {
    if (!W || !o) return NULL;
    if (list == CC2X_AIRFIELD_DOCK_QUEUE) return dq(o, W->a_dock_queue);
    if (list == CC2X_AIRFIELD_UNDOCK_QUEUE) return dq(o, W->a_undock_queue);
    return NULL;
}

static int w_list_remove(void *o, int list, int index) {
    Deque *q = id_list(o, list);
    if (!q || !dq_item(q, index, 4)) return 0;
    for (int j = index; j + 1 < (int)q->count; ++j) *(uint32_t *)dq_item(q, j, 4) = *(uint32_t *)dq_item(q, j + 1, 4);
    q->count--;
    return 1;
}

static int w_list_push(void *o, int list, int64_t value) {
    Deque *q = id_list(o, list);
    if (!q) return 0;
    if (q->count >= q->cap) ((GrowFn)va(W->f_deque_grow))(q, q->cap * 2 < 0x20 ? 0x20 : q->cap * 2);
    if (!q->data || q->count >= q->cap) return 0;
    q->count++;
    *(uint32_t *)dq_item(q, (int)q->count - 1, 4) = (uint32_t)value;
    return 1;
}

static int w_list_clear(void *o, int list) {
    if (!W || !o) return 0;
    if (list == CC2X_VEHICLE_WAYPOINTS) { ((VoidFn)va(W->f_waypoints_clear))(dq(o, W->v_waypoints)); return 1; }
    if (list == CC2X_AIRFIELD_TAXI_PATH) { dq(o, W->a_path)->count = 0; return 1; }
    Deque *q = id_list(o, list);
    if (!q) return 0;
    q->count = 0;
    return 1;
}

/* ============================================================================ physics */

typedef struct { float q[4]; float p[3]; } PxTransform;
typedef PxTransform *(THISCALL *GetPoseFn)(void *self, PxTransform *out);
typedef void (THISCALL *SetPoseFn)(void *self, const PxTransform *pose, int wake);
typedef float *(THISCALL *GetVecFn)(void *self, float *out);
typedef void (THISCALL *SetVecFn)(void *self, const float *v, int wake);
typedef void *(THISCALL *GetSceneFn)(void *self);
typedef uint8_t (THISCALL *RaycastFn)(void *scene, const float *origin, const float *dir, float dist, void *hits,
                                      uint32_t hit_flags, const void *filter, void *filter_cb, const void *cache);
enum {   /* PxRigidDynamic and PxScene virtual functions */
    PX_GET_SCENE = 6 * 4, PX_GET_POSE = 18 * 4, PX_SET_POSE = 19 * 4, PX_GET_VEL = 38 * 4, PX_SET_VEL = 39 * 4,
    PX_GET_SPIN = 40 * 4, PX_SET_SPIN = 41 * 4, PX_SCENE_RAYCAST = 0x158,
};

static char *body_actor(const void *v) {
    char *b = main_body(v);
    char *a = b ? AT(char *, b, W->body_actor) : NULL;
    return a && AT(uint32_t, a, 0) == va(W->vt_rigid_dynamic) ? a : NULL;
}

static int w_body_get(const void *v, cc2x_body *o) {
    char *b, *a;
    if (!W || !v || !o || !(b = main_body(v)) || !(a = body_actor(v))) return 0;
    PxTransform pose;
    float f[3];
    ((GetPoseFn)vfn(a, PX_GET_POSE))(a, &pose);
    for (int k = 0; k < 4; ++k) o->rot[k] = pose.q[k];
    for (int k = 0; k < 3; ++k) o->pos[k] = AT(double, b, W->body_pos + 8 * k);
    ((GetVecFn)vfn(a, PX_GET_VEL))(a, f);
    for (int k = 0; k < 3; ++k) o->vel[k] = f[k];
    ((GetVecFn)vfn(a, PX_GET_SPIN))(a, f);
    for (int k = 0; k < 3; ++k) o->spin[k] = f[k];
    return 1;
}

static int w_body_set(void *v, const cc2x_body *in, int what) {
    char *b, *a;
    if (!W || !v || !in || !(b = main_body(v)) || !(a = body_actor(v))) return 0;
    if (what & (CC2X_BODY_POS | CC2X_BODY_ROT)) {
        PxTransform pose;
        ((GetPoseFn)vfn(a, PX_GET_POSE))(a, &pose);
        if (what & CC2X_BODY_POS)
            for (int k = 0; k < 3; ++k) {   /* PhysX works around a floating origin: keep its offset */
                double *wp = &AT(double, b, W->body_pos + 8 * k);
                pose.p[k] = (float)(pose.p[k] + (in->pos[k] - *wp));
                *wp = in->pos[k];             /* the game copies it from PhysX next tick anyway */
            }
        if (what & CC2X_BODY_ROT) {
            double n = sqrt(in->rot[0] * in->rot[0] + in->rot[1] * in->rot[1] + in->rot[2] * in->rot[2] + in->rot[3] * in->rot[3]);
            if (n < 1e-9) return 0;
            for (int k = 0; k < 4; ++k) pose.q[k] = (float)(in->rot[k] / n);
        }
        ((SetPoseFn)vfn(a, PX_SET_POSE))(a, &pose, 1);
    }
    if (what & CC2X_BODY_VEL) {
        float f[3] = {(float)in->vel[0], (float)in->vel[1], (float)in->vel[2]};
        ((SetVecFn)vfn(a, PX_SET_VEL))(a, f, 1);
    }
    if (what & CC2X_BODY_SPIN) {
        float f[3] = {(float)in->spin[0], (float)in->spin[1], (float)in->spin[2]};
        ((SetVecFn)vfn(a, PX_SET_SPIN))(a, f, 1);
    }
    return 1;
}

/* ---- scene query. The scene and its floating-origin offset (world = PhysX + shift) come from a
 * vehicle body; checked once per session that all vehicles share one scene, else the scene of the
 * vehicle nearest to the ray is used. */
typedef struct { void *scene; double shift[3]; } Scene;
static Scene g_scene;
static int g_scene_tick_ok, g_scene_checked, g_scene_multi;

static int scene_of(const void *v, Scene *s) {
    char *a = body_actor(v), *b = main_body(v);
    if (!a || !b || !(s->scene = ((GetSceneFn)vfn(a, PX_GET_SCENE))(a))) return 0;
    PxTransform pose;
    ((GetPoseFn)vfn(a, PX_GET_POSE))(a, &pose);
    for (int k = 0; k < 3; ++k) s->shift[k] = AT(double, b, W->body_pos + 8 * k) - pose.p[k];
    return 1;
}

static const Scene *ray_scene(const double at[3], Scene *tmp) {
    Deque *q = dq(srv(), W->s_vehicles);
    int n = dq_count(q);
    if (!g_scene_checked) {
        g_scene_checked = 1;
        void *first = NULL;
        for (int i = 0; i < n; ++i) {
            Scene t;
            if (!scene_of(dq_ptr(q, i), &t)) continue;
            if (!first) first = t.scene;
            else if (t.scene != first) g_scene_multi = 1;
        }
        if (g_scene_multi) LOG(CC2X_LOG_INFO, "several PhysX scenes: ray casts use the one of the nearest vehicle");
    }
    if (g_scene_multi) {
        double best = 1e30;
        const void *closest = NULL;
        for (int i = 0; i < n; ++i) {
            char *v = dq_ptr(q, i), *b = v ? main_body(v) : NULL;
            if (!b || !body_actor(v)) continue;
            double dx = AT(double, b, W->body_pos) - at[0], dz = AT(double, b, W->body_pos + 16) - at[2];
            if (dx * dx + dz * dz < best) { best = dx * dx + dz * dz; closest = v; }
        }
        return closest && scene_of(closest, tmp) ? tmp : NULL;
    }
    for (int i = 0; !g_scene_tick_ok && i < n; ++i) g_scene_tick_ok = scene_of(dq_ptr(q, i), &g_scene);
    return g_scene_tick_ok ? &g_scene : NULL;
}

static uint8_t THISCALL ray_touches(void *self, const void *hits, uint32_t n) { return 0; }
static void THISCALL ray_finalize(void *self) {}
static void *THISCALL ray_dtor(void *self, uint32_t flags) { return self; }
static void *g_ray_vt[3];                           /* PxHitCallback<PxRaycastHit> */

static int w_raycast(const double from[3], const double dir[3], double max_dist, int flags, cc2x_hit *hit) {
    Scene tmp;
    const Scene *sc;
    double len = dir ? sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]) : 0.0;
    if (!from || len < 1e-9 || max_dist <= 0.0 || !srv() || !(sc = ray_scene(from, &tmp))) return 0;
    float o[3], d[3];
    for (int k = 0; k < 3; ++k) { o[k] = (float)(from[k] - sc->shift[k]); d[k] = (float)(dir[k] / len); }
    struct { uint32_t data[4]; uint16_t flags; } filter = {{0, 0, 0, 0}, 0};   /* PxQueryFilterData */
    if (flags & CC2X_RAY_STATIC) filter.flags |= 1;
    if (flags & CC2X_RAY_DYNAMIC) filter.flags |= 2;
    if (!filter.flags) filter.flags = 1;
    char h[0x54];                                    /* PxRaycastBuffer: vtable, block hit, touches */
    memset(h, 0, sizeof(h));
    g_ray_vt[0] = (void *)ray_touches; g_ray_vt[1] = (void *)ray_finalize; g_ray_vt[2] = (void *)ray_dtor;
    *(void ***)h = g_ray_vt;
    *(uint32_t *)(h + 0x0c) = 0xffffffff;            /* faceIndex */
    *(float *)(h + 0x2c) = 3.4e38f;                  /* distance */
    ((RaycastFn)vfn(sc->scene, PX_SCENE_RAYCAST))(sc->scene, o, d, (float)max_dist, h, 0x403, &filter, NULL, NULL);
    if (!h[0x44]) return 0;                          /* hasBlock */
    if (hit) {
        for (int k = 0; k < 3; ++k) {
            hit->pos[k] = *(float *)(h + 0x14 + 4 * k) + sc->shift[k];
            hit->normal[k] = *(float *)(h + 0x20 + 4 * k);
        }
        hit->distance = *(float *)(h + 0x2c);
    }
    return 1;
}

/* ============================================================================ game functions */

typedef void (THISCALL *BrakesFn)(void *self, float brake, float grip);
typedef void (THISCALL *GearFn)(void *self, int down);
typedef void (THISCALL *Vehicle1Fn)(void *self, void *vehicle);
typedef void (THISCALL *BroadcastFn)(void *self, uint32_t id, void *a, void *b, uint32_t c);

static void w_brakes(void *v, float brake, float grip) { if (W && v) ((BrakesFn)vfn(v, W->vt_brakes))(v, brake, grip); }
static void w_gear(void *v, int down) { if (W && v) ((GearFn)vfn(v, W->vt_gear))(v, down); }

static int w_request_landing(void *island, void *v) {
    if (!W || !island || !v) return 0;
    ((Vehicle1Fn)va(W->f_request_landing))(dq(island, W->i_airfields), v);
    return AT(uint32_t, v, W->v_dock_state) == CC2X_DOCK_QUEUED;
}

static void w_waypoints_sync(void *v) {
    char *s = srv();
    if (!s || !v) return;
    void *obj = s + W->s_broadcast;
    ((BroadcastFn)vfn(obj, 0))(obj, AT(uint32_t, v, W->v_id), (char *)v + W->v_sync_data, dq(v, W->v_waypoints),
                               AT(uint32_t, v, W->v_sync_count));
}

static void w_to_world(const void *af, const double l[3], double w[3]) {
    if (W && af) to_world(af, l, w);
    else w[0] = w[1] = w[2] = 0.0;
}

/* Hangar bays of the airfield prefab (the same on every island; measured where the game parks AI
 * aircraft): slot 0 (-20, -20), 1 (+20, -20), 2 (-20, +20), 3 (+20, +20) in airfield coordinates,
 * nose towards the taxi lane between them (slots 0 and 2 face +x, 1 and 3 face -x). */
static int w_airfield_bay(const void *af, int slot, double pos[3], double *yaw) {
    if (!W || !af || slot < 0 || slot > 3) return 0;
    double l[3] = {(slot & 1) ? 20.0 : -20.0, 0.0, (slot & 2) ? 20.0 : -20.0};
    const double *m = (const double *)((const char *)af + W->a_xform);
    double sgn = (slot & 1) ? -1.0 : 1.0;
    if (pos) to_world(af, l, pos);
    if (yaw) *yaw = atan2(sgn * m[0], sgn * m[2]);
    return 1;
}

/* ============================================================================ events */

#define MAX_SUBS 16
typedef struct { void *fn, *user; } Sub;
typedef struct {
    const char *name;
    int (*install)(void);
    int installed, n;
    Sub sub[MAX_SUBS];
} Event;

static int subscribe(Event *e, void *fn, void *user) {
    if (!W || !fn || e->n >= MAX_SUBS) return 0;
    if (!e->installed) {
        if (!core_patch(e->install)) {
            LOG(CC2X_LOG_ERROR, "event %s: hook not installed", e->name);
            return 0;
        }
        e->installed = 1;
        LOG(CC2X_LOG_INFO, "event %s: hook installed", e->name);
    }
    e->sub[e->n].fn = fn;
    e->sub[e->n].user = user;
    MemoryBarrier();
    e->n++;
    return 1;
}
#define EACH(e, i) for (int i = 0; i < (e).n; ++i)

/* ---- the server's "set vehicle waypoints" handler, at its end: eax = vehicle */
static int add_waypoints(void);
static Event ev_waypoints = {.name = "waypoints", .install = add_waypoints};
typedef void (*WaypointsFn)(void *vehicle, void *user);

static void hk_waypoints(cc2x_cpu *cpu, void *user) {
    void *v = (void *)(uintptr_t)cpu->eax;
    if (v) EACH(ev_waypoints, i) ((WaypointsFn)ev_waypoints.sub[i].fn)(v, ev_waypoints.sub[i].user);
}
static int add_waypoints(void) { return patch_mid(W->h_waypoints, 6, W->h_waypoints_bytes, hk_waypoints, NULL); }

/* ---- island airfield tick (the call from the island controller's tick) */
static int add_af_tick(void);
static Event ev_af_tick = {.name = "airfield_tick", .install = add_af_tick};
typedef void (THISCALL *AfTickFn)(void *self, int a, int b, int c);
typedef void (*AirfieldFn)(void *airfield, void *user);
static AfTickFn o_af_tick;

static void THISCALL hk_af_tick(void *af, int a, int b, int c) {
    o_af_tick(af, a, b, c);
    EACH(ev_af_tick, i) ((AirfieldFn)ev_af_tick.sub[i].fn)(af, ev_af_tick.sub[i].user);
}
static int add_af_tick(void) {
    uint8_t call[5] = {0xe8};
    int32_t rel = (int32_t)(W->f_af_tick - (W->h_af_tick + 5));
    memcpy(call + 1, &rel, 4);
    return patch_call(W->h_af_tick, 5, call, (void *)hk_af_tick, (void **)&o_af_tick);
}

/* ---- hangar slot for a landing aircraft: edi = airfield, esi = vehicle. The game takes the first
 * slot holding an id (its own aircraft are registered there); the result goes to eax with edx = the
 * slot count, continuing at "found" or "none" */
static int add_hangar(void);
static Event ev_hangar = {.name = "hangar_choice", .install = add_hangar};
typedef int (*HangarFn)(void *airfield, void *vehicle, void *user);

static void hk_hangar(cc2x_cpu *cpu, void *user) {
    char *af = (char *)(uintptr_t)cpu->edi;
    void *v = (void *)(uintptr_t)cpu->esi;
    uint32_t n = AT(uint32_t, af, W->a_hangar_count), *h = AT(uint32_t *, af, W->a_hangars);
    int found = -1;
    if (!h) n = 0;
    for (int i = 0; i < ev_hangar.n && n && found < 0; ++i) {
        int r = ((HangarFn)ev_hangar.sub[i].fn)(af, v, ev_hangar.sub[i].user);
        if (r >= 0 && (uint32_t)r < n) found = r;
    }
    for (uint32_t k = 0; k < n && found < 0; ++k)
        if (h[k]) found = (int)k;
    cpu->edx = n;
    cpu->eax = found < 0 ? 0 : (uint32_t)found;
    cpu->jump_to = va(found < 0 ? W->h_hangar_none : W->h_hangar_found);
}
static int add_hangar(void) { return patch_mid(W->h_hangar, 5, W->h_hangar_bytes, hk_hangar, NULL); }

/* ---- "is the island's team AI?" at the two places that create island controllers */
static int add_controller(void);
static Event ev_controller = {.name = "island_controller", .install = add_controller};
typedef char (THISCALL *IsAiFn)(void *self, int team);
typedef int (*ControllerFn)(int team, void *user);

static char THISCALL hk_controller(void *rules, int team) {
    char r = ((IsAiFn)vfn(rules, W->vt_rules_is_ai))(rules, team);
    for (int i = 0; i < ev_controller.n && !r; ++i)
        if (((ControllerFn)ev_controller.sub[i].fn)(team, ev_controller.sub[i].user)) r = 1;
    return r;
}
static int add_controller(void) {
    return patch_call(W->h_controller[0], 7, W->h_controller_bytes[0], (void *)hk_controller, NULL) &&
           patch_call(W->h_controller[1], 7, W->h_controller_bytes[1], (void *)hk_controller, NULL);
}

/* ---- an island controller spawns the island's defence units: (tile, a2, team object, ...) */
static int add_spawn(void);
static Event ev_spawn = {.name = "island_spawn", .install = add_spawn};
typedef int (THISCALL *SpawnFn)(void *self, void *tile, void *a2, void *team, void *a4, void *a5);
typedef int (*IslandSpawnFn)(void *tile, int ai_team, void *user);
static SpawnFn o_spawn;

static int THISCALL hk_spawn(void *self, void *tile, void *a2, void *team, void *a4, void *a5) {
    int ai = !team || AT(uint8_t, team, W->team_is_ai) != 0;
    for (int i = 0; i < ev_spawn.n; ++i)
        if (!((IslandSpawnFn)ev_spawn.sub[i].fn)(tile, ai, ev_spawn.sub[i].user)) return 0;
    return o_spawn(self, tile, a2, team, a4, a5);
}
static int add_spawn(void) {
    uint8_t call[5] = {0xe8};
    int32_t rel = (int32_t)(W->f_spawn - (W->h_spawn + 5));
    memcpy(call + 1, &rel, 4);
    return patch_call(W->h_spawn, 5, call, (void *)hk_spawn, (void **)&o_spawn);
}

static int w_on_waypoints(void (*fn)(void *, void *), void *user) { return subscribe(&ev_waypoints, (void *)fn, user); }
static int w_on_af_tick(void (*fn)(void *, void *), void *user) { return subscribe(&ev_af_tick, (void *)fn, user); }
static int w_on_hangar(int (*fn)(void *, void *, void *), void *user) { return subscribe(&ev_hangar, (void *)fn, user); }
static int w_on_controller(int (*fn)(int, void *), void *user) { return subscribe(&ev_controller, (void *)fn, user); }
static int w_on_spawn(int (*fn)(void *, int, void *), void *user) { return subscribe(&ev_spawn, (void *)fn, user); }

/* ============================================================================ setup */

int world_init(const char *build) {
    for (size_t i = 0; i < sizeof(LAYOUTS) / sizeof(LAYOUTS[0]); ++i)
        if (!strcmp(LAYOUTS[i].build, build)) W = &LAYOUTS[i];
    return W != NULL;
}

void world_session(void) {
    g_scene_tick_ok = g_scene_checked = g_scene_multi = 0;
}

void world_tick(void) { g_scene_tick_ok = 0; }

void world_fill(cc2x_api *a) {   /* every function checks W: without a layout they do nothing */
    a->find = w_find; a->tile_at = w_tile_at; a->count = w_count; a->ref = w_ref;
    a->geti = w_geti; a->getf = w_getf; a->seti = w_seti; a->setf = w_setf;
    a->list_remove = w_list_remove; a->list_push = w_list_push; a->list_clear = w_list_clear;
    a->body_get = w_body_get; a->body_set = w_body_set; a->raycast = w_raycast;
    a->vehicle_brakes = w_brakes; a->vehicle_gear = w_gear; a->request_landing = w_request_landing;
    a->waypoints_sync = w_waypoints_sync; a->airfield_to_world = w_to_world; a->airfield_bay = w_airfield_bay;
    a->on_waypoints = w_on_waypoints; a->on_airfield_tick = w_on_af_tick; a->on_hangar_choice = w_on_hangar;
    a->on_island_controller = w_on_controller; a->on_island_spawn = w_on_spawn;
}
