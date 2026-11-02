#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include "cc2x.h"

/* Test-only, passive trace. The hook handlers only copy register/stack values
 * into bounded storage. All formatting and logging occurs later on server tick. */
#define TRACE_CAPACITY 64
#define POINTER_CAPACITY 32

enum { EVENT_ENQUEUE = 1, EVENT_CONSUMER_MATCH = 2, EVENT_SESSION_START = 3, EVENT_SESSION_END = 4 };
typedef struct TraceEvent {
    volatile LONG ready;
    LONG kind;
    LONG generation;
    LONG sequence;
    DWORD thread_id;
    ULONGLONG tick_ms;
    uintptr_t owner;
    uintptr_t entity;
    uint32_t flag;
} TraceEvent;

static const cc2x_api *g_api;
static TraceEvent g_events[TRACE_CAPACITY];
static volatile LONG g_event_count;
static volatile LONG g_overflow;
static volatile LONG g_generation;
static void *volatile g_queued[POINTER_CAPACITY];
static volatile LONG g_queued_count;
static LONG g_reported;
static LONG g_overflow_reported;
static volatile LONG g_worker_started;
static volatile LONG g_worker_failed;

static void push_event(LONG kind, uintptr_t owner, uintptr_t entity, uint32_t flag) {
    LONG index = InterlockedIncrement(&g_event_count) - 1;
    if (index < 0 || index >= TRACE_CAPACITY) {
        InterlockedIncrement(&g_overflow);
        return;
    }
    TraceEvent *event = &g_events[index];
    event->kind = kind;
    event->generation = InterlockedCompareExchange(&g_generation, 0, 0);
    event->sequence = index + 1;
    event->thread_id = GetCurrentThreadId();
    event->tick_ms = GetTickCount64();
    event->owner = owner;
    event->entity = entity;
    event->flag = flag;
    InterlockedExchange(&event->ready, 1);
}

static void remember_pointer(void *entity) {
    if (!entity) return;
    LONG count = InterlockedCompareExchange(&g_queued_count, 0, 0);
    for (LONG i = 0; i < count && i < POINTER_CAPACITY; ++i) {
        if (InterlockedCompareExchangePointer(&g_queued[i], NULL, NULL) == entity) return;
    }
    LONG index = InterlockedIncrement(&g_queued_count) - 1;
    if (index >= 0 && index < POINTER_CAPACITY)
        InterlockedExchangePointer(&g_queued[index], entity);
    else
        InterlockedExchange(&g_queued_count, POINTER_CAPACITY);
}

static int consume_matching_pointer(void *entity) {
    if (!entity) return 0;
    LONG count = InterlockedCompareExchange(&g_queued_count, 0, 0);
    if (count > POINTER_CAPACITY) count = POINTER_CAPACITY;
    for (LONG i = 0; i < count; ++i) {
        if (InterlockedCompareExchangePointer(&g_queued[i], NULL, NULL) == entity) {
            InterlockedExchangePointer(&g_queued[i], NULL);
            return 1;
        }
    }
    return 0;
}

static void __cdecl on_enqueue(cc2x_cpu *cpu, void *user) {
    (void)user;
    uint32_t *stack = CC2X_STACK(cpu);
    void *entity = (void *)(uintptr_t)stack[0];
    uint32_t flag = stack[1];
    remember_pointer(entity);
    push_event(EVENT_ENQUEUE, cpu->ecx, (uintptr_t)entity, flag);
}

static void __cdecl on_consumer(cc2x_cpu *cpu, void *user) {
    (void)user;
    uint32_t *stack = CC2X_STACK(cpu);
    void *entity = (void *)(uintptr_t)stack[0];
    if (consume_matching_pointer(entity))
        push_event(EVENT_CONSUMER_MATCH, cpu->ecx, (uintptr_t)entity, 0);
}

static void __cdecl on_session(void *server, int started, void *user) {
    (void)user;
    if (started) {
        InterlockedIncrement(&g_generation);
        InterlockedExchange(&g_queued_count, 0);
        for (LONG i = 0; i < POINTER_CAPACITY; ++i)
            InterlockedExchangePointer(&g_queued[i], NULL);
    }
    push_event(started ? EVENT_SESSION_START : EVENT_SESSION_END,
               (uintptr_t)server, 0, 0);
}

static DWORD WINAPI writer_thread(void *unused) {
    (void)unused;
    for (;;) {
        LONG count = InterlockedCompareExchange(&g_event_count, 0, 0);
        if (count > TRACE_CAPACITY) count = TRACE_CAPACITY;
        while (g_reported < count) {
            TraceEvent *event = &g_events[g_reported];
            if (InterlockedCompareExchange(&event->ready, 0, 0) != 1) break;
            const char *name = "UNKNOWN";
            switch (event->kind) {
                case EVENT_ENQUEUE: name = "QUEUE_INSERT"; break;
                case EVENT_CONSUMER_MATCH: name = "DELETE_CONSUMER_MATCH"; break;
                case EVENT_SESSION_START: name = "SESSION_START"; break;
                case EVENT_SESSION_END: name = "SESSION_END"; break;
            }
            g_api->log("entity_retirement_trace", CC2X_LOG_INFO,
                       "event=%s seq=%ld generation=%ld tick_ms=%llu thread=%lu owner=%p entity=%p flag=%lu",
                       name, event->sequence, event->generation, event->tick_ms,
                       event->thread_id, (void *)event->owner, (void *)event->entity,
                       (unsigned long)event->flag);
            ++g_reported;
        }
        LONG overflow = InterlockedCompareExchange(&g_overflow, 0, 0);
        if (overflow && !g_overflow_reported) {
            g_api->log("entity_retirement_trace", CC2X_LOG_WARN,
                       "event=TRACE_CAP_REACHED capacity=%d dropped=%ld", TRACE_CAPACITY, overflow);
            g_overflow_reported = 1;
        }
        Sleep(25);
    }
}

static void __cdecl on_server_tick(void *server, void *user) {
    (void)user;
    if (InterlockedCompareExchange(&g_worker_started, 1, 0) == 0) {
        HANDLE thread = CreateThread(NULL, 0, writer_thread, NULL, 0, NULL);
        if (thread) {
            CloseHandle(thread);
        } else {
            InterlockedExchange(&g_worker_started, 0);
            if (InterlockedExchange(&g_worker_failed, 1) == 0 && g_api && g_api->log)
                g_api->log("entity_retirement_trace", CC2X_LOG_ERROR,
                           "event=TRACE_WRITER_START_FAILED error=%lu", GetLastError());
        }
    }
}

CC2X_EXPORT int cc2x_plugin_load(const cc2x_api *api) {
    static const uint8_t enqueue_call[5] = {0xE8, 0x76, 0xC5, 0xEA, 0xFF};
    static const uint8_t consumer_call[5] = {0xE8, 0x8A, 0x06, 0xEA, 0xFF};
    char enabled_path[MAX_PATH * 2];
    static const char enabled_suffix[] = "\\entity_retirement_trace.enabled";
    size_t config_len;

    if (!api || api->api_version < 5 ||
        api->size < offsetof(cc2x_api, on_session) + sizeof(api->on_session) ||
        !api->config_dir || !api->log || !api->patch_begin || !api->patch_mid ||
        !api->patch_commit || !api->patch_abort || !api->on_server_tick || !api->on_session) return 0;

    config_len = strlen(api->config_dir);
    if (config_len + sizeof(enabled_suffix) > sizeof(enabled_path)) return 0;
    memcpy(enabled_path, api->config_dir, config_len);
    memcpy(enabled_path + config_len, enabled_suffix, sizeof(enabled_suffix));
    if (GetFileAttributesA(enabled_path) == INVALID_FILE_ATTRIBUTES) {
        api->log("entity_retirement_trace", CC2X_LOG_INFO,
                 "event=TRACE_DISABLED enable_file=%s", enabled_path);
        return 1;
    }

    g_api = api;
    api->patch_begin("entity_retirement_trace");
    if (!api->patch_mid(0x00742345, 5, enqueue_call, on_enqueue, NULL) ||
        !api->patch_mid(0x0074E111, 5, consumer_call, on_consumer, NULL) ||
        !api->patch_commit()) {
        api->patch_abort();
        api->log("entity_retirement_trace", CC2X_LOG_ERROR,
                 "event=TRACE_INSTALL_FAILED enqueue_rva=0x00342345 consumer_rva=0x0034E111; no trace hooks active");
        g_api = NULL;
        return 0;
    }

    api->on_session(on_session, NULL);
    api->on_server_tick(on_server_tick, NULL);
    api->log("entity_retirement_trace", CC2X_LOG_INFO,
             "event=TRACE_INSTALLED enqueue_va=0x00742345 consumer_va=0x0074E111 capacity=%d; original calls replay unchanged",
             TRACE_CAPACITY);
    return 1;
}
