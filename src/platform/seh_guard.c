/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A freestanding C boundary keeps native SEH separate from C++ object lifetime.
 * Build this one file with the MSVC C ABI; it exchanges only cdecl fn/void*/
int cc2x_seh_guard(void (*callback)(void *), void *user) {
    if (!callback) return 0;
    __try { callback(user); }
    __except (1) { return 0; }
    return 1;
}
