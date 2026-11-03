/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Optional plugin example. Registers a Lua function; changes no gameplay. */
#include "cc2x_framework.h"
static const cc2x_framework_api *framework;
static int sum(cc2x_vm *vm, void *user) {
    int64_t a, b;
    (void)user;
    if (framework->vm->gettop(vm) != 2 ||
        !framework->vm->tointeger(vm, 1, &a) || !framework->vm->tointeger(vm, 2, &b)) return 0;
    /* Avoid signed overflow in this deliberately small example. */
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return 0;
    framework->vm->pushinteger(vm, a + b);
    return 1;
}
CC2X_EXPORT int cc2x_plugin_load(const cc2x_api *api) {
    framework = cc2x_framework_from_core(api);
    if (!framework || !(framework->capabilities() & CC2X_FRAMEWORK_LUA_BINDINGS)) return 0;
    return framework->vm_bind("example_c", "sum", sum, NULL) != 0;
}
