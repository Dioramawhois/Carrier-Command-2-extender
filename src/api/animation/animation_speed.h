#pragma once

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include "cc2x.h"

namespace cc2::api::animation {

enum class Group : std::uint32_t { kRoutes, kRunway, kLift, kCrane };
// Recovered profiles: routes/lift <=50, runway <=100, crane <=10. Integers only.
bool SetSpeed(Group group, std::uint32_t multiplier);
bool IsReady();
// Diagnostic-only counters in CC2_ANIMATION_TRACE builds; cumulative per process.
void ReadRoutePatchHitCounts(std::uint32_t counts[3]);
// Bootstrap only. Authenticates EXE, validates ASLR-adjusted instructions,
// installs fixed caves with neutral factors. Never invoked from a Lua setter.
// Startup-only transaction through the CC2X patch owner/overlap engine.
DWORD InitializeWithApi(const cc2x_api* api);

#ifdef CC2_ANIMATION_TESTING
// Test fixture supplies a private relocated PE image; never patches the game.
DWORD InstallFixture(void* base, std::size_t bytes, const char* build);
bool VerifyFixtureTarget(const wchar_t* path);
bool FixtureHasLegacyPatchProfile(const wchar_t* path);
#endif

}  // namespace cc2::api::animation
