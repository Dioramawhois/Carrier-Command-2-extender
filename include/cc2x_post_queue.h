/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Session-scoped command queue. The caller serializes ALL access with its own lock.
 * No game addresses, Windows APIs, allocation or callbacks are used here. */
#ifndef CC2X_POST_QUEUE_H
#define CC2X_POST_QUEUE_H
#include <stddef.h>
#include <stdint.h>
#define CC2X_POST_MAX 256
typedef struct { void (*fn)(void *, void *); void *user; } cc2x_post;
typedef struct {
    cc2x_post items[CC2X_POST_MAX];
    void *owner;
    uint64_t epoch;
    unsigned head, count;
} cc2x_post_queue;

static inline void cc2x_posts_reset(cc2x_post_queue *q, void *owner) {
    q->owner = owner;
    if (++q->epoch == 0) ++q->epoch;
    q->head = q->count = 0;
}
static inline int cc2x_posts_push(cc2x_post_queue *q, uint64_t epoch,
                                 void (*fn)(void *, void *), void *user) {
    if (!q->owner || !epoch || epoch != q->epoch || !fn || q->count == CC2X_POST_MAX) return 0;
    q->items[(q->head + q->count) % CC2X_POST_MAX] = (cc2x_post){fn, user};
    ++q->count;
    return 1;
}
static inline int cc2x_posts_take(cc2x_post_queue *q, void *owner, uint64_t epoch, cc2x_post *out) {
    if (!owner || q->owner != owner || !epoch || epoch != q->epoch || !q->count || !out) return 0;
    *out = q->items[q->head];
    q->head = (q->head + 1) % CC2X_POST_MAX;
    --q->count;
    return 1;
}
#endif
