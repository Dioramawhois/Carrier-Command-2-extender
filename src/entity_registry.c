/*
 * CC2X internal user-logic descriptor registry.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * This stores declarative metadata only; it never constructs game objects.
 */
#include <windows.h>
#include <ctype.h>
#include <string.h>
#include "entity_registry.h"

static CRITICAL_SECTION g_lock;
static cc2x_entity_type g_types[CC2X_ENTITY_TYPE_CAP];
static int g_count;
static int g_initialized;

static void copy_label(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) return;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void cc2x_entity_registry_init(void) {
    if (InterlockedCompareExchange((volatile LONG *)&g_initialized, 1, 0) == 0) {
        InitializeCriticalSection(&g_lock);
        memset(g_types, 0, sizeof(g_types));
        g_count = 0;
        MemoryBarrier();
        InterlockedExchange((volatile LONG *)&g_initialized, 2);
    } else {
        while (InterlockedCompareExchange((volatile LONG *)&g_initialized, 2, 2) != 2) Sleep(0);
    }
}

static int valid_label(const char *s, size_t cap, int namespaced) {
    size_t n;
    int colons = 0;
    if (!s || !(n = strnlen(s, cap)) || n >= cap) return 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == ':') { ++colons; if (!namespaced || i == 0 || i + 1 == n) return 0; continue; }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return 0;
    }
    return namespaced ? colons == 1 : colons == 0;
}

cc2x_entity_type_result cc2x_entity_type_register(const char *owner, const char *id, const char *base, const char *definition) {
    cc2x_entity_type_result result = CC2X_ENTITY_TYPE_OK;
    if (!valid_label(owner, CC2X_ENTITY_OWNER_CAP, 0) || !valid_label(id, CC2X_ENTITY_TYPE_ID_CAP, 1) ||
        !valid_label(base, CC2X_ENTITY_LABEL_CAP, 0) || !valid_label(definition, CC2X_ENTITY_LABEL_CAP, 0)) return CC2X_ENTITY_TYPE_INVALID;
    cc2x_entity_registry_init();
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < g_count; ++i) if (!strcmp(g_types[i].id, id)) { result = CC2X_ENTITY_TYPE_DUPLICATE; goto done; }
    if (g_count == CC2X_ENTITY_TYPE_CAP) { result = CC2X_ENTITY_TYPE_FULL; goto done; }
    copy_label(g_types[g_count].owner, sizeof(g_types[g_count].owner), owner);
    copy_label(g_types[g_count].id, sizeof(g_types[g_count].id), id);
    copy_label(g_types[g_count].base, sizeof(g_types[g_count].base), base);
    copy_label(g_types[g_count].definition, sizeof(g_types[g_count].definition), definition);
    ++g_count;
done:
    LeaveCriticalSection(&g_lock);
    return result;
}

int cc2x_entity_type_count(void) {
    int count;
    cc2x_entity_registry_init();
    EnterCriticalSection(&g_lock); count = g_count; LeaveCriticalSection(&g_lock);
    return count;
}

int cc2x_entity_type_get(int index, cc2x_entity_type *out) {
    int ok = 0;
    if (!out) return 0;
    cc2x_entity_registry_init();
    EnterCriticalSection(&g_lock);
    if (index >= 0 && index < g_count) { *out = g_types[index]; ok = 1; }
    LeaveCriticalSection(&g_lock);
    return ok;
}

int cc2x_entity_type_remove(const char *owner, const char *id) {
    int removed = 0;
    if (!valid_label(owner, CC2X_ENTITY_OWNER_CAP, 0) || !valid_label(id, CC2X_ENTITY_TYPE_ID_CAP, 1)) return 0;
    cc2x_entity_registry_init();
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < g_count; ++i) if (!strcmp(g_types[i].id, id)) {
        if (strcmp(g_types[i].owner, owner)) break;
        g_types[i] = g_types[g_count - 1]; memset(&g_types[--g_count], 0, sizeof(g_types[0])); removed = 1; break;
    }
    LeaveCriticalSection(&g_lock);
    return removed;
}

int cc2x_entity_type_remove_owner(const char *owner) {
    int removed = 0;
    if (!valid_label(owner, CC2X_ENTITY_OWNER_CAP, 0)) return 0;
    cc2x_entity_registry_init();
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < g_count;) {
        if (!strcmp(g_types[i].owner, owner)) { g_types[i] = g_types[g_count - 1]; memset(&g_types[--g_count], 0, sizeof(g_types[0])); ++removed; }
        else ++i;
    }
    LeaveCriticalSection(&g_lock);
    return removed;
}
