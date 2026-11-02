/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CC2X_SESSION_WIN32_H
#define CC2X_SESSION_WIN32_H
#include <windows.h>
#include <string.h>
#include "cc2x_session.h"

/* Resolve only from the module owning an existing core callback. Never search
 * arbitrary DLLs by a package filename or inspect unknown cc2x_api tail slots. */
static inline const cc2x_session_api *cc2x_session_from_core(const void *core_address) {
    HMODULE module = NULL;
    if (!core_address || !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)core_address, &module))
        return NULL;
    FARPROC symbol = GetProcAddress(module, CC2X_SESSION_QUERY);
    cc2x_query_session_fn query = NULL;
    /* Windows exports are addresses; the named v1 contract specifies cdecl. */
#ifdef __cplusplus
    static_assert(sizeof(query) == sizeof(symbol), "Windows function pointer size");
#else
    _Static_assert(sizeof(query) == sizeof(symbol), "Windows function pointer size");
#endif
    memcpy(&query, &symbol, sizeof(query));
    return cc2x_session_resolve(query);
}
#endif
