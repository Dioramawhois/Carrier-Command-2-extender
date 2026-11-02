#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace cc2::api::carrier {

// FUN_006ECA60 changes carrier throttle control_factors by 0.025 per
// positive/negative input update. This helper mirrors that discrete step for
// the Bridge-owned carrier command path.
inline bool ApplyThrottleSteps(float current, const std::int8_t* steps,
                               std::size_t count, float* result) {
  if (!result || (count != 0 && !steps) || !std::isfinite(current)) return false;
  float value = current;
  for (std::size_t i = 0; i < count; ++i) {
    if (steps[i] != -1 && steps[i] != 1) return false;
    value += static_cast<float>(steps[i]) * 0.025f;
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
  }
  *result = value;
  return true;
}

}  // namespace cc2::api::carrier
