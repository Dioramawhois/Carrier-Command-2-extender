/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include "platform/seh_guard.h"
static int calls;
static void successful(void *user) { assert(user == &calls); ++calls; }
static void failed(void *user) { (void)user; RaiseException(0xE0123456, 0, 0, NULL); }
int main(void) {
    assert(!cc2x_seh_guard(NULL, NULL));
    assert(cc2x_seh_guard(successful, &calls) && calls == 1);
    assert(!cc2x_seh_guard(failed, NULL));
    assert(cc2x_seh_guard(successful, &calls) && calls == 2);
    puts("PASS: native SEH isolation, null callback, fault recovery");
    return 0;
}
