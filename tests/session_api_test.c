/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "cc2x.h"
#include "cc2x_session.h"

/* The stable API 4 ends here. Fork-specific tail slots must never be treated
 * as session function pointers just because their numeric version is >= 5. */
_Static_assert(CC2X_API_VERSION == 8, "Canonical integrated team API");
_Static_assert(offsetof(cc2x_api, active_mod_count) == CC2X_API_V4_SIZE,
               "Preserve the common API 4 prefix; session extension uses no tail slot");

static uint64_t generation(void) { return 42; }
static int post(uint64_t epoch, void (*fn)(void *, void *), void *user) {
    (void)epoch; (void)fn; (void)user; return 0;
}
static cc2x_session_api sample;
static const cc2x_session_api *query(uint32_t version, uint32_t size) {
    assert(version == CC2X_SESSION_VERSION && size == sizeof(sample));
    return &sample;
}

int main(void) {
    cc2x_session_api api = {1, sizeof(cc2x_session_api), CC2X_SESSION_ID, generation, post};
    sample = api;
    assert(!cc2x_session_resolve(NULL)); /* an API 5/8 fork with no named extension */
    assert(cc2x_session_resolve(query) == &sample);
    sample.interface_id ^= 1;
    assert(!cc2x_session_resolve(query));
    assert(!cc2x_session_compatible(NULL));
    assert(cc2x_session_compatible(&api));
    api.interface_id ^= 1;
    assert(!cc2x_session_compatible(&api));
    api.interface_id = CC2X_SESSION_ID;
    api.size--;
    assert(!cc2x_session_compatible(&api));
    api.size = sizeof(api); api.version = 8;
    assert(!cc2x_session_compatible(&api));
    api.version = 1; api.post = NULL;
    assert(!cc2x_session_compatible(&api));
    api.post = post; api.generation = NULL;
    assert(!cc2x_session_compatible(&api));
    puts("PASS: stable API prefix, extension identity/version/size, absent callbacks");
    return 0;
}
