// SPDX-License-Identifier: GPL-3.0-or-later
// Optional C++ consumer of the same public C ABI. Changes no gameplay.
#include "cc2x_framework.h"
#include <cstdint>
namespace {
const cc2x_framework_api* framework = nullptr;
int sum(cc2x_vm* vm, void*) {
    std::int64_t a=0, b=0;
    if (framework->vm->gettop(vm)!=2 || !framework->vm->tointeger(vm,1,&a) ||
        !framework->vm->tointeger(vm,2,&b) ||
        (b>0 && a>INT64_MAX-b) || (b<0 && a<INT64_MIN-b)) return 0;
    framework->vm->pushinteger(vm,a+b);return 1;
}
}
extern "C" CC2X_EXPORT int cc2x_plugin_load(const cc2x_api* api) {
    framework=cc2x_framework_from_core(api);
    return framework && (framework->capabilities()&CC2X_FRAMEWORK_LUA_BINDINGS) &&
        framework->vm_bind("example_cpp","sum",sum,nullptr)!=0;
}
