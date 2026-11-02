#include "incompatibility.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cc2::api::mods {
namespace {
constexpr char kSupportedBuild[] = "1.5.18-win32-r2";
constexpr std::uint32_t kBranchVa = 0x006474da;
constexpr std::uint8_t kExpected[] = {0x75, 0x27}; // JNE: invoke stock conflict-disable path
constexpr std::uint8_t kReplacement[] = {0xeb, 0x27}; // JMP: skip only that path
volatile LONG g_state = 0; // 0=not attempted, 1=installed, -1=failed/unsupported

void Log(const cc2x_api* api, int level, const char* event, const char* reason) {
  if (api && api->log)
    api->log("native_bridge.mod_compatibility", level,
      "event=%s build=%s va=0x%08lx reason=%s",
      event, api->game_build ? api->game_build : "unknown",
      static_cast<unsigned long>(kBranchVa), reason);
}
} // namespace

bool InstallCompatibilityBypass(const cc2x_api* api) {
  if (InterlockedCompareExchange(&g_state, 0, 0) == 1) return true;
  if (!api || api->api_version < 5 ||
      api->size < offsetof(cc2x_api, patch_bytes) + sizeof(api->patch_bytes) ||
      !api->patch_begin || !api->patch_bytes || !api->patch_commit || !api->patch_abort) {
    InterlockedExchange(&g_state, -1);
    Log(api, CC2X_LOG_WARN, "MOD_COMPATIBILITY_BYPASS_UNAVAILABLE", "patch_api_unavailable");
    return false;
  }
  if (!api->game_build || std::strcmp(api->game_build, kSupportedBuild) != 0) {
    InterlockedExchange(&g_state, -1);
    Log(api, CC2X_LOG_WARN, "MOD_COMPATIBILITY_BYPASS_UNAVAILABLE", "unsupported_build");
    return false;
  }

  // The only changed control-flow edge is the conditional that enters the
  // loop which calls this same setter with enabled=false for other mods.
  api->patch_begin("native_bridge.mod_compatibility");
  if (!api->patch_bytes(kBranchVa, sizeof(kExpected), kExpected, kReplacement)) {
    api->patch_abort();
    InterlockedExchange(&g_state, -1);
    Log(api, CC2X_LOG_WARN, "MOD_COMPATIBILITY_BYPASS_UNAVAILABLE", "expected_bytes_mismatch_or_overlap");
    return false;
  }
  if (!api->patch_commit()) {
    api->patch_abort();
    InterlockedExchange(&g_state, -1);
    Log(api, CC2X_LOG_WARN, "MOD_COMPATIBILITY_BYPASS_UNAVAILABLE", "patch_commit_failed");
    return false;
  }
  InterlockedExchange(&g_state, 1);
  Log(api, CC2X_LOG_INFO, "MOD_COMPATIBILITY_AUTO_DISABLE_BYPASSED", "conflict_disable_branch_skipped");
  return true;
}

} // namespace cc2::api::mods
