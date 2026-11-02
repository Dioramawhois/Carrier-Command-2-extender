#include "api/mods/workshop_upload.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace {
int failures = 0;
int begins = 0, commits = 0, aborts = 0, byteCalls = 0;
std::array<std::uint32_t, 3> sites{};
std::array<std::array<std::uint8_t, 5>, 3> expectedBytes{}, replacementBytes{};
const char* owner = nullptr;

void PatchBegin(const char* name) { ++begins; owner = name; }
void PatchAbort() { ++aborts; }
int PatchCommit() { ++commits; return 1; }
int PatchBytes(std::uint32_t va, std::uint32_t length, const std::uint8_t* expected,
               const std::uint8_t* replacement) {
  if (byteCalls >= static_cast<int>(sites.size()) || length != 5) return 0;
  const auto i = static_cast<std::size_t>(byteCalls++);
  sites[i] = va;
  std::memcpy(expectedBytes[i].data(), expected, 5);
  std::memcpy(replacementBytes[i].data(), replacement, 5);
  return 1;
}
void Log(const char*, int, const char*, ...) {}
std::uintptr_t Va(std::uint32_t imageVa) { return imageVa; }

cc2x_api MakeApi(const char* build) {
  cc2x_api api{};
  api.api_version = 8;
  api.size = sizeof(api);
  api.game_build = build;
  api.module_base = 0x00400000;
  api.log = Log;
  api.va = Va;
  api.patch_begin = PatchBegin;
  api.patch_abort = PatchAbort;
  api.patch_commit = PatchCommit;
  api.patch_bytes = PatchBytes;
  return api;
}
void Check(bool ok, const char* message) {
  if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
std::uint32_t RelativeTarget(std::uint32_t site, const std::array<std::uint8_t, 5>& call) {
  std::uint32_t displacement = 0;
  std::memcpy(&displacement, call.data() + 1, sizeof(displacement));
  return site + 5 + displacement;
}
} // namespace

int main() {
  auto unsupported = MakeApi("future-build");
  Check(!cc2::api::mods::InstallWorkshopUploadSupport(&unsupported), "unsupported build fails closed");
  Check(begins == 0 && byteCalls == 0, "unsupported build does not stage patches");

  auto supported = MakeApi("1.5.18-win32-r2");
  Check(cc2::api::mods::InstallWorkshopUploadSupport(&supported), "supported profile stages and commits");
  Check(begins == 1 && commits == 1 && aborts == 0 && byteCalls == 3, "validator and both callsites commit atomically");
  Check(owner && std::strcmp(owner, "native_bridge.workshop_upload") == 0, "dedicated patch owner");
  Check(sites[0] == 0x00644a02 && sites[1] == 0x0064542f && sites[2] == 0x00644b5f,
        "exact validator, content-copy and preparation callsites");
  Check(expectedBytes[0] == std::array<std::uint8_t, 5>{0xb9, 0xa0, 0x05, 0x00, 0x00},
        "validator expected bytes");
  Check(replacementBytes[0] == std::array<std::uint8_t, 5>{0xeb, 0x43, 0x90, 0x90, 0x90},
        "only extension rejection is bypassed");
  Check(expectedBytes[1] == std::array<std::uint8_t, 5>{0xe8, 0x5c, 0x01, 0x00, 0x00},
        "content worker callsite expected bytes");
  Check(expectedBytes[2] == std::array<std::uint8_t, 5>{0xe8, 0x6c, 0x62, 0xdd, 0xff},
        "preparation callsite expected bytes");
  Check(replacementBytes[1][0] == 0xe8 && replacementBytes[2][0] == 0xe8,
        "replacement callsites remain relative calls");
  Check(RelativeTarget(sites[1], replacementBytes[1]) != 0x00645590,
        "content callsite routes through stock-first fallback wrapper");
  Check(RelativeTarget(sites[2], replacementBytes[2]) != 0x0041add0,
        "preparation callsite captures current temp root");
  Check(cc2::api::mods::InstallWorkshopUploadSupport(&supported), "repeated installation is idempotent");
  Check(begins == 1 && byteCalls == 3, "idempotent call does not patch twice");
  if (failures) return 1;
  std::cout << "PASS Workshop upload patch profile and transactional callsite contract\n";
  return 0;
}
