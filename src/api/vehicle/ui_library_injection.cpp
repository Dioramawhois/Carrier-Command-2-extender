#include "ui_library_injection.h"

#include "cc2x.h"
#include "api/vehicle/definition_registry.h"
#include "api/vehicle/vehicle_hud_source_patch.h"
#include "ui_library_shim_source.h"
#include "runtime/mod_loader.h"

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>

namespace cc2::api::vehicle {
namespace {

constexpr std::uint32_t kScriptVectorHook = 0x005B555B;
constexpr std::uint32_t kVectorEmplaceBack = 0x00858740;
constexpr std::uint32_t kStringReserve = 0x00412B30;
constexpr std::uint32_t kStringFree = 0x00C4C722;
constexpr std::uint8_t kScriptVectorHookBytes[] = {0x8D, 0x5F, 0x74, 0x8D, 0x45, 0xC4};
constexpr std::uint8_t kVectorEmplaceBackPrefix[] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF};
constexpr std::uint8_t kStringReservePrefix[] = {
    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x53, 0x8B, 0xD9, 0x57};
constexpr std::uint8_t kStringFreePrefix[] = {
    0x8B, 0xFF, 0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x85, 0xC0};
constexpr char kScriptLabel[] = "scripts/cc2x_vehicle_ui_shim.lua";

struct GameString {
  char* data;
  std::uint32_t size;
  std::uint32_t capacity;
};

struct GameScript {
  GameString name;
  GameString source;
};

struct GameScriptVector {
  GameScript* data;
  std::uint32_t capacity;
  std::uint32_t first;
  std::uint32_t size;
};

static_assert(sizeof(GameString) == 12, "unexpected CC2 string layout");
static_assert(sizeof(GameScript) == 24, "unexpected CC2 script record stride");
static_assert(sizeof(GameScriptVector) == 16, "unexpected CC2 script vector layout");

const cc2x_api* g_api = nullptr;
volatile LONG g_installState = 0;
volatile LONG g_injectedLog = 0;
volatile LONG g_failureLog = 0;
volatile LONG g_hudPatchLog = 0;
std::once_flag g_extensionSnapshot;
std::vector<host::ScreenExtensionDescriptor> g_screenExtensions;

void PrepareScreenExtensions() {
  // This first runs when the engine compiles screens, after load_mod_plugins()
  // completed the enabled-package snapshot. Hook installation happens earlier.
  std::call_once(g_extensionSnapshot, [] {
    if (!g_api || !g_api->active_mod_count || !g_api->active_mod_path) return;
    const int count = g_api->active_mod_count();
    if (count < 0 || count > 256) {
      g_api->log("cc2x_screen_extensions", CC2X_LOG_WARN,
          "event=SCREEN_EXTENSIONS_UNAVAILABLE reason=invalid_activation_snapshot");
      return;
    }
    std::vector<std::wstring> packages;
    for (int i = 0; i < count; ++i) {
      wchar_t path[32768]{};
      if (!g_api->active_mod_path(i, path, 32768)) {
        g_api->log("cc2x_screen_extensions", CC2X_LOG_WARN,
            "event=SCREEN_EXTENSIONS_UNAVAILABLE reason=invalid_activation_path");
        return;
      }
      packages.emplace_back(path);
    }
    auto discovered = host::DiscoverActiveScreenExtensions(packages);
    for (const auto& diagnostic : discovered.diagnostics)
      g_api->log("cc2x_screen_extensions", CC2X_LOG_WARN,
          "event=SCREEN_EXTENSION_REJECTED detail=%ls", diagnostic.c_str());
    g_screenExtensions = std::move(discovered.extensions);
    g_api->log("cc2x_screen_extensions", CC2X_LOG_INFO,
        "event=SCREEN_EXTENSIONS_READY active_packages=%d extensions=%lu diagnostics=%lu",
        count, static_cast<unsigned long>(g_screenExtensions.size()),
        static_cast<unsigned long>(discovered.diagnostics.size()));
  });
}

bool MemoryRange(const void* address, std::size_t length, bool writable) {
  if (!address || length == 0) return false;
  MEMORY_BASIC_INFORMATION info{};
  if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) return false;
  const DWORD protection = info.Protect & 0xFF;
  const bool canWrite = protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
      protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
  const bool canRead = canWrite || protection == PAGE_READONLY ||
      protection == PAGE_EXECUTE_READ;
  if (writable ? !canWrite : !canRead) return false;
  const auto start = reinterpret_cast<std::uintptr_t>(address);
  const auto end = start + length;
  const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
  return end >= start && end <= regionEnd;
}

bool WritableRange(const void* address, std::size_t length) {
  return MemoryRange(address, length, true);
}

bool CodePrefixMatches(const cc2x_api* api, std::uint32_t imageVa,
                       const std::uint8_t* expected, std::size_t length) {
  if (!api || !api->va || !expected) return false;
  const auto* address = reinterpret_cast<const std::uint8_t*>(api->va(imageVa));
  return MemoryRange(address, length, false) &&
      std::memcmp(address, expected, length) == 0;
}

void LogFailure(const char* reason) {
  if (!g_api || !g_api->log || InterlockedCompareExchange(&g_failureLog, 1, 0) != 0) return;
  g_api->log("cc2x_vehicle_ui", CC2X_LOG_WARN,
      "event=UI_SHIM_INJECTION_SKIPPED reason=%s", reason ? reason : "unknown");
}

using EmplaceBackFn = GameScript* (__thiscall*)(GameScriptVector*);
using ReserveStringFn = void (__thiscall*)(GameString*, std::uint32_t);
using FreeStringFn = void (__cdecl*)(void*);

bool SetGameString(GameString* target, const char* text, std::uint32_t length,
                   ReserveStringFn reserve) {
  if (!target || !text || !length || !reserve) return false;
  reserve(target, length + 1);
  if (!target->data || target->capacity < length + 1 ||
      !WritableRange(target->data, length + 1)) return false;
  std::memcpy(target->data, text, length);
  target->data[length] = '\0';
  target->size = length;
  return true;
}

bool EndsWith(std::string_view value, std::string_view suffix) {
  return value.size() >= suffix.size() &&
      value.substr(value.size() - suffix.size()) == suffix;
}

void PatchVehicleHudScript(GameScript* script, ReserveStringFn reserve,
                           FreeStringFn freeString) {
  if (!script || !reserve || !freeString ||
      !MemoryRange(&script->name, sizeof(script->name), false) ||
      !MemoryRange(&script->source, sizeof(script->source), true)) return;
  const auto& name = script->name;
  auto& source = script->source;
  if (!name.data || name.size == 0 || name.size > 4096 ||
      !MemoryRange(name.data, name.size, false) ||
      !EndsWith(std::string_view(name.data, name.size), "vehicle_hud.lua")) return;
  if (!source.data || source.size == 0 || source.size > 2 * 1024 * 1024 ||
      source.capacity < source.size || !MemoryRange(source.data, source.size, false)) {
    if (InterlockedCompareExchange(&g_hudPatchLog, 1, 0) == 0 && g_api && g_api->log)
      g_api->log("cc2x_vehicle_ui", CC2X_LOG_WARN,
          "event=VEHICLE_HUD_COMPATIBILITY_SKIPPED reason=source_string_invalid");
    return;
  }

  std::string patched;
  const auto result = PatchVehicleHudSource(
      std::string_view(source.data, source.size), &patched);
  if (result == VehicleHudPatchResult::kNoMatch ||
      result == VehicleHudPatchResult::kAlreadyPatched) return;
  if (result == VehicleHudPatchResult::kAmbiguous ||
      patched.size() >= std::numeric_limits<std::uint32_t>::max()) {
    if (InterlockedCompareExchange(&g_hudPatchLog, 1, 0) == 0 && g_api && g_api->log)
      g_api->log("cc2x_vehicle_ui", CC2X_LOG_WARN,
          "event=VEHICLE_HUD_COMPATIBILITY_SKIPPED reason=source_anchor_ambiguous_or_oversized");
    return;
  }

  GameString replacement{};
  if (!SetGameString(&replacement, patched.data(),
          static_cast<std::uint32_t>(patched.size()), reserve)) {
    if (replacement.data) freeString(replacement.data);
    if (InterlockedCompareExchange(&g_hudPatchLog, 1, 0) == 0 && g_api && g_api->log)
      g_api->log("cc2x_vehicle_ui", CC2X_LOG_WARN,
          "event=VEHICLE_HUD_COMPATIBILITY_SKIPPED reason=replacement_string_allocation_failed");
    return;
  }

  const auto oldSize = source.size;
  auto* original = source.data;
  source = replacement;
  freeString(original);
  if (InterlockedCompareExchange(&g_hudPatchLog, 1, 0) == 0 && g_api && g_api->log)
    g_api->log("cc2x_vehicle_ui", CC2X_LOG_INFO,
        "event=VEHICLE_HUD_COMPATIBILITY_PATCHED script=vehicle_hud.lua old_bytes=%lu new_bytes=%lu",
        static_cast<unsigned long>(oldSize),
        static_cast<unsigned long>(source.size));
}

void PatchVehicleHudScripts(GameScriptVector* vector, std::uint32_t size,
                            ReserveStringFn reserve, FreeStringFn freeString) {
  if (!vector || size == 0 || vector->capacity == 0 || !vector->data ||
      vector->first >= vector->capacity || size > vector->capacity) return;
  for (std::uint32_t i = 0; i < size; ++i) {
    const auto index = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(vector->first) + i) % vector->capacity);
    auto* script = vector->data + index;
    if (MemoryRange(script, sizeof(*script), true))
      PatchVehicleHudScript(script, reserve, freeString);
  }
}

void RollBackAppendedScript(GameScriptVector* vector, GameScript* script,
                            FreeStringFn freeString) {
  if (!vector || !script) return;
  if (script->source.data && freeString) freeString(script->source.data);
  if (script->name.data && freeString) freeString(script->name.data);
  std::memset(script, 0, sizeof(*script));
  if (vector->size > 0) --vector->size;
}

void OnScreenScriptsReady(cc2x_cpu* cpu, void*) {
  if (!cpu || !g_api) return;
  try {
    PrepareScreenExtensions();
  } catch (...) {
    LogFailure("screen_extension_snapshot_failed");
    return;
  }
  const bool hasVehicles = Registry().Count() != 0;
  if (!hasVehicles && g_screenExtensions.empty()) return;
  if (cpu->ebp < 0x1000u + 0x3Cu) {
    LogFailure("invalid_screen_frame");
    return;
  }

  auto* vector = reinterpret_cast<GameScriptVector*>(
      static_cast<std::uintptr_t>(cpu->ebp - 0x3Cu));
  if (!WritableRange(vector, sizeof(*vector)) || vector->size > 4096 ||
      (vector->size != 0 && (!vector->data || vector->capacity < vector->size ||
          vector->first >= vector->capacity))) {
    LogFailure("screen_script_vector_unreadable_or_invalid");
    return;
  }

  auto emplaceBack = reinterpret_cast<EmplaceBackFn>(g_api->va(kVectorEmplaceBack));
  auto reserveString = reinterpret_cast<ReserveStringFn>(g_api->va(kStringReserve));
  auto freeString = reinterpret_cast<FreeStringFn>(g_api->va(kStringFree));
  if (!emplaceBack || !reserveString || !freeString ||
      !CodePrefixMatches(g_api, kVectorEmplaceBack, kVectorEmplaceBackPrefix,
          sizeof(kVectorEmplaceBackPrefix)) ||
      !CodePrefixMatches(g_api, kStringReserve, kStringReservePrefix,
          sizeof(kStringReservePrefix)) ||
      !CodePrefixMatches(g_api, kStringFree, kStringFreePrefix,
          sizeof(kStringFreePrefix))) {
    LogFailure("game_helper_profile_mismatch");
    return;
  }

  const auto originalSize = vector->size;
  auto append = [&](const char* label, const char* source, std::uint32_t sourceLength) {
    GameScript* script = emplaceBack(vector);
    if (!script) { LogFailure("game_script_vector_append_failed"); return false; }
    if (!WritableRange(script, sizeof(*script))) {
      if (vector->size > 0) --vector->size;
      LogFailure("game_script_record_unwritable"); return false;
    }
    if (!SetGameString(&script->name, label, static_cast<std::uint32_t>(std::strlen(label)), reserveString) ||
        !SetGameString(&script->source, source, sourceLength, reserveString)) {
      RollBackAppendedScript(vector, script, freeString);
      LogFailure("game_script_record_initialization_failed"); return false;
    }
    return true;
  };

  // Match only the original chunks, never an extension appended above another.
  // A screen package needs no custom vehicles or Bridge gameplay manifest.
  for (const auto& extension : g_screenExtensions) {
    bool matches = false;
    std::uint32_t logicalIndex=0,matchCount=0;
    for (std::uint32_t i = 0; i < originalSize; ++i) {
      const auto index = static_cast<std::uint32_t>(
          (static_cast<std::uint64_t>(vector->first) + i) % vector->capacity);
      const auto* candidate = vector->data + index;
      if (!MemoryRange(candidate, sizeof(*candidate), false)) continue;
      const auto& name = candidate->name;
      if (name.data && name.size == extension.screen.size() &&
          MemoryRange(name.data, name.size, false) &&
          host::ScreenExtensionMatches(extension, std::string_view(name.data, name.size))) {
        matches = true;logicalIndex=i;++matchCount;
      }
    }
    if(!matches)continue;
    GameString replacement{};
    if(!extension.profiles.empty()) {
      if(matchCount!=1) {
        g_api->log("cc2x_screen_extensions",CC2X_LOG_WARN,"event=SCREEN_SOURCE_EDIT_SKIPPED reason=ambiguous_target screen=%s",extension.screen.c_str());
        continue;
      }
      auto* target=vector->data+((static_cast<std::uint64_t>(vector->first)+logicalIndex)%vector->capacity);
      if(!WritableRange(target,sizeof(*target))||target->source.size==0||target->source.size>2*1024*1024||
         target->source.capacity<target->source.size||!MemoryRange(target->source.data,target->source.size,false))continue;
      std::string transformed;
      const auto result=host::TransformScreenSource(extension.profiles,
          std::string_view(target->source.data,target->source.size),&transformed);
      if(result!=host::ScreenSourceEditResult::Applied) {
        g_api->log("cc2x_screen_extensions",CC2X_LOG_WARN,"event=SCREEN_SOURCE_EDIT_SKIPPED reason=%s screen=%s",
            result==host::ScreenSourceEditResult::NoProfile?"source_hash_mismatch":"rejected_edits_or_syntax",extension.screen.c_str());
        continue;
      }
      if(!SetGameString(&replacement,transformed.data(),static_cast<std::uint32_t>(transformed.size()),reserveString)) {
        if(replacement.data)freeString(replacement.data);
        continue;
      }
    }
    if (matches && append(extension.sourcePath.c_str(), extension.source.data(),
            static_cast<std::uint32_t>(extension.source.size()))) {
      if(replacement.data) {
        // Appending can grow/rebase the engine ring; reacquire by logical index.
        auto* target=vector->data+((static_cast<std::uint64_t>(vector->first)+logicalIndex)%vector->capacity);
        if(!WritableRange(target,sizeof(*target))) {
          const auto last=(static_cast<std::uint64_t>(vector->first)+vector->size-1)%vector->capacity;
          RollBackAppendedScript(vector,vector->data+last,freeString);
          freeString(replacement.data);continue;
        }
        auto* previous=target->source.data;target->source=replacement;
        if(previous)freeString(previous);
        g_api->log("cc2x_screen_extensions",CC2X_LOG_INFO,
            "event=SCREEN_SOURCE_EDIT_APPLIED screen=%s",extension.screen.c_str());
      }
      g_api->log("cc2x_screen_extensions", CC2X_LOG_INFO,
          "event=SCREEN_EXTENSION_SOURCE_QUEUED screen=%s source=%s bytes=%lu",
          extension.screen.c_str(), extension.sourcePath.c_str(),
          static_cast<unsigned long>(extension.source.size()));
    } else if(replacement.data) {
      freeString(replacement.data);
    }
  }

  if (!hasVehicles) return;
  PatchVehicleHudScripts(vector, originalSize, reserveString, freeString);

  constexpr auto sourceLength = static_cast<std::uint32_t>(sizeof(kUiLibraryShimSource) - 1);
  if (!append(kScriptLabel, kUiLibraryShimSource, sourceLength)) return;

  if (InterlockedCompareExchange(&g_injectedLog, 1, 0) == 0 && g_api->log) {
    g_api->log("cc2x_vehicle_ui", CC2X_LOG_INFO,
        "event=UI_SHIM_SOURCE_QUEUED script=%s source_bytes=%lu vector_entries=%lu",
        kScriptLabel, static_cast<unsigned long>(sourceLength),
        static_cast<unsigned long>(vector->size));
  }
}

}  // namespace

bool InstallStockUiLibraryInjection(const cc2x_api* api) {
  if (!api || !api->game_build || !api->va || !api->log || !api->patch_mid ||
      !api->patch_begin || !api->patch_commit || !api->patch_abort) return false;
  if (std::strcmp(api->game_build, "1.5.18-win32-r2") != 0) {
    api->log("cc2x_vehicle_ui", CC2X_LOG_WARN,
        "event=UI_SHIM_INJECTION_UNAVAILABLE build=%s", api->game_build);
    return false;
  }
  if (InterlockedCompareExchange(&g_installState, 1, 0) != 0) {
    return InterlockedCompareExchange(&g_installState, 0, 0) == 2;
  }

  g_api = api;
  api->patch_begin("native_bridge.vehicle_ui_shim");
  const bool staged = api->patch_mid(kScriptVectorHook,
      static_cast<std::uint32_t>(sizeof(kScriptVectorHookBytes)),
      kScriptVectorHookBytes, OnScreenScriptsReady, nullptr) != 0;
  if (!staged || !api->patch_commit()) {
    api->patch_abort();
    g_api = nullptr;
    InterlockedExchange(&g_installState, 0);
    api->log("cc2x_vehicle_ui", CC2X_LOG_WARN,
        "event=UI_SHIM_INJECTION_INSTALL_FAILED build=%s hook_va=0x%08lX",
        api->game_build, static_cast<unsigned long>(kScriptVectorHook));
    return false;
  }

  InterlockedExchange(&g_installState, 2);
  if (api->lua_feature) api->lua_feature("screen_extensions_v1");
  if (api->lua_feature) api->lua_feature("screen_source_edits_v1");
  api->log("cc2x_vehicle_ui", CC2X_LOG_INFO,
      "event=UI_SHIM_INJECTION_HOOK_INSTALLED build=%s hook_va=0x%08lX",
      api->game_build, static_cast<unsigned long>(kScriptVectorHook));
  return true;
}

}  // namespace cc2::api::vehicle
