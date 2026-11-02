/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "cc2x_post_queue.h"

static void command(void *server, void *user) { (void)server; (void)user; }

int main(void) {
    cc2x_post_queue q = {0};
    cc2x_post p;
    void *host = (void *)(uintptr_t)1;
    assert(!cc2x_posts_push(&q, 0, command, NULL));
    cc2x_posts_reset(&q, host);
    uint64_t first = q.epoch;
    assert(first && q.owner == host);
    assert(!cc2x_posts_push(&q, first - 1, command, NULL));
    assert(!cc2x_posts_push(&q, first, NULL, NULL));
    for (uintptr_t i = 0; i < CC2X_POST_MAX; ++i)
        assert(cc2x_posts_push(&q, first, command, (void *)i));
    assert(!cc2x_posts_push(&q, first, command, NULL));
    assert(!cc2x_posts_take(&q, (void *)(uintptr_t)2, first, &p));
    for (uintptr_t i = 0; i < CC2X_POST_MAX; ++i) {
        assert(cc2x_posts_take(&q, host, first, &p));
        assert(p.fn == command && p.user == (void *)i);
    }
    assert(!cc2x_posts_take(&q, host, first, &p));
    assert(cc2x_posts_push(&q, first, command, host));
    cc2x_posts_reset(&q, NULL);
    assert(!cc2x_posts_push(&q, q.epoch, command, NULL));
    assert(!cc2x_posts_take(&q, host, first, &p));
    /* A new session can reuse the same game object address. Its old commands cannot survive. */
    cc2x_posts_reset(&q, host);
    assert(q.epoch != first);
    assert(!cc2x_posts_push(&q, first, command, NULL));
    assert(!cc2x_posts_take(&q, host, q.epoch, &p));
    assert(cc2x_posts_push(&q, q.epoch, command, host));
    assert(cc2x_posts_take(&q, host, q.epoch, &p));
    assert(p.user == host);
    puts("PASS: session isolation, stale rejection, FIFO, capacity, address reuse, no-server refusal");
    return 0;
}
