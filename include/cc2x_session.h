/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Named optional extension: never reinterpret fork-specific cc2x_api tails.
 * Resolve CC2X_SESSION_QUERY from the module owning api->server_post. */
#ifndef CC2X_SESSION_H
#define CC2X_SESSION_H
#include <stdint.h>
#include <stddef.h>

#define CC2X_SESSION_QUERY "cc2x_query_session_v1"
#define CC2X_SESSION_VERSION 1u
#define CC2X_SESSION_ID UINT64_C(0x4343325853455331)

typedef struct cc2x_session_api {
    uint32_t version;
    uint32_t size;
    uint64_t interface_id;
    /* 0 without a local logic server; changes on every session, even pointer reuse. */
    uint64_t (*generation)(void);
    /* Returns 0 if absent/stale/full; discarded on end, no ownership transfer or RPC. */
    int (*post)(uint64_t generation, void (*fn)(void *server, void *user), void *user);
} cc2x_session_api;

typedef const cc2x_session_api *(*cc2x_query_session_fn)(uint32_t version, uint32_t size);

static inline int cc2x_session_compatible(const cc2x_session_api *api) {
    return api && api->version == CC2X_SESSION_VERSION && api->size >= sizeof(*api)
        && api->interface_id == CC2X_SESSION_ID && api->generation && api->post;
}
static inline const cc2x_session_api *cc2x_session_resolve(cc2x_query_session_fn query) {
    if (!query) return NULL;
    const cc2x_session_api *api = query(CC2X_SESSION_VERSION, sizeof(cc2x_session_api));
    return cc2x_session_compatible(api) ? api : NULL;
}
#endif
