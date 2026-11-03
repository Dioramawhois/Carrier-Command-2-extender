#include "runtime/host.h"
#include "api/carrier/hangar_spawn.h"
#include "api/carrier/pe_relocation_guard.h"
#include "api/vehicle/definition_adapter.h"

#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace cc2::runtime {
namespace {
constexpr uint32_t kHangarEvent = 0x26;       // exact event constant at vanilla case 0x0c
struct HangarProfile {
  const char* build;
  const char* sha256;
};
constexpr std::uintptr_t kLegacyFunctions[]{
  0x005ec130,0x005ee700,0x005ee6a0,0x00850670,0x0040fb60,0x005e9c10,
  0x0040ffa0,0x005ec2c0,0x005e9d10,0x005ee8c0,0x005e44f0,0x0041a730,
  0x005ea870,0x0088e080,0x00411570,0x00572090,0x005720b0,0x004380b0,0x00565870};
constexpr std::uintptr_t kSteam26277078Functions[]{
  0x005ec270,0x005ee840,0x005ee7e0,0x008507a0,0x0040fb60,0x005e9d50,
  0x0040ffa0,0x005ec400,0x005e9e50,0x005eea00,0x005e4630,0x0041a730,
  0x005ea9b0,0x0088e1c0,0x00411570,0x005721e0,0x00572200,0x004380b0,0x00565990};
constexpr BYTE kSteam26277078EntryBytes[][16]{
  {0x55,0x8b,0xec,0x8b,0x45,0x08,0x83,0xc0,0xfe,0x83,0xf8,0x56,0x77,0x28,0x0f,0xb6},
  {0x55,0x8b,0xec,0x6a,0xff,0x68,0x9d,0xd2,0xc7,0x00,0x64,0xa1,0x00,0x00,0x00,0x00},
  {0x55,0x8b,0xec,0x51,0x53,0x8b,0xd9,0x33,0xc0,0x56,0x57,0x39,0x43,0x44,0x74,0x2d},
  {0x55,0x8b,0xec,0x56,0x8b,0x71,0x08,0x03,0x75,0x08,0x8b,0xc6,0x2b,0x41,0x04,0x3b},
  {0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x10,0x8b,0x45,0x08,0xf3,0x0f,0x10,0x40},
  {0x8b,0x91,0x90,0x00,0x00,0x00,0x8b,0x81,0x8c,0x00,0x00,0x00,0x56,0x8b,0xb1,0x94},
  {0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x50,0xa1,0x44,0xdb,0xe1,0x00,0x33,0xc4},
  {0x55,0x8b,0xec,0x83,0xe4,0xf8,0x81,0xec,0x24,0x01,0x00,0x00,0xa1,0x44,0xdb,0xe1},
  {0x55,0x8b,0xec,0x83,0xe4,0xf8,0x81,0xec,0x3c,0x01,0x00,0x00,0xa1,0x44,0xdb,0xe1},
  {0x55,0x8b,0xec,0x6a,0xff,0x68,0xdd,0x1b,0xca,0x00,0x64,0xa1,0x00,0x00,0x00,0x00},
  {0xc7,0x01,0x00,0x00,0x00,0x00,0x8b,0xc1,0xc7,0x41,0x04,0xff,0xff,0xff,0xff,0xc7},
  {0xc7,0x01,0x00,0x00,0x00,0x00,0x8b,0xc1,0xc7,0x41,0x04,0x00,0x00,0x00,0x00,0xc3},
  {0x55,0x8b,0xec,0x8b,0x89,0x5c,0x01,0x00,0x00,0x8b,0x45,0x08,0x8b,0x0c,0x81,0x85},
  {0x55,0x8b,0xec,0x6a,0xff,0x68,0x7e,0xf9,0xcc,0x00,0x64,0xa1,0x00,0x00,0x00,0x00},
  {0x55,0x8b,0xec,0x6a,0xff,0x68,0xf0,0xc4,0xc7,0x00,0x64,0xa1,0x00,0x00,0x00,0x00},
  {0x8b,0x41,0x2c,0xc3,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc},
  {0x55,0x8b,0xec,0x57,0x8b,0xf9,0x8b,0x4d,0x08,0x3b,0x4f,0x2c,0x73,0x1d,0x8b,0x57},
  {0x8b,0x01,0xc3,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc},
  {0x55,0x8b,0xec,0x6a,0xff,0x68,0x5d,0x7f,0xc9,0x00,0x64,0xa1,0x00,0x00,0x00,0x00},
};
struct EntryRelocations { std::uint8_t count; pe_guard::RelocationField fields[2]; };
// HIGHLOW operand offsets within the corresponding 16-byte entry signatures,
// extracted from the exact executable whose SHA-256 is pinned below. The
// image's .reloc section is IMAGE_SCN_MEM_DISCARDABLE and cannot be relied on
// being readable after Windows has applied relocations.
constexpr EntryRelocations kSteam26277078EntryRelocations[]{
  {0,{{0,0},{0,0}}},{1,{{6,0x00c7d29d},{0,0}}},{0,{{0,0},{0,0}}},
  {0,{{0,0},{0,0}}},{0,{{0,0},{0,0}}},{0,{{0,0},{0,0}}},
  {1,{{10,0x00e1db44},{0,0}}},{1,{{13,0x00e1db44},{0,0}}},
  {1,{{13,0x00e1db44},{0,0}}},{1,{{6,0x00ca1bdd},{0,0}}},
  {0,{{0,0},{0,0}}},{0,{{0,0},{0,0}}},{0,{{0,0},{0,0}}},
  {1,{{6,0x00ccf97e},{0,0}}},{1,{{6,0x00c7c4f0},{0,0}}},
  {0,{{0,0},{0,0}}},{0,{{0,0},{0,0}}},{0,{{0,0},{0,0}}},
  {1,{{6,0x00c97f5d},{0,0}}},
};
static_assert(std::size(kSteam26277078Functions)==std::size(kSteam26277078EntryBytes));
static_assert(std::size(kSteam26277078Functions)==std::size(kSteam26277078EntryRelocations));
constexpr char kLegacyBuild[]="1.5.18-win32";
constexpr char kLegacySha256[]="1c9a01c3007cb2bec2a04d716812cf9f24c87efa11056395c89ddd7d2c341026";
constexpr HangarProfile kLegacyProfile{kLegacyBuild,kLegacySha256};
constexpr char kSteamBuild[]="1.5.18-win32-r2";
constexpr char kSteamSha256[]="461e12623bacb7de29ca27b4614425c87e8df2c8327a5e958a957c850effe42a";
constexpr HangarProfile kSteamProfile{kSteamBuild,kSteamSha256};
constexpr std::uint32_t kSteamImageBase=0x00400000;
constexpr std::size_t kSteamImageSize=0x00af7000;

struct Mat34 { double v[12]; };

template<class T> T At(void* p, size_t offset) {
  return *reinterpret_cast<T*>(static_cast<unsigned char*>(p) + offset);
}

bool Readable(const void* address, size_t bytes) {
  if (!address || !bytes) return false;
  MEMORY_BASIC_INFORMATION info{};
  if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) return false;
  const DWORD protection = info.Protect & 0xff;
  if (protection != PAGE_READONLY && protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
      protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE &&
      protection != PAGE_EXECUTE_WRITECOPY) return false;
  const auto begin = reinterpret_cast<uintptr_t>(address);
  const auto region = reinterpret_cast<uintptr_t>(info.BaseAddress);
  if (begin < region) return false;
  const auto offset = begin - region;
  return offset <= info.RegionSize && bytes <= info.RegionSize - offset;
}

void* ImageVa(const cc2x_api* api, uint32_t imageVa);

bool HashRunningImage(char output[65]) {
  wchar_t path[32768]{};
  const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
  if (!length || length >= std::size(path)) return false;
  HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD objectBytes = 0, returned = 0, hashBytes = 0;
  std::vector<UCHAR> object, digest;
  bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
            BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes),
                              sizeof(objectBytes), &returned, 0) >= 0 &&
            BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashBytes),
                              sizeof(hashBytes), &returned, 0) >= 0 && hashBytes == 32;
  if (ok) {
    object.resize(objectBytes); digest.resize(hashBytes);
    ok = BCryptCreateHash(algorithm, &hash, object.data(), objectBytes, nullptr, 0, 0) >= 0;
  }
  std::array<UCHAR, 64 * 1024> buffer{};
  while (ok) {
    DWORD read = 0;
    if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) { ok = false; break; }
    if (!read) break;
    if (BCryptHashData(hash, buffer.data(), read, 0) < 0) { ok = false; break; }
  }
  if (ok) ok = BCryptFinishHash(hash, digest.data(), hashBytes, 0) >= 0;
  if (hash) BCryptDestroyHash(hash);
  if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  CloseHandle(file);
  if (!ok) return false;
  static constexpr char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < digest.size(); ++i) {
    output[i * 2] = hex[digest[i] >> 4];
    output[i * 2 + 1] = hex[digest[i] & 15];
  }
  output[64] = '\0';
  return true;
}

const HangarProfile* ProfileFor(const cc2x_api* api) {
  if (!api || !api->game_build) return nullptr;
  if (std::strcmp(api->game_build,kLegacyBuild)==0) return &kLegacyProfile;
  if (std::strcmp(api->game_build,kSteamBuild)==0) return &kSteamProfile;
  return nullptr;
}

const std::uintptr_t* FunctionAddresses(const cc2x_api* api) {
  if (!api || !api->game_build) return nullptr;
  if (std::strcmp(api->game_build,kLegacyBuild)==0) return kLegacyFunctions;
  if (std::strcmp(api->game_build,kSteamBuild)==0) return kSteam26277078Functions;
  return nullptr;
}

bool EntryGuardsMatch(const cc2x_api* api, const HangarProfile* profile,
                      std::uint32_t* badVa=nullptr,
                      pe_guard::MatchDiagnostics* badMatch=nullptr,
                      std::size_t* badIndex=nullptr) {
  if (!api || !profile || profile!=&kSteamProfile) return profile==&kLegacyProfile;
  const auto* image=reinterpret_cast<const BYTE*>(api->module_base);
  for(std::size_t i=0;i<std::size(kSteam26277078Functions);++i) {
    const auto va=static_cast<std::uint32_t>(kSteam26277078Functions[i]);
    if(va<kSteamImageBase) { if(badVa)*badVa=va; return false; }
    const auto rva=va-kSteamImageBase;
    auto* entry=image+rva;
    pe_guard::MatchDiagnostics match{};
    const bool entryReadable=Readable(entry,16);
    const bool matches=entryReadable && pe_guard::MatchLoadedBytes(image,kSteamImageSize,rva,
        kSteam26277078EntryBytes[i],sizeof(kSteam26277078EntryBytes[i]),
        kSteam26277078EntryRelocations[i].fields,
        kSteam26277078EntryRelocations[i].count,
        static_cast<std::uint32_t>(kSteamImageBase),&match);
    if(!matches) {
      if(!entryReadable) match.reason="entry_unreadable";
      if(badVa)*badVa=va;
      if(badMatch)*badMatch=match;
      if(badIndex)*badIndex=i;
      return false;
    }
  }
  return true;
}

void FormatBytes(const BYTE* bytes,std::size_t count,char* output,std::size_t outputSize) {
  if(!output||!outputSize) return;
  std::size_t used=0;
  for(std::size_t i=0;i<count&&used+2<outputSize;++i) {
    const int written=std::snprintf(output+used,outputSize-used,"%02x",bytes[i]);
    if(written!=2) break;
    used+=2;
  }
  output[used]='\0';
}

bool SupportedImage(const cc2x_api* api, std::string* reason) {
  const auto* profile=ProfileFor(api);
  if (!profile) {
    *reason = "unsupported_game_build"; return false;
  }
  if (reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) != api->module_base) {
    *reason = "module_base_mismatch"; return false;
  }
  char hash[65]{};
  if (!HashRunningImage(hash)) { *reason = "running_exe_hash_unavailable"; return false; }
  if (std::strcmp(hash, profile->sha256) != 0) { *reason = "running_exe_hash_mismatch"; return false; }
  std::uint32_t badVa=0;
  pe_guard::MatchDiagnostics match{};
  std::size_t badIndex=0;
  if(!EntryGuardsMatch(api,profile,&badVa,&match,&badIndex)) {
    char expected[33]{},normalized[33]{},actual[33]{};
    FormatBytes(kSteam26277078EntryBytes[badIndex],16,expected,sizeof(expected));
    if(match.normalizedAvailable)
      FormatBytes(match.normalized,16,normalized,sizeof(normalized));
    const auto* image=reinterpret_cast<const BYTE*>(api->module_base);
    if(Readable(image+(badVa-kSteamImageBase),16))
      FormatBytes(image+(badVa-kSteamImageBase),16,actual,sizeof(actual));
    char detail[384]{};
    std::snprintf(detail,sizeof(detail),
      "native_entry_signature_mismatch_va=0x%08lx index=%lu cause=%s image_base=0x%08lx mapped_header_base=0x%08lx profile_preferred_base=0x%08lx relocation_delta=0x%08lx expected=%s normalized=%s actual=%s",
      static_cast<unsigned long>(badVa),static_cast<unsigned long>(badIndex),
      match.reason?match.reason:"unknown",
      static_cast<unsigned long>(api->module_base),
      static_cast<unsigned long>(match.mappedHeaderImageBase),
      static_cast<unsigned long>(match.profilePreferredImageBase),
      static_cast<unsigned long>(match.relocationDelta),expected,
      match.normalizedAvailable?normalized:"unavailable",actual);
    *reason=detail;
    return false;
  }
  *reason = "build_and_sha256_verified";
  return true;
}

void* ImageVa(const cc2x_api* api, uint32_t imageVa) {
  return api && api->va ? reinterpret_cast<void*>(api->va(imageVa)) : nullptr;
}

using FindSlotFn = uint32_t (__thiscall *)(void*, uint32_t, uint32_t);
using FactoryFn = void* (__thiscall *)(void*, void*, const Mat34*, uint32_t, void*);
using GetVehicleFn = void* (__thiscall *)(void*, uint32_t);
using SlotTransformFn = void* (__thiscall *)(void*, uint32_t);
using FloatMatrixFn = void (__thiscall *)(Mat34*, const float*);
using GetTransformFn = void* (__thiscall *)(void*);
using DockFn = void (__thiscall *)(void*, void*, void*, uint32_t);
using PostDockFn = void (__thiscall *)(void*, uint32_t, void*, uint32_t, uint32_t);
using RetireQueueFn = void (__thiscall *)(void*, void*, uint32_t);
using VoidThisFn = void (__thiscall *)(void*);
using VehicleEventFn = void (__thiscall *)(void*, uint32_t, void*);
using ClientCountFn = uint32_t (__thiscall *)(void*);
using ClientAtFn = void* (__thiscall *)(void*, uint32_t);
using ClientSenderFn = void* (__thiscall *)(void*);
using SendEventFn = void (__thiscall *)(void*, uint32_t, uint32_t, uint32_t);
using ServerPublishFn = void (__thiscall *)(void*, uint32_t, uint32_t, void*);

void ComposeNative(void* target, Mat34* output, const Mat34* carrierWorld, const Mat34* local) {
  // FUN_0040FFA0 is ECX=out, EDX=carrier transform, one caller-cleaned stack pointer.
  __asm {
    mov eax, target
    mov ecx, output
    mov edx, carrierWorld
    push local
    call eax
    add esp, 4
  }
}

void SerializeNative(void* target, void* message, void* eventData) {
  // Vanilla case 0x0c calls FUN_0088E080 with identical event pointers in EDX and [ESP+4].
  __asm {
    mov eax, target
    mov ecx, message
    mov edx, eventData
    push eventData
    call eax
    add esp, 4
  }
}

} // namespace

bool ValidateHangarSpawnImage(const cc2x_api* api,std::string* reason) {
  if(!reason) return false;
  return SupportedImage(api,reason);
}

void Host::ProcessHangarSpawn(void* server) {
  if (hangarSpawnRollbackPending_) {
    const auto* api = Api();
    void* manager = static_cast<unsigned char*>(server) + 0x344;
    const auto* profile=ProfileFor(api);
    const auto* addresses=FunctionAddresses(api);
    if(!profile||!addresses||!EntryGuardsMatch(api,profile)) {
      LogHangarSpawn("HANGAR_SPAWN_ROLLBACK_UNCONFIRMED", "profile_or_lookup_signature_unavailable; no retry");
      return;
    }
    auto getVehicle = api && api->va
        ? reinterpret_cast<GetVehicleFn>(api->va(static_cast<uint32_t>(addresses[2]))) : nullptr;
    if (!getVehicle || !Readable(manager, 0x68)) {
      LogHangarSpawn("HANGAR_SPAWN_ROLLBACK_UNCONFIRMED", "manager_or_lookup_unavailable; no retry");
      return;
    }
    void* remaining = getVehicle(manager, static_cast<uint32_t>(hangarSpawnRollbackId_));
    if (!remaining) {
      char detail[160]{};
      std::snprintf(detail, sizeof(detail), "generation=%llu vehicle_id=%llu removed_from_manager=1",
                    metrics_.generation, hangarSpawnRollbackId_);
      LogHangarSpawn("HANGAR_SPAWN_ROLLBACK_CONFIRMED", detail);
      hangarSpawnRollbackId_ = 0;
      hangarSpawnRollbackTicks_ = 0;
      hangarSpawnRollbackPending_ = false;
      hangarSpawnUnsafeLatched_ = false;
      return;
    }
    if (hangarSpawnRollbackTicks_ != UINT_MAX) ++hangarSpawnRollbackTicks_;
    if (hangarSpawnRollbackTicks_ == 1 || hangarSpawnRollbackTicks_ % 90 == 0) {
      char detail[160]{};
      std::snprintf(detail, sizeof(detail), "generation=%llu vehicle_id=%llu ticks=%u; spawn remains disabled",
                    metrics_.generation, hangarSpawnRollbackId_, hangarSpawnRollbackTicks_);
      LogHangarSpawn("HANGAR_SPAWN_ROLLBACK_PENDING", detail);
    }
    return;
  }
  if (!server || server_ != server || !hangarSpawnInFlight_ ||
      hangarSpawnAttemptGeneration_ != metrics_.generation) {
    LogHangarSpawn("HANGAR_SPAWN_REJECTED", "stale_server_or_generation"); return;
  }
  char detail[256]{};
  std::snprintf(detail, sizeof(detail), "generation=%llu server=%p", metrics_.generation, server);
  LogHangarSpawn("SERVER_READY", detail);
  const auto* api = Api();
  auto* module = GetModuleHandleW(nullptr);
  std::string reason;
  if (!SupportedImage(api, &reason)) {
    LogHangarSpawn("HANGAR_SPAWN_PREFLIGHT_FAIL", reason.c_str()); return;
  }
  const auto* imageVas=FunctionAddresses(api);
  if(!imageVas) { LogHangarSpawn("HANGAR_SPAWN_PREFLIGHT_FAIL","native_profile_functions_unavailable"); return; }
  void* f[19]{};
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
  if (!Readable(dos, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
    LogHangarSpawn("HANGAR_SPAWN_PREFLIGHT_FAIL", "invalid_pe_headers"); return;
  }
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const unsigned char*>(dos) + dos->e_lfanew);
  if (!Readable(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC || !nt->OptionalHeader.SizeOfImage) {
    LogHangarSpawn("HANGAR_SPAWN_PREFLIGHT_FAIL", "invalid_pe32_image"); return;
  }
  const uintptr_t imageBase = reinterpret_cast<uintptr_t>(module);
  const uintptr_t imageEnd = imageBase + nt->OptionalHeader.SizeOfImage;
  for (size_t i = 0; i < std::size(f); ++i) {
    f[i] = ImageVa(api, static_cast<uint32_t>(imageVas[i]));
    MEMORY_BASIC_INFORMATION region{};
    if (!f[i] || reinterpret_cast<uintptr_t>(f[i]) < imageBase ||
        reinterpret_cast<uintptr_t>(f[i]) >= imageEnd ||
        !VirtualQuery(f[i], &region, sizeof(region)) || region.State != MEM_COMMIT ||
        region.AllocationBase != module || (region.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
        PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0) {
      LogHangarSpawn("HANGAR_SPAWN_PREFLIGHT_FAIL", "native_entry_resolution_failed"); return;
    }
  }
  if (!Readable(server, 0xdd8) || !Readable(static_cast<unsigned char*>(server) + 0x344, 0x68) ||
      !Readable(static_cast<unsigned char*>(server) + 0x514, 8)) {
    LogHangarSpawn("HANGAR_SPAWN_PREFLIGHT_FAIL", "server_roots_unreadable"); return;
  }

  const uint32_t carrierId=hangarSpawnCarrierId_,slot=hangarSpawnSlot_,chassisType=hangarSpawnType_;
  void* carrier=api&&api->find?api->find(CC2X_WORLD_VEHICLES,carrierId):nullptr;
  if(!carrier||!api->geti||static_cast<uint32_t>(api->geti(carrier,CC2X_VEHICLE_ID,0))!=carrierId) {
    LogHangarSpawn("HANGAR_SPAWN_PREFLIGHT_FAIL","requested_carrier_id_not_found");return;
  }
  std::snprintf(detail, sizeof(detail), "generation=%llu carrier_id=%u", metrics_.generation, carrierId);
  LogHangarSpawn("REQUESTED_CARRIER_RESOLVED", detail);
  if (!Readable(carrier, 0x1ac) || !Readable(static_cast<unsigned char*>(carrier) + 0x154, 0x10)) {
    LogHangarSpawn("HANGAR_SPAWN_PREFLIGHT_FAIL", "carrier_layout_unreadable"); return;
  }
  void* ownerTeam = At<void*>(carrier, 0x1a8);
  void* carrierDefinition = At<void*>(carrier, 0x88);
  void* definitionContainer = static_cast<unsigned char*>(server) + 0x1bc;
  void* vehicleManager = static_cast<unsigned char*>(server) + 0x344;
  void* eventManager = static_cast<unsigned char*>(server) + 0x514;
  if (!ownerTeam || !carrierDefinition || !Readable(ownerTeam, 0x1c) ||
      !Readable(carrierDefinition, 0xec) || !Readable(definitionContainer, 0x10)) {
    LogHangarSpawn("HANGAR_SPAWN_PREFLIGHT_FAIL", "owner_or_definition_context_missing"); return;
  }

  const uint32_t slotCount = At<uint32_t>(carrier, 0x158);
  void* slotRecords = At<void*>(carrier, 0x154);
  if(slot>=slotCount) { LogHangarSpawn("HANGAR_SPAWN_PREFLIGHT_FAIL","requested_slot_out_of_range");return; }
  if(!slotRecords||!Readable(static_cast<unsigned char*>(slotRecords)+static_cast<size_t>(slot)*0x70,0x70)) {
    LogHangarSpawn("HANGAR_SPAWN_PREFLIGHT_FAIL","requested_slot_record_unreadable");return;
  }
  if(At<uint32_t>(slotRecords,static_cast<size_t>(slot)*0x70+0x64)!=0) {
    LogHangarSpawn("HANGAR_SPAWN_PREFLIGHT_FAIL","requested_slot_occupied");return;
  }
  const auto findSlot = reinterpret_cast<FindSlotFn>(f[0]);
  const auto compatibilityType=cc2::api::vehicle::definition_adapter::CompatibilityType(chassisType);
  if(findSlot(carrier,compatibilityType,slot)!=slot) {
    LogHangarSpawn("HANGAR_SPAWN_PREFLIGHT_FAIL", "requested_slot_not_compatible_with_chassis_type"); return;
  }

  void* slotTransformRecord = reinterpret_cast<SlotTransformFn>(f[3])(
      static_cast<unsigned char*>(carrierDefinition) + 0xe0, slot);
  if (!slotTransformRecord || !Readable(static_cast<unsigned char*>(slotTransformRecord) + 0xc, 0x30)) {
    LogHangarSpawn("HANGAR_SPAWN_PREFLIGHT_FAIL", "slot_transform_missing"); return;
  }
  Mat34 localTransform{}, worldTransform{};
  reinterpret_cast<FloatMatrixFn>(f[4])(&localTransform,
      reinterpret_cast<const float*>(static_cast<unsigned char*>(slotTransformRecord) + 0xc));
  void* carrierTransform = reinterpret_cast<GetTransformFn>(f[5])(carrier);
  if (!carrierTransform || !Readable(carrierTransform, sizeof(Mat34))) {
    LogHangarSpawn("HANGAR_SPAWN_PREFLIGHT_FAIL", "carrier_world_transform_missing"); return;
  }
  ComposeNative(f[6], &worldTransform, static_cast<const Mat34*>(carrierTransform), &localTransform);

  std::snprintf(detail, sizeof(detail), "generation=%llu carrier_id=%u slot=%u type=%u source=stock_empty_bay_path",
                metrics_.generation, carrierId, slot, chassisType);
  LogHangarSpawn("HANGAR_SLOT", detail);
  LogHangarSpawn("HANGAR_SPAWN_PREFLIGHT_PASS", detail);
  void* newVehicle = nullptr;
  uint32_t newId = 0;
  std::snprintf(detail,sizeof(detail),"type=%u; inventory_debit=not_performed; source=public_api",chassisType);
  LogHangarSpawn("FACTORY_BEGIN", detail);
  uint32_t notified = 0;
  try {
    newVehicle = reinterpret_cast<FactoryFn>(f[1])(vehicleManager, definitionContainer,
        &worldTransform, chassisType, ownerTeam);
  } catch (...) {
    hangarSpawnUnsafeLatched_ = true;
    LogHangarSpawn("HANGAR_SPAWN_FACTORY_EXCEPTION", "factory outcome unknown; further requests blocked for this session");
  }
  if (!newVehicle) {
    LogHangarSpawn("HANGAR_SPAWN_FACTORY_FAIL", hangarSpawnUnsafeLatched_
        ? "FUN_005EE700 raised; outcome unknown and further requests blocked"
        : "FUN_005EE700 returned null; no vehicle object returned");
    return;
  }
  if (!Readable(newVehicle, 0x184)) {
    hangarSpawnUnsafeLatched_ = true;
    LogHangarSpawn("HANGAR_SPAWN_FACTORY_VERIFY_FAIL", "factory returned unreadable object; refusing further calls");
    return;
  }
  newId = At<uint32_t>(newVehicle, 0x80);
  hangarSpawnResultVehicleId_=newId;
  auto rollback = [&](const char* why) {
    reinterpret_cast<RetireQueueFn>(f[9])(vehicleManager, newVehicle, At<uint32_t>(server, 0xdd4));
    hangarSpawnRollbackId_ = newId;
    hangarSpawnRollbackTicks_ = 0;
    hangarSpawnRollbackPending_ = true;
    std::snprintf(detail, sizeof(detail), "generation=%llu vehicle_id=%u reason=%s",
                  metrics_.generation, newId, why);
    LogHangarSpawn("HANGAR_SPAWN_ROLLBACK_QUEUED", detail);
  };
  auto registered = reinterpret_cast<GetVehicleFn>(f[2])(vehicleManager, newId);
  if (!newId || registered != newVehicle) {
    rollback("manager_lookup_mismatch_before_dock");
    LogHangarSpawn("HANGAR_SPAWN_FACTORY_VERIFY_FAIL", "manager lookup did not return the new object; rollback queued");
    return;
  }
  std::snprintf(detail, sizeof(detail), "generation=%llu carrier_id=%u vehicle_id=%u object=%p definition=%u resolved_definition=%p",
                metrics_.generation, carrierId, newId, newVehicle, chassisType,
                reinterpret_cast<void*>(At<std::uintptr_t>(newVehicle, 0x88)));
  LogHangarSpawn("HANGAR_SPAWN_FACTORY_CREATED", detail);
  LogHangarSpawn("VEHICLE_REGISTERED", detail);

  auto** eventVtable = *reinterpret_cast<void***>(eventManager);
  MEMORY_BASIC_INFORMATION publishCode{};
  void* publishTarget = eventVtable && Readable(eventVtable, 2 * sizeof(void*)) ? eventVtable[1] : nullptr;
  if (!publishTarget || reinterpret_cast<uintptr_t>(publishTarget) < imageBase ||
      reinterpret_cast<uintptr_t>(publishTarget) >= imageEnd ||
      !VirtualQuery(publishTarget, &publishCode, sizeof(publishCode)) ||
      publishCode.State != MEM_COMMIT || publishCode.AllocationBase != module ||
      (publishCode.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0) {
    rollback("server_event_manager_unavailable");
    LogHangarSpawn("HANGAR_SPAWN_PUBLISH_FAIL", "server event manager slot 1 is not a supported executable target");
    return;
  }
  void* clientList = static_cast<unsigned char*>(server) + 0xc48;
  const uint32_t clients = reinterpret_cast<ClientCountFn>(f[15])(clientList);
  if (clients > 1024) {
    rollback("client_count_sanity_bound");
    LogHangarSpawn("HANGAR_SPAWN_PUBLISH_FAIL", "client count exceeded static sanity bound");
    return;
  }

  alignas(16) std::array<uint32_t, 13> vehicleEvent{};
  alignas(16) std::array<uint32_t, 2> networkMessage{};
  bool networkMessageInitialized = false;
  try {
    *reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(newVehicle) + 0x180) = 1;
    LogHangarSpawn("DOCK_BEGIN", "using vanilla FUN_005EC2C0 carrier dock bookkeeping");
    reinterpret_cast<DockFn>(f[7])(carrier, eventManager, newVehicle, slot);
    if (At<uint32_t>(slotRecords, static_cast<size_t>(slot) * 0x70 + 0x64) != newId) {
      rollback("carrier_slot_verification_failed"); LogHangarSpawn("HANGAR_SPAWN_DOCK_VERIFY_FAIL", "carrier slot did not retain the new vehicle id"); return;
    }
    reinterpret_cast<PostDockFn>(f[8])(newVehicle, 0, definitionContainer, kHangarEvent, 1);
    void* worldVehicle = api->find(CC2X_WORLD_VEHICLES, newId);
    if (worldVehicle != newVehicle) {
      rollback("not_visible_in_cc2x_server_world_snapshot");
      LogHangarSpawn("WORLD_VISIBLE", "visible=0; rollback queued before client publication");
      return;
    }
    std::snprintf(detail, sizeof(detail), "generation=%llu carrier_id=%u vehicle_id=%u visible=1",
                  metrics_.generation, carrierId, newId);
    LogHangarSpawn("WORLD_VISIBLE", detail);
    LogHangarSpawn("DOCK_RESULT", "slot_record_matches_vehicle_id=1; post_dock_sequence=called");

    // Reproduce the stock case-0x0c creation notification sequence (not a new message ABI).
    reinterpret_cast<VoidThisFn>(f[10])(vehicleEvent.data());
    reinterpret_cast<VoidThisFn>(f[11])(networkMessage.data());
    networkMessageInitialized = true;
    reinterpret_cast<VehicleEventFn>(f[12])(newVehicle, 0, vehicleEvent.data());
    SerializeNative(f[13], networkMessage.data(), vehicleEvent.data());
    reinterpret_cast<ServerPublishFn>(eventVtable[1])(eventManager, newId, 0, networkMessage.data());

    for (uint32_t i = 0; i < clients; ++i) {
      void* client = reinterpret_cast<ClientAtFn>(f[16])(clientList, i);
      if (!client || !Readable(static_cast<unsigned char*>(client) + 0x64, sizeof(void*))) continue;
      void* sender = reinterpret_cast<ClientSenderFn>(f[17])(static_cast<unsigned char*>(client) + 0x64);
      if (sender) { reinterpret_cast<SendEventFn>(f[18])(sender, newId, 0, kHangarEvent); ++notified; }
    }
    reinterpret_cast<VoidThisFn>(f[14])(networkMessage.data());
  } catch (...) {
    if (networkMessageInitialized) reinterpret_cast<VoidThisFn>(f[14])(networkMessage.data());
    rollback("post_factory_cpp_exception"); LogHangarSpawn("HANGAR_SPAWN_POST_FACTORY_EXCEPTION", "C++ exception in stock post-factory sequence; rollback queued"); return;
  }

  std::snprintf(detail, sizeof(detail), "generation=%llu carrier_id=%u vehicle_id=%u slot=%u clients_notified=%u",
                metrics_.generation, carrierId, newId, slot, notified);
  LogHangarSpawn("HANGAR_SPAWN_DOCKED_AND_PUBLISHED", detail);
}

bool Host::DescribeHangarSlots(std::uint32_t carrierId,std::uint32_t chassisType,
    std::vector<HangarSlotOption>* options,std::string* reason) {
  if(!options||!reason) return false;
  options->clear();
  if(!InServerContext()) { *reason="wrong_execution_context_or_no_session";return false; }
  const auto* api=Api();
  if(!api||!api->find||!api->geti||!api->va) { *reason="world_api_unavailable";return false; }
  if(!SupportedImage(api,reason)) return false;
  void* carrier=api->find(CC2X_WORLD_VEHICLES,carrierId);
  if(!carrier||static_cast<std::uint32_t>(api->geti(carrier,CC2X_VEHICLE_ID,0))!=carrierId) {
    *reason="requested_carrier_id_not_found";return false;
  }
  if(!Readable(carrier,0x1ac)||!Readable(static_cast<unsigned char*>(carrier)+0x154,0x10)) {
    *reason="carrier_layout_unreadable";return false;
  }
  const uint32_t count=At<uint32_t>(carrier,0x158);
  void* records=At<void*>(carrier,0x154);
  if(count>256||(!records&&count)) { *reason="hangar_slot_array_invalid";return false; }
  const auto* imageVas=FunctionAddresses(api);
  if(!imageVas) { *reason="native_profile_functions_unavailable";return false; }
  auto findSlot=reinterpret_cast<FindSlotFn>(api->va(static_cast<uint32_t>(imageVas[0])));
  if(!findSlot||!Readable(reinterpret_cast<const void*>(findSlot),1)) { *reason="slot_compatibility_function_unavailable";return false; }
  options->reserve(count);
  for(uint32_t i=0;i<count;++i) {
    const auto* record=static_cast<const unsigned char*>(records)+static_cast<size_t>(i)*0x70;
    if(!Readable(record,0x70)) { options->clear();*reason="hangar_slot_record_unreadable";return false; }
  const auto compatibilityType=cc2::api::vehicle::definition_adapter::CompatibilityType(chassisType);
    options->push_back({i,findSlot(carrier,compatibilityType,i)==i,
        At<uint32_t>(records,static_cast<size_t>(i)*0x70+0x64)!=0});
  }
  *reason="ok";
  return true;
}

} // namespace cc2::runtime
