#include "api/mods/incompatibility.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace {
int failures = 0;
int begins = 0, commits = 0, aborts = 0, byte_calls = 0;
std::uint32_t patched_va = 0;
std::array<std::uint8_t, 2> before{}, after{};
const char* owner = nullptr;

void PatchBegin(const char* name) { ++begins; owner = name; }
void PatchAbort() { ++aborts; }
int PatchCommit() { ++commits; return 1; }
int PatchBytes(std::uint32_t va, std::uint32_t len, const std::uint8_t* expected,
               const std::uint8_t* replacement) {
  ++byte_calls;
  patched_va = va;
  if (len != 2) return 0;
  std::memcpy(before.data(), expected, 2);
  std::memcpy(after.data(), replacement, 2);
  return 1;
}
void Log(const char*, int, const char*, ...) {}

cc2x_api MakeApi(const char* build) {
  cc2x_api api{};
  api.api_version = 8;
  api.size = sizeof(api);
  api.game_build = build;
  api.log = Log;
  api.patch_begin = PatchBegin;
  api.patch_abort = PatchAbort;
  api.patch_commit = PatchCommit;
  api.patch_bytes = PatchBytes;
  return api;
}
void Check(bool ok, const char* message) {
  if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
} // namespace

int main() {
  auto unsupported = MakeApi("future-build");
  Check(!cc2::api::mods::InstallCompatibilityBypass(&unsupported), "unsupported build fails closed");
  Check(begins == 0 && byte_calls == 0, "unsupported build does not stage a patch");

  // State is process-lifetime and intentionally not resettable; use a fresh
  // test process per case. The supported profile is exercised by this binary.
  auto supported = MakeApi("1.5.18-win32-r2");
  Check(cc2::api::mods::InstallCompatibilityBypass(&supported), "supported profile stages and commits");
  Check(begins == 1 && commits == 1 && aborts == 0 && byte_calls == 1, "single atomic patch transaction");
  Check(owner && std::strcmp(owner, "native_bridge.mod_compatibility") == 0, "dedicated patch owner");
  Check(patched_va == 0x006474da, "exact image VA");
  Check(before == std::array<std::uint8_t, 2>{0x75, 0x27}, "expected original JNE bytes");
  Check(after == std::array<std::uint8_t, 2>{0xeb, 0x27}, "replacement preserves branch target");
  Check(cc2::api::mods::InstallCompatibilityBypass(&supported), "repeated call is idempotent");
  Check(begins == 1 && byte_calls == 1, "idempotent call does not patch twice");
  if (failures) return 1;
  std::cout << "PASS mod compatibility branch profile and transactional patch contract\n";
  return 0;
}
