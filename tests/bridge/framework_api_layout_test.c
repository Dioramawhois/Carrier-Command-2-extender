/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cc2x_framework.h"
#include "cc2x_carrier.h"
#include "cc2x_settings.h"
#include <stdio.h>
_Static_assert(sizeof(((cc2x_framework_api *)0)->interface_id) == 8, "interface identity width");
_Static_assert(offsetof(cc2x_framework_api, capabilities) == 16, "stable named interface prefix");
_Static_assert(sizeof(((cc2x_button_info *)0)->name) == 128, "bounded names");
_Static_assert(sizeof(((cc2x_message *)0)->topic) == 65, "bounded message topics");
_Static_assert(sizeof(cc2x_recovery_options)==16,"carrier options layout");
_Static_assert(sizeof(cc2x_recovery_status)==40,"carrier status layout");
_Static_assert(sizeof(cc2x_dock_result)==32,"carrier completion layout");
_Static_assert(offsetof(cc2x_carrier_api,available)==16,"named carrier header layout");
_Static_assert(offsetof(cc2x_settings_api,number)==16,"named settings header layout");
int main(void) {
    cc2x_framework_api api = {0};
    if(cc2x_carrier_resolve(NULL)||cc2x_settings_resolve(NULL))return 3;
    if (cc2x_framework_resolve(NULL) || cc2x_framework_compatible(NULL) || cc2x_framework_compatible(&api)) return 1;
    api.version = CC2X_FRAMEWORK_VERSION; api.size = sizeof(api); api.interface_id = CC2X_FRAMEWORK_ID;
    if (cc2x_framework_compatible(&api)) return 2; /* mandatory callbacks absent */
    puts("PASS C-compatible framework header, fixed records, named version/identity/size guards");
    return 0;
}
