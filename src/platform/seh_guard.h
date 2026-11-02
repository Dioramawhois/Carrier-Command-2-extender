/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
/* Callback must not throw C++ exceptions or rely on C++ unwinding through the
 * native engine call. Returns 0 when native SEH is caught, 1 after normal return. */
int cc2x_seh_guard(void (*callback)(void *), void *user);
#ifdef __cplusplus
}
#endif
