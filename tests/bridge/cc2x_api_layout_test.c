#include "cc2x.h"
#include <stddef.h>

#define CC2X_STATIC_ASSERT(condition, name) typedef char static_assert_##name[(condition) ? 1 : -1]
CC2X_STATIC_ASSERT(CC2X_API_VERSION == 8, api_version_is_eight);
CC2X_STATIC_ASSERT(offsetof(cc2x_api, carrier_throttle) ==
                   offsetof(cc2x_api, patch_bytes) + sizeof(((cc2x_api *)0)->patch_bytes),
                   api6_follows_api5_prefix);
CC2X_STATIC_ASSERT(offsetof(cc2x_api, animation_speed) ==
                   offsetof(cc2x_api, carrier_throttle) + sizeof(((cc2x_api *)0)->carrier_throttle),
                   api6_fields_are_append_only);
CC2X_STATIC_ASSERT(offsetof(cc2x_api, carrier_hangar_spawn) ==
                   offsetof(cc2x_api, animation_speed) + sizeof(((cc2x_api *)0)->animation_speed),
                   api6_hangar_field_is_append_only);
CC2X_STATIC_ASSERT(offsetof(cc2x_api, on_hangar_spawn_result) ==
                   offsetof(cc2x_api, carrier_hangar_spawn) + sizeof(((cc2x_api *)0)->carrier_hangar_spawn),
                   api6_hangar_result_subscription_is_append_only);
CC2X_STATIC_ASSERT(offsetof(cc2x_api, lua_add_global) ==
                   offsetof(cc2x_api, on_hangar_spawn_result) + sizeof(((cc2x_api *)0)->on_hangar_spawn_result),
                   api8_global_lua_registration_is_append_only);

int main(void) {
    return CC2X_ANIMATION_ROUTES == 0 && CC2X_ANIMATION_RUNWAY == 1 &&
           CC2X_ANIMATION_LIFT == 2 && CC2X_ANIMATION_CRANE == 3 ? 0 : 1;
}
