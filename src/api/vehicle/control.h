// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "cc2x.h"
namespace cc2::api::vehicle {
// Engine outputs, not named flight modes. Only checked live wing vehicles and
// the exact supported instruction layout are accepted. No persistent policy.
bool ReadThrottle(const cc2x_api* api,const void* vehicle,float* result);
bool WriteThrottle(const cc2x_api* api,void* vehicle,double value);
}
