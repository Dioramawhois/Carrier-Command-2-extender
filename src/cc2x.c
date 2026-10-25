/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * CC2X core: dinput8 proxy, game build check, patch engine, server events, game Lua bridge,
 * and plugin loader (the live world API is in world.c, its Lua side in lua_world.c). One DLL, dropped next to
 * carrier_command.exe.
 */
#include <windows.h>
#include <tlhelp32.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "internal.h"

#define CC2X_VERSION "0.5.0"

/* ============================================================================ game profile */

typedef struct {
    const char *build;
    uint32_t timestamp, image_size;
    uint32_t server_vtable;                      /* init, tick, end */
    uint32_t server_init, server_tick, server_end;
    uint32_t lua_site; uint8_t lua_site_bytes[8];
    uint32_t f_index2addr, f_auxsetstr, f_finishset, f_luaS_new, f_luaH_new, f_luaC_step, nilobject;
} Profile;

static const Profile PROFILES[] = {{
    "1.5.18-win32", 0x6a36c177, 0x00af7000,
    0x00d34bc4, 0x00772fd0, 0x0077ee10, 0x0077eea0,
    0x004465ec, {0x6a, 0x20, 0x89, 0x85, 0x64, 0xff, 0xff, 0xff},
    0x008cda70, 0x008ce200, 0x008ff6b0, 0x00908220, 0x00904a50, 0x00904050, 0x00d03518,
}};

static const struct { int field; uint8_t bytes[8]; } LUA_SIGS[] = {
    {0, {0x8b, 0x41, 0x14, 0x85, 0xd2, 0x7e, 0x0d, 0xc1}},
    {1, {0x55, 0x8b, 0xec, 0x83, 0xec, 0x08, 0x53, 0x56}},
    {2, {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x83, 0xec}},
    {3, {0x55, 0x8b, 0xec, 0x83, 0xec, 0x08, 0x53, 0x8b}},
    {4, {0x56, 0x8b, 0x71, 0x10, 0x33, 0xd2, 0x6a, 0x20}},
    {5, {0x53, 0x8b, 0xd9, 0x56, 0x57, 0x8b, 0x7b, 0x10}},
};

static const Profile *P;
static uintptr_t g_base;
static HMODULE g_self;
static char g_dir[MAX_PATH];
static char g_log_path[MAX_PATH];
static CRITICAL_SECTION g_log_cs;

uintptr_t va(uint32_t image_va) { return image_va - 0x400000u + g_base; }

/* ============================================================================ log */

static void logv(const char *who, int level, const char *fmt, va_list ap) {
    static const char *names[] = {"debug", "info", "warn", "error"};
    char text[1024];
    vsnprintf(text, sizeof(text), fmt, ap);
    SYSTEMTIME t;
    GetLocalTime(&t);
    EnterCriticalSection(&g_log_cs);
    FILE *f = fopen(g_log_path, "ab");
    if (f) {
        fprintf(f, "%02u:%02u:%02u.%03u [%s] %s: %s\r\n", t.wHour, t.wMinute, t.wSecond,
                t.wMilliseconds, who ? who : "?", names[level & 3], text);
        fclose(f);
    }
    LeaveCriticalSection(&g_log_cs);
}

void api_log(const char *who, int level, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    logv(who, level, fmt, ap);
    va_end(ap);
}
#define LOG(level, ...) api_log("cc2x", level, __VA_ARGS__)

/* ============================================================================ dinput8 proxy */

static HMODULE g_real;
static INIT_ONCE g_real_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK load_real(PINIT_ONCE o, PVOID p, PVOID *c) {
    (void)o; (void)p; (void)c;
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n && n < MAX_PATH - 14) {
        wcscat(path, L"\\dinput8.dll");
        g_real = LoadLibraryW(path);
    }
    return TRUE;
}

static FARPROC real(const char *name) {
    InitOnceExecuteOnce(&g_real_once, load_real, NULL, NULL);
    return g_real ? GetProcAddress(g_real, name) : NULL;
}

HRESULT WINAPI DirectInput8Create(HINSTANCE h, DWORD v, const GUID *iid, void **out, void *outer) {
    typedef HRESULT (WINAPI *Fn)(HINSTANCE, DWORD, const GUID *, void **, void *);
    Fn fn = (Fn)(uintptr_t)real("DirectInput8Create");
    return fn ? fn(h, v, iid, out, outer) : E_FAIL;
}
HRESULT WINAPI DllCanUnloadNow(void) {
    typedef HRESULT (WINAPI *Fn)(void);
    Fn fn = (Fn)(uintptr_t)real("DllCanUnloadNow");
    return fn ? fn() : S_FALSE;
}
HRESULT WINAPI DllGetClassObject(const GUID *c, const GUID *i, void **o) {
    typedef HRESULT (WINAPI *Fn)(const GUID *, const GUID *, void **);
    Fn fn = (Fn)(uintptr_t)real("DllGetClassObject");
    return fn ? fn(c, i, o) : E_FAIL;
}
HRESULT WINAPI DllRegisterServer(void) {
    typedef HRESULT (WINAPI *Fn)(void);
    Fn fn = (Fn)(uintptr_t)real("DllRegisterServer");
    return fn ? fn() : E_FAIL;
}
HRESULT WINAPI DllUnregisterServer(void) {
    typedef HRESULT (WINAPI *Fn)(void);
    Fn fn = (Fn)(uintptr_t)real("DllUnregisterServer");
    return fn ? fn() : E_FAIL;
}

/* ============================================================================ patch engine */

#define MAX_PATCHES 128
#define MAX_PATCH_LEN 32

typedef struct {
    uintptr_t addr;
    uint32_t len;
    uint8_t orig[MAX_PATCH_LEN], repl[MAX_PATCH_LEN];
    const char *owner;
} PatchRec;

static PatchRec g_done[MAX_PATCHES], g_pending[MAX_PATCHES];
static int g_ndone, g_npending, g_pending_failed;
static const char *g_owner = "?";

static uint8_t *g_exec, *g_exec_end;

static void *exec_alloc(size_t n) {
    n = (n + 15) & ~(size_t)15;
    if (!g_exec || g_exec + n > g_exec_end) {
        g_exec = VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!g_exec) return NULL;
        g_exec_end = g_exec + 0x10000;
    }
    void *p = g_exec;
    g_exec += n;
    return p;
}

static void put_rel32(uint8_t *at, uint8_t op, uintptr_t from, uintptr_t to) {
    at[0] = op;
    int32_t rel = (int32_t)(to - (from + 5));
    memcpy(at + 1, &rel, 4);
}

static int readable(uintptr_t a, size_t n) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void *)a, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return a + n <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

static void patch_begin(const char *plugin) {
    g_npending = 0;
    g_pending_failed = 0;
    g_owner = plugin ? plugin : "?";
}

static PatchRec *pending_add(uintptr_t addr, uint32_t len, const uint8_t *expected) {
    if (g_npending >= MAX_PATCHES || len == 0 || len > MAX_PATCH_LEN || !readable(addr, len)) {
        g_pending_failed = 1;
        return NULL;
    }
    if (expected && memcmp((void *)addr, expected, len) != 0) {
        api_log(g_owner, CC2X_LOG_ERROR, "unexpected original bytes at 0x%08x (game build differs?)",
                (unsigned)(addr - g_base + 0x400000));
        g_pending_failed = 1;
        return NULL;
    }
    PatchRec *r = &g_pending[g_npending++];
    r->addr = addr;
    r->len = len;
    r->owner = g_owner;
    memcpy(r->orig, (void *)addr, len);
    return r;
}

int patch_call(uint32_t site, uint32_t len, const uint8_t *expected, void *fn, void **original) {
    uintptr_t a = va(site);
    if (len < 5) { g_pending_failed = 1; return 0; }
    PatchRec *r = pending_add(a, len, expected);
    if (!r) return 0;
    if (original) {
        *original = NULL;
        if (r->orig[0] == 0xE8) {
            int32_t rel;
            memcpy(&rel, r->orig + 1, 4);
            *original = (void *)(a + 5 + rel);
        }
    }
    put_rel32(r->repl, 0xE8, a, (uintptr_t)fn);
    memset(r->repl + 5, 0x90, len - 5);
    return 1;
}

typedef struct MidHook {
    cc2x_mid_fn fn;
    void *user;
    uintptr_t thunk;
} MidHook;

static void __cdecl mid_dispatch(cc2x_cpu *cpu, MidHook *h) {
    h->fn(cpu, h->user);
    if (!cpu->jump_to) cpu->jump_to = h->thunk;
}

int patch_mid(uint32_t site, uint32_t len, const uint8_t *expected, cc2x_mid_fn fn, void *user) {
    uintptr_t a = va(site);
    if (len < 5 || !fn) { g_pending_failed = 1; return 0; }
    PatchRec *r = pending_add(a, len, expected);
    if (!r) return 0;
    MidHook *h = exec_alloc(sizeof(MidHook));
    uint8_t *thunk = exec_alloc(len + 5);
    uint8_t *stub = exec_alloc(32);
    if (!h || !thunk || !stub) { g_npending--; g_pending_failed = 1; return 0; }
    /* thunk: displaced bytes (first rel32 call/jmp relocated) + jmp back */
    memcpy(thunk, r->orig, len);
    if (r->orig[0] == 0xE8 || r->orig[0] == 0xE9) {
        int32_t rel;
        memcpy(&rel, r->orig + 1, 4);
        put_rel32(thunk, r->orig[0], (uintptr_t)thunk, a + 5 + rel);
    }
    put_rel32(thunk + len, 0xE9, (uintptr_t)(thunk + len), a + len);
    h->fn = fn;
    h->user = user;
    h->thunk = (uintptr_t)thunk;
    /* stub: push 0 (jump_to); pushfd; pushad; mov eax,esp; push h; push eax;
     *       call mid_dispatch; add esp,8; popad; popfd; ret (-> jump_to) */
    uint8_t *s = stub;
    *s++ = 0x6A; *s++ = 0x00;
    *s++ = 0x9C;
    *s++ = 0x60;
    *s++ = 0x89; *s++ = 0xE0;
    *s++ = 0x68; memcpy(s, &h, 4); s += 4;
    *s++ = 0x50;
    put_rel32(s, 0xE8, (uintptr_t)s, (uintptr_t)mid_dispatch); s += 5;
    *s++ = 0x83; *s++ = 0xC4; *s++ = 0x08;
    *s++ = 0x61;
    *s++ = 0x9D;
    *s++ = 0xC3;
    put_rel32(r->repl, 0xE9, a, (uintptr_t)stub);
    memset(r->repl + 5, 0x90, len - 5);
    return 1;
}

static int patch_ptr(uint32_t slot, uint32_t expected_target, void *fn, void **original) {
    uintptr_t a = va(slot);
    PatchRec *r = pending_add(a, 4, NULL);
    if (!r) return 0;
    uint32_t cur;
    memcpy(&cur, r->orig, 4);
    if (expected_target && cur != va(expected_target)) {
        api_log(g_owner, CC2X_LOG_ERROR, "pointer at 0x%08x already changed (another mod?)", slot);
        g_npending--;
        g_pending_failed = 1;
        return 0;
    }
    if (original) *original = (void *)(uintptr_t)cur;
    uint32_t v = (uint32_t)(uintptr_t)fn;
    memcpy(r->repl, &v, 4);
    return 1;
}

static int overlaps(const PatchRec *a, const PatchRec *b) {
    return a->addr < b->addr + b->len && b->addr < a->addr + a->len;
}

static int write_bytes(uintptr_t addr, const uint8_t *bytes, uint32_t len) {
    DWORD old, tmp;
    if (!VirtualProtect((void *)addr, len, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy((void *)addr, bytes, len);
    VirtualProtect((void *)addr, len, old, &tmp);
    FlushInstructionCache(GetCurrentProcess(), (void *)addr, len);
    return 1;
}

static int patch_commit(void) {
    int ok = !g_pending_failed && g_ndone + g_npending <= MAX_PATCHES;
    for (int i = 0; ok && i < g_npending; ++i) {
        for (int j = 0; ok && j < g_ndone; ++j)
            if (overlaps(&g_pending[i], &g_done[j])) {
                api_log(g_owner, CC2X_LOG_ERROR, "patch at 0x%08x conflicts with %s",
                        (unsigned)(g_pending[i].addr - g_base + 0x400000), g_done[j].owner);
                ok = 0;
            }
        for (int j = 0; ok && j < i; ++j)
            if (overlaps(&g_pending[i], &g_pending[j])) ok = 0;
    }
    int written = 0;
    for (; ok && written < g_npending; ++written)
        if (!write_bytes(g_pending[written].addr, g_pending[written].repl, g_pending[written].len)) ok = 0;
    if (!ok) {
        while (written > 0) {
            --written;
            write_bytes(g_pending[written].addr, g_pending[written].orig, g_pending[written].len);
        }
        api_log(g_owner, CC2X_LOG_ERROR, "patches not applied (nothing was changed)");
        g_npending = 0;
        return 0;
    }
    memcpy(&g_done[g_ndone], g_pending, sizeof(PatchRec) * g_npending);
    g_ndone += g_npending;
    g_npending = 0;
    return 1;
}

static void patch_abort(void) { g_npending = 0; g_pending_failed = 0; }

int core_patch(int (*add)(void)) {
    static PatchRec saved[MAX_PATCHES];
    int n = g_npending, failed = g_pending_failed;
    const char *owner = g_owner;
    memcpy(saved, g_pending, sizeof(PatchRec) * n);
    patch_begin("cc2x");
    int ok = add() && patch_commit();
    memcpy(g_pending, saved, sizeof(PatchRec) * n);
    g_npending = n;
    g_pending_failed = failed;
    g_owner = owner;
    return ok;
}

/* ============================================================================ events */

#define MAX_CB 32
typedef struct { void (*fn)(void *, void *); void *user; } TickCb;
typedef struct { void (*fn)(void *, int, void *); void *user; } SessCb;
typedef struct { void (*fn)(cc2x_lua *, void *); void *user; } LuaCb;
static TickCb g_tick_cb[MAX_CB]; static int g_ntick;
static SessCb g_sess_cb[MAX_CB]; static int g_nsess;
static LuaCb g_lua_cb[MAX_CB]; static int g_nluacb;

static void on_server_tick(void (*fn)(void *, void *), void *user) {
    if (fn && g_ntick < MAX_CB) g_tick_cb[g_ntick++] = (TickCb){fn, user};
}
static void on_session(void (*fn)(void *, int, void *), void *user) {
    if (fn && g_nsess < MAX_CB) g_sess_cb[g_nsess++] = (SessCb){fn, user};
}
static void on_lua_state(void (*fn)(cc2x_lua *, void *), void *user) {
    if (fn && g_nluacb < MAX_CB) g_lua_cb[g_nluacb++] = (LuaCb){fn, user};
}

/* server_post queue */
#define POST_MAX 256
typedef struct { void (*fn)(void *, void *); void *user; } Post;
static Post g_post[POST_MAX];
static int g_post_head, g_post_count;
static CRITICAL_SECTION g_post_cs;

static int server_post(void (*fn)(void *, void *), void *user) {
    int ok = 0;
    EnterCriticalSection(&g_post_cs);
    if (fn && g_post_count < POST_MAX) {
        g_post[(g_post_head + g_post_count) % POST_MAX] = (Post){fn, user};
        g_post_count++;
        ok = 1;
    }
    LeaveCriticalSection(&g_post_cs);
    return ok;
}

/* ============================================================================ server lifecycle */

typedef void (__attribute__((thiscall)) *ServerFn)(void *);
static ServerFn o_init, o_tick, o_end;
static void *volatile g_server;
static uint32_t g_tickno;

static void session_change(void *server, int started) {
    world_session();
    for (int i = 0; i < g_nsess; ++i) g_sess_cb[i].fn(server, started, g_sess_cb[i].user);
    LOG(CC2X_LOG_INFO, "session %s", started ? "started" : "ended");
}

static void __attribute__((thiscall)) h_init(void *self) {
    o_init(self);
    if (g_server && g_server != self) session_change(g_server, 0);
    if (g_server != self) {
        g_server = self;
        g_tickno = 0;
        session_change(self, 1);
    }
}

static int g_time_scale = 1;   /* cc2x.ini [cc2x] time_scale: developer option, logic ticks per tick */

static void tick_once(void *self);
static void __attribute__((thiscall)) h_tick(void *self) {
    for (int k = 0; k < g_time_scale; ++k) tick_once(self);
}

static void tick_once(void *self) {
    o_tick(self);
    if (g_server != self) {             /* a tick before init was seen */
        if (g_server) session_change(g_server, 0);
        g_server = self;
        g_tickno = 0;
        session_change(self, 1);
    }
    world_tick();
    for (;;) {
        Post p = {0};
        EnterCriticalSection(&g_post_cs);
        if (g_post_count) {
            p = g_post[g_post_head];
            g_post_head = (g_post_head + 1) % POST_MAX;
            g_post_count--;
        }
        LeaveCriticalSection(&g_post_cs);
        if (!p.fn) break;
        p.fn(self, p.user);
    }
    for (int i = 0; i < g_ntick; ++i) g_tick_cb[i].fn(self, g_tick_cb[i].user);
    g_tickno++;
}

static void __attribute__((thiscall)) h_end(void *self) {
    if (g_server == self) {
        session_change(self, 0);
        g_server = NULL;
    }
    o_end(self);
}

void *api_server(void) { return g_server; }

/* ============================================================================ game Lua bridge */

typedef struct { union { void *p; int b; int64_t i; double n; } v; int tt; int pad; } TValue;
#define L_TOP(L) (*(TValue **)((char *)(L) + 0xc))
#define L_G(L) (*(char **)((char *)(L) + 0x10))
#define L_CI(L) (*(char **)((char *)(L) + 0x14))
#define REGISTRYINDEX (-1001000)

static uintptr_t f_index2addr, f_auxsetstr, f_finishset, f_luaS_new, f_luaH_new, f_luaC_step, p_nilobject;
static int g_lua_ok;

#define XMM_CLOBBERS "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"

static TValue *x_index2addr(void *L, int idx) {
    TValue *r;
    __asm__ __volatile__("call *%[f]" : "=a"(r), "+c"(L), "+d"(idx) : [f] "m"(f_index2addr)
                         : "memory", "cc", XMM_CLOBBERS);
    return r;
}
static void x_auxsetstr(void *L, TValue *t, const char *k) {
    __asm__ __volatile__("pushl %[k]\n\tcall *%[f]\n\taddl $4, %%esp"
                         : "+c"(L), "+d"(t) : [k] "r"(k), [f] "m"(f_auxsetstr)
                         : "eax", "memory", "cc", XMM_CLOBBERS);
}
static void x_finishset(void *L, TValue *t, TValue *key, TValue *val, const void *slot) {
    __asm__ __volatile__("pushl %[s]\n\tpushl %[v]\n\tpushl %[k]\n\tcall *%[f]\n\taddl $12, %%esp"
                         : "+c"(L), "+d"(t) : [k] "r"(key), [v] "r"(val), [s] "r"(slot), [f] "m"(f_finishset)
                         : "eax", "memory", "cc", XMM_CLOBBERS);
}
static void *x_luaS_new(void *L, const char *s) {
    void *r;
    __asm__ __volatile__("call *%[f]" : "=a"(r), "+c"(L), "+d"(s) : [f] "m"(f_luaS_new)
                         : "memory", "cc", XMM_CLOBBERS);
    return r;
}
static void *x_luaH_new(void *L) {
    void *r;
    __asm__ __volatile__("call *%[f]" : "=a"(r), "+c"(L) : [f] "m"(f_luaH_new)
                         : "edx", "memory", "cc", XMM_CLOBBERS);
    return r;
}
static void x_luaC_step(void *L) {
    __asm__ __volatile__("call *%[f]" : "+c"(L) : [f] "m"(f_luaC_step)
                         : "eax", "edx", "memory", "cc", XMM_CLOBBERS);
}

static void gc_check(void *L) {
    if (*(int *)(L_G(L) + 0xc) > 0) x_luaC_step(L);
}

static TValue *arg(cc2x_lua *L, int idx) {
    TValue *top = L_TOP(L);
    if (idx > 0) {
        TValue *o = *(TValue **)L_CI(L) + idx;
        return o < top ? o : NULL;
    }
    return idx < 0 ? top + idx : NULL;
}

static int l_gettop(cc2x_lua *L) { return (int)(L_TOP(L) - (*(TValue **)L_CI(L) + 1)); }
static int l_type(cc2x_lua *L, int idx) {
    TValue *o = arg(L, idx);
    return o ? (o->tt & 0x0f) : CC2X_LUA_NONE;
}
static int l_tointeger(cc2x_lua *L, int idx, int64_t *out) {
    TValue *o = arg(L, idx);
    if (!o) return 0;
    if (o->tt == 0x13) { *out = o->v.i; return 1; }
    if (o->tt == 0x03) { *out = (int64_t)o->v.n; return 1; }
    return 0;
}
static int l_tonumber(cc2x_lua *L, int idx, double *out) {
    TValue *o = arg(L, idx);
    if (!o) return 0;
    if (o->tt == 0x13) { *out = (double)o->v.i; return 1; }
    if (o->tt == 0x03) { *out = o->v.n; return 1; }
    return 0;
}
static int l_toboolean(cc2x_lua *L, int idx) {
    TValue *o = arg(L, idx);
    return o && o->tt != 0 && !(o->tt == 1 && o->v.b == 0);
}
static const char *l_tostring(cc2x_lua *L, int idx, size_t *len) {
    TValue *o = arg(L, idx);
    if (!o || (o->tt != 0x44 && o->tt != 0x54)) return NULL;
    char *ts = o->v.p;
    if (len) *len = o->tt == 0x44 ? *(uint8_t *)(ts + 7) : *(size_t *)(ts + 12);
    return ts + 16;
}

static TValue *push_slot(cc2x_lua *L) {
    TValue *t = L_TOP(L);
    L_TOP(L) = t + 1;
    return t;
}
static void l_pushnil(cc2x_lua *L) { push_slot(L)->tt = 0; }
static void l_pushboolean(cc2x_lua *L, int b) { TValue *t = push_slot(L); t->v.i = 0; t->v.b = !!b; t->tt = 1; }
static void l_pushinteger(cc2x_lua *L, int64_t v) { TValue *t = push_slot(L); t->v.i = v; t->tt = 0x13; }
static void l_pushnumber(cc2x_lua *L, double v) { TValue *t = push_slot(L); t->v.n = v; t->tt = 0x03; }
static void l_pushcfunction(cc2x_lua *L, cc2x_lua_fn fn) {
    TValue *t = push_slot(L);
    t->v.i = 0;
    t->v.p = (void *)fn;
    t->tt = 0x16;
}
static void l_pushstring(cc2x_lua *L, const char *s) {
    if (!s) { l_pushnil(L); return; }
    char *ts = x_luaS_new(L, s);
    TValue *t = push_slot(L);
    t->v.i = 0;
    t->v.p = ts;
    t->tt = *(uint8_t *)(ts + 4) | 0x40;
    gc_check(L);
}
static void l_newtable(cc2x_lua *L) {
    void *h = x_luaH_new(L);
    TValue *t = push_slot(L);
    t->v.i = 0;
    t->v.p = h;
    t->tt = 0x45;
    gc_check(L);
}
static void l_setfield(cc2x_lua *L, const char *k) {
    x_auxsetstr(L, x_index2addr(L, -2), k);
}
uintptr_t lua_state_key(cc2x_lua *L) { return (uintptr_t)L_G(L); }

static void l_appendi(cc2x_lua *L, int64_t i) {
    TValue *t = x_index2addr(L, -2);
    l_pushinteger(L, i);
    TValue *top = L_TOP(L);
    x_finishset(L, t, top - 1, top - 2, (const void *)p_nilobject);
    L_TOP(L) = top - 2;
}

/* registered functions */
#define MAX_LUA_FN 256
#define MAX_FEATURES 64
typedef struct { char ns[32]; char name[48]; cc2x_lua_fn fn; } LuaFn;
static LuaFn g_luafn[MAX_LUA_FN]; static int g_nluafn;
static char g_features[MAX_FEATURES][48]; static int g_nfeatures;

static int lua_add(const char *ns, const char *name, cc2x_lua_fn fn) {
    if (!name || !fn || g_nluafn >= MAX_LUA_FN) return 0;
    LuaFn *f = &g_luafn[g_nluafn++];
    snprintf(f->ns, sizeof(f->ns), "%s", ns ? ns : "");
    snprintf(f->name, sizeof(f->name), "%s", name);
    f->fn = fn;
    return 1;
}
static void lua_feature(const char *name) {
    if (name && g_nfeatures < MAX_FEATURES) snprintf(g_features[g_nfeatures++], 48, "%s", name);
}

/* core Lua functions */
static int lf_has(cc2x_lua *L) {
    const char *n = l_tostring(L, 1, NULL);
    int found = 0;
    for (int i = 0; n && i < g_nfeatures; ++i)
        if (!strcmp(g_features[i], n)) found = 1;
    l_pushboolean(L, found);
    return 1;
}
static int lf_log(cc2x_lua *L) {
    char buf[900];
    size_t used = 0;
    int n = l_gettop(L);
    buf[0] = 0;
    for (int i = 1; i <= n && used < sizeof(buf) - 2; ++i) {
        char tmp[64];
        const char *s = l_tostring(L, i, NULL);
        double d;
        int64_t v;
        if (!s) {
            TValue *o = arg(L, i);
            if (o && o->tt == 0x13 && l_tointeger(L, i, &v)) snprintf(tmp, sizeof(tmp), "%lld", (long long)v);
            else if (l_tonumber(L, i, &d)) snprintf(tmp, sizeof(tmp), "%g", d);
            else if (l_type(L, i) == CC2X_LUA_BOOLEAN) snprintf(tmp, sizeof(tmp), "%s", l_toboolean(L, i) ? "true" : "false");
            else snprintf(tmp, sizeof(tmp), "<%d>", l_type(L, i));
            s = tmp;
        }
        used += snprintf(buf + used, sizeof(buf) - used, "%s%s", i > 1 ? " " : "", s);
    }
    api_log("lua", CC2X_LOG_INFO, "%s", buf);
    return 0;
}
static void register_cc2x(cc2x_lua *L) {
    l_newtable(L);
    l_pushstring(L, CC2X_VERSION); l_setfield(L, "version");
    l_pushinteger(L, CC2X_API_VERSION); l_setfield(L, "api");
    l_pushstring(L, P->build); l_setfield(L, "game_build");
    l_pushcfunction(L, lf_has); l_setfield(L, "has");
    l_pushcfunction(L, lf_log); l_setfield(L, "log");
    for (int i = 0; i < g_nluafn; ++i) {            /* plugin functions without namespace */
        if (g_luafn[i].ns[0]) continue;
        l_pushcfunction(L, g_luafn[i].fn);
        l_setfield(L, g_luafn[i].name);
    }
    for (int i = 0; i < g_nluafn; ++i) {            /* one sub-table per namespace */
        if (!g_luafn[i].ns[0]) continue;
        int first = 1;
        for (int j = 0; j < i; ++j)
            if (!strcmp(g_luafn[j].ns, g_luafn[i].ns)) first = 0;
        if (!first) continue;
        l_newtable(L);
        for (int j = i; j < g_nluafn; ++j) {
            if (strcmp(g_luafn[j].ns, g_luafn[i].ns)) continue;
            l_pushcfunction(L, g_luafn[j].fn);
            l_setfield(L, g_luafn[j].name);
        }
        l_setfield(L, g_luafn[i].ns);
    }
    /* _G.cc2x = table: registry[LUA_RIDX_GLOBALS = 2] */
    TValue *reg = (TValue *)(L_G(L) + 0x28);
    char *regt = reg->v.p;
    if (reg->tt == 0x45 && *(uint32_t *)(regt + 8) >= 2) {
        TValue *globals = *(TValue **)(regt + 0xc) + 1;
        x_auxsetstr(L, globals, "cc2x");
    } else {
        L_TOP(L) = L_TOP(L) - 1;
        LOG(CC2X_LOG_ERROR, "unexpected Lua registry layout; cc2x not registered");
    }
}

static void h_lua_state(cc2x_cpu *cpu, void *user) {
    (void)user;
    cc2x_lua *L = (cc2x_lua *)(uintptr_t)cpu->edi;
    if (!L) return;
    TValue *top = L_TOP(L);
    register_cc2x(L);
    for (int i = 0; i < g_nluacb; ++i) g_lua_cb[i].fn(L, g_lua_cb[i].user);
    if (L_TOP(L) != top) {
        LOG(CC2X_LOG_WARN, "Lua stack changed by a plugin callback; restored");
        L_TOP(L) = top;
    }
}

/* ============================================================================ plugins */

static cc2x_api g_api;
static char g_loading_dir[MAX_PATH * 2];             /* folder of the plugin being loaded (ANSI) */
static char g_config_dir[MAX_PATH * 2];
#define MAX_PLUGINS 64
static wchar_t g_loaded[MAX_PLUGINS][MAX_PATH];      /* file names: one copy of each plugin */
static int g_nloaded;

static const char *api_plugin_dir(void) { return g_loading_dir; }

static void to_ansi(const wchar_t *w, char *out, int size) {
    if (!WideCharToMultiByte(CP_ACP, 0, w, -1, out, size, NULL, NULL)) out[0] = 0;
}

/* dir: the plugin's folder, file: its file name, from: where it came from (for the log) */
static void load_plugin(const wchar_t *dir, const wchar_t *file, const char *from) {
    char name[MAX_PATH];
    to_ansi(file, name, sizeof(name));
    for (int i = 0; i < g_nloaded; ++i)
        if (!_wcsicmp(g_loaded[i], file)) {
            LOG(CC2X_LOG_WARN, "plugin %s in %s ignored: already loaded from an enabled mod (an old copy? it can be deleted)", name, from);
            return;
        }
    wchar_t path[MAX_PATH * 2];
    _snwprintf(path, MAX_PATH * 2, L"%ls\\%ls", dir, file);
    path[MAX_PATH * 2 - 1] = 0;
    HMODULE m = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m) { LOG(CC2X_LOG_ERROR, "cannot load plugin %s from %s (error %lu)", name, from, GetLastError()); return; }
    cc2x_plugin_load_fn load = (cc2x_plugin_load_fn)(uintptr_t)GetProcAddress(m, "cc2x_plugin_load");
    if (!load) { LOG(CC2X_LOG_ERROR, "%s: no cc2x_plugin_load export", name); return; }
    if (g_nloaded < MAX_PLUGINS) wcsncpy(g_loaded[g_nloaded++], file, MAX_PATH - 1);
    to_ansi(dir, g_loading_dir, sizeof(g_loading_dir));
    patch_abort();
    int ok = load(&g_api);
    if (g_npending) {                 /* plugin forgot to commit */
        patch_abort();
        ok = 0;
    }
    g_loading_dir[0] = 0;
    LOG(ok ? CC2X_LOG_INFO : CC2X_LOG_WARN, "plugin %s %s (%s)", name, ok ? "loaded" : "inactive", from);
}

static void load_dir(const wchar_t *dir, const wchar_t *mask, const char *from) {
    wchar_t pattern[MAX_PATH * 2];
    _snwprintf(pattern, MAX_PATH * 2, L"%ls\\%ls", dir, mask);
    pattern[MAX_PATH * 2 - 1] = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) load_plugin(dir, fd.cFileName, from);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* the whole file as a NUL-terminated buffer (caller frees), NULL if missing */
static char *read_file(const wchar_t *path) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD size = GetFileSize(f, NULL), got = 0;
    char *buf = size < (1u << 20) ? HeapAlloc(GetProcessHeap(), 0, size + 1) : NULL;
    if (buf && ReadFile(f, buf, size, &got, NULL)) buf[got] = 0;
    else if (buf) { HeapFree(GetProcessHeap(), 0, buf); buf = NULL; }
    CloseHandle(f);
    return buf;
}

/* value of the first attr="..." after `from` (XML entities for & " ' < >), as UTF-16 */
static int xml_attr(const char *from, const char *attr, wchar_t *out, int size, const char **end) {
    char key[32], val[MAX_PATH * 3];
    snprintf(key, sizeof(key), "%s=\"", attr);
    const char *p = strstr(from, key);
    if (!p) return 0;
    p += strlen(key);
    int n = 0;
    while (*p && *p != '"' && n < (int)sizeof(val) - 1) {
        static const struct { const char *ent; char ch; } E[] = {{"&amp;", '&'}, {"&quot;", '"'}, {"&apos;", '\''}, {"&lt;", '<'}, {"&gt;", '>'}};
        int hit = 0;
        for (size_t i = 0; i < sizeof(E) / sizeof(E[0]) && !hit; ++i)
            if (!strncmp(p, E[i].ent, strlen(E[i].ent))) { val[n++] = E[i].ch; p += strlen(E[i].ent); hit = 1; }
        if (!hit) val[n++] = *p++;
    }
    val[n] = 0;
    if (end) *end = p;
    if (!MultiByteToWideChar(CP_UTF8, 0, val, -1, out, size)) return 0;
    for (wchar_t *c = out; *c; ++c) if (*c == L'/') *c = L'\\';
    return 1;
}

/* Plugins of the mods enabled in the game's mod menu: <mod folder>\content\cc2x\*.dll (inside content
 * because the game's Workshop upload takes only mod.xml, thumbnail.png and content), or
 * <mod folder>\cc2x\*.dll for local development. The game keeps the enabled mods (local folders and
 * Steam Workshop items alike) in %APPDATA%\Carrier Command 2\mods.xml. */
static void load_mod_plugins(void) {
    wchar_t appdata[MAX_PATH], list[MAX_PATH * 2];
    if (!GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH)) return;
    _snwprintf(list, MAX_PATH * 2, L"%ls\\Carrier Command 2\\mods.xml", appdata);
    list[MAX_PATH * 2 - 1] = 0;
    char *xml = read_file(list);
    if (!xml) { LOG(CC2X_LOG_INFO, "no mods.xml yet: no mods enabled"); return; }
    const char *p = strstr(xml, "<active_mod_folders");
    int mods = 0;
    wchar_t dir[MAX_PATH * 2], plugins[MAX_PATH * 2], modxml[MAX_PATH * 2], title[128];
    while (p && (p = strstr(p, "<path ")) != NULL) {
        if (!xml_attr(p, "value", dir, MAX_PATH * 2, &p)) break;
        size_t len = wcslen(dir);
        while (len && dir[len - 1] == L'\\') dir[--len] = 0;
        if (GetFileAttributesW(dir) == INVALID_FILE_ATTRIBUTES) continue;   /* stale entry */
        ++mods;
        _snwprintf(plugins, MAX_PATH * 2, L"%ls\\content\\cc2x", dir);
        plugins[MAX_PATH * 2 - 1] = 0;
        if (GetFileAttributesW(plugins) == INVALID_FILE_ATTRIBUTES) {
            _snwprintf(plugins, MAX_PATH * 2, L"%ls\\cc2x", dir);
            plugins[MAX_PATH * 2 - 1] = 0;
            if (GetFileAttributesW(plugins) == INVALID_FILE_ATTRIBUTES) continue;
        }
        _snwprintf(modxml, MAX_PATH * 2, L"%ls\\mod.xml", dir);
        modxml[MAX_PATH * 2 - 1] = 0;
        char *mx = read_file(modxml), from[300], tname[128];
        if (!mx || !xml_attr(mx, "name", title, 128, NULL)) wcscpy(title, wcsrchr(dir, L'\\') ? wcsrchr(dir, L'\\') + 1 : dir);
        if (mx) HeapFree(GetProcessHeap(), 0, mx);
        if (!WideCharToMultiByte(CP_UTF8, 0, title, -1, tname, sizeof(tname), NULL, NULL)) strcpy(tname, "?");
        snprintf(from, sizeof(from), "mod \"%s\"", tname);
        load_dir(plugins, L"*.dll", from);
    }
    HeapFree(GetProcessHeap(), 0, xml);
    LOG(CC2X_LOG_INFO, "%d enabled mod folder(s) checked for plugins", mods);
}

static void load_plugins(void) {
    load_mod_plugins();                                  /* first: they win over old manual copies */
    wchar_t game[MAX_PATH], sub[MAX_PATH * 2];
    if (!GetModuleFileNameW(NULL, game, MAX_PATH)) return;
    wchar_t *slash = wcsrchr(game, L'\\');
    if (slash) *slash = 0;
    load_dir(game, L"cc2x_*.dll", "the game folder");   /* manual installs, development */
    _snwprintf(sub, MAX_PATH * 2, L"%ls\\cc2x\\plugins", game);
    sub[MAX_PATH * 2 - 1] = 0;
    load_dir(sub, L"*.dll", "cc2x\\plugins");
}

/* ============================================================================ startup */

static const Profile *detect(void) {
    uint8_t *base = (uint8_t *)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    for (size_t i = 0; i < sizeof(PROFILES) / sizeof(PROFILES[0]); ++i)
        if (nt->FileHeader.TimeDateStamp == PROFILES[i].timestamp &&
            nt->OptionalHeader.SizeOfImage == PROFILES[i].image_size)
            return &PROFILES[i];
    LOG(CC2X_LOG_WARN, "unsupported game build (PE timestamp 0x%08lx, size 0x%08lx): CC2X stays inactive",
        nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
    return NULL;
}

static void startup(void) {
    P = detect();
    if (!P) return;
    g_base = (uintptr_t)GetModuleHandleW(NULL);

    uint32_t fns[6] = {P->f_index2addr, P->f_auxsetstr, P->f_finishset, P->f_luaS_new, P->f_luaH_new, P->f_luaC_step};
    g_lua_ok = 1;
    for (size_t i = 0; i < sizeof(LUA_SIGS) / sizeof(LUA_SIGS[0]); ++i)
        if (memcmp((void *)va(fns[LUA_SIGS[i].field]), LUA_SIGS[i].bytes, 8)) g_lua_ok = 0;
    f_index2addr = va(P->f_index2addr); f_auxsetstr = va(P->f_auxsetstr); f_finishset = va(P->f_finishset);
    f_luaS_new = va(P->f_luaS_new); f_luaH_new = va(P->f_luaH_new); f_luaC_step = va(P->f_luaC_step);
    p_nilobject = va(P->nilobject);

    patch_begin("cc2x");
    void *oi, *ot, *oe;
    patch_ptr(P->server_vtable + 0, P->server_init, (void *)h_init, &oi);
    patch_ptr(P->server_vtable + 4, P->server_tick, (void *)h_tick, &ot);
    patch_ptr(P->server_vtable + 8, P->server_end, (void *)h_end, &oe);
    if (g_lua_ok) patch_mid(P->lua_site, 8, P->lua_site_bytes, h_lua_state, NULL);
    if (!patch_commit()) {
        LOG(CC2X_LOG_ERROR, "core hooks failed; CC2X stays inactive");
        P = NULL;
        return;
    }
    o_init = (ServerFn)oi; o_tick = (ServerFn)ot; o_end = (ServerFn)oe;
    if (!g_lua_ok) LOG(CC2X_LOG_WARN, "Lua functions not recognised: the cc2x Lua table is disabled");

    g_api = (cc2x_api){
        CC2X_API_VERSION, sizeof(cc2x_api), CC2X_VERSION, P->build, g_base, g_dir,
        va, api_log,
        patch_begin, patch_call, patch_mid, patch_ptr, patch_commit, patch_abort,
        on_server_tick, on_session, on_lua_state, api_server, server_post,
        lua_add, lua_feature,
        l_gettop, l_type, l_tointeger, l_tonumber, l_toboolean, l_tostring,
        l_pushnil, l_pushboolean, l_pushinteger, l_pushnumber, l_pushstring, l_newtable, l_setfield, l_appendi,
        snap_vehicles, snap_vehicle, snap_tiles, snap_tile,
        api_plugin_dir, .config_dir = g_config_dir,
    };
    if (!world_init(P->build)) LOG(CC2X_LOG_WARN, "no world layout for this build: API 3 functions do nothing");
    world_fill(&g_api);
    lua_world_init(&g_api);          /* before the plugins: the core Lua functions come first */
    wchar_t appdata[MAX_PATH], cfg[MAX_PATH * 2];
    if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH)) {
        _snwprintf(cfg, MAX_PATH * 2, L"%ls\\Carrier Command 2\\cc2x", appdata);
        cfg[MAX_PATH * 2 - 1] = 0;
        CreateDirectoryW(cfg, NULL);
        to_ansi(cfg, g_config_dir, sizeof(g_config_dir));
    }
    char ini[MAX_PATH + 16];
    snprintf(ini, sizeof(ini), "%s\\cc2x.ini", g_dir);
    g_time_scale = (int)GetPrivateProfileIntA("cc2x", "time_scale", 1, ini);
    if (g_time_scale < 1) g_time_scale = 1;
    if (g_time_scale > 8) g_time_scale = 8;
    if (g_time_scale > 1) LOG(CC2X_LOG_WARN, "developer option time_scale=%d (logic runs %dx faster)", g_time_scale, g_time_scale);
    LOG(CC2X_LOG_INFO, "CC2X %s ready for Carrier Command 2 %s%s", CC2X_VERSION, P->build,
        g_lua_ok ? "" : " (no Lua bridge)");
    load_plugins();
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    g_self = module;
    InitializeCriticalSection(&g_log_cs);
    InitializeCriticalSection(&g_post_cs);
    HMODULE pinned;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                       (LPCWSTR)(uintptr_t)DllMain, &pinned);
    DWORD n = GetModuleFileNameA(NULL, g_dir, MAX_PATH);
    char *slash = n ? strrchr(g_dir, '\\') : NULL;
    if (slash) *slash = 0;
    snprintf(g_log_path, sizeof(g_log_path), "%s\\cc2x.log", g_dir);
    DeleteFileA(g_log_path);
    startup();
    return TRUE;
}
