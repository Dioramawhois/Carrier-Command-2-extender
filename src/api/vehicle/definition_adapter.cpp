#include "api/vehicle/definition_adapter.h"
#include "api/vehicle/definition_adapter_contract.h"
#include "api/vehicle/definition_registry.h"

#include <Windows.h>
#include "platform/seh_guard.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace cc2::api::vehicle::definition_adapter {
namespace {

constexpr std::uint32_t kLoaderRegisterSite = 0x005A5C0D;
constexpr std::uint32_t kDefinitionLoaderSite = 0x005A2E70;
constexpr std::uint32_t kNameMapResizeSite = 0x00853E10;
constexpr std::uint32_t kXmlParseSuccessSite = 0x005A5FD5;
constexpr std::uint32_t kXmlParseReturnSite = 0x005A5C42;
constexpr std::uint32_t kServerClassSelector = 0x00734B60;
constexpr std::uint32_t kClientClassSelector = 0x00730660;
constexpr std::uint32_t kRegisterNameFunction = 0x005A5CE0;
constexpr std::uint32_t kNameMapResizeReturn = 0x005A2EA1;
constexpr std::uint32_t kExpectedBuildSize = 0x00AF7000;

constexpr std::uint8_t kLoaderRegisterExpected[6] = {0xFF,0x76,0x04,0x8D,0x4E,0x08};
constexpr std::uint8_t kDefinitionLoaderExpected[5] = {0x55,0x8B,0xEC,0x6A,0xFF};
constexpr std::uint8_t kNameMapResizeExpected[6] = {0x55,0x8B,0xEC,0x51,0x53,0x56};
constexpr std::uint8_t kXmlParseSuccessExpected[6] = {0x84,0xC0,0x74,0x14,0x56,0x51};
constexpr std::uint8_t kXmlParseReturnExpected[6] = {0x8B,0x4D,0x0C,0x8B,0x55,0x08};
constexpr std::uint8_t kServerSelectorExpected[5] = {0x55,0x8B,0xEC,0x6A,0xFF};
constexpr std::uint8_t kClientSelectorExpected[5] = {0x55,0x8B,0xEC,0x6A,0xFF};
constexpr std::uint8_t kRegisterNameExpected[5] = {0x55,0x8B,0xEC,0x6A,0xFF};

volatile LONG g_enabled = 0;
volatile LONG g_readyMask = 0;
volatile LONG g_parseSucceededMask = 0;
volatile LONG g_nameMapExpanded = 0;
volatile LONG g_lastEvent = 0;
volatile LONG g_eventDefinitionId = 0;
volatile LONG g_registeredNames = 0;
volatile LONG g_serverRemaps = 0;
volatile LONG g_clientRemaps = 0;
volatile LONG g_failureReason = 0;
volatile LONG g_observedNameCount = 0;
volatile LONG g_observedDefinitionArrayCount = 0;
volatile LONG g_observedResizeReturn = 0;
volatile LONG g_expectedResizeReturn = 0;
volatile LONG g_observedResizeCount = 0;
volatile LONG g_registrationContext = 0;
volatile LONG g_registrationAttempts = 0;
const cc2x_api* g_api = nullptr;

enum FailureReason : LONG {
  kNoFailure = 0,
  kInvalidContext = 1,
  kNameMapNotExpanded = 2,
  kCountsMismatch = 3,
  kRegisterFunctionGuard = 4,
  kRegisterNameException = 5,
  kXmlDocumentParseFailed = 6,
};

enum Event : LONG {
  kNoEvent = 0,
  kNameMapExpanded = 1,
  kRegistrationFailed = 2,
  kDefinitionNameRegistered = 3,
  kDefinitionXmlParsed = 4,
  kDefinitionReady = 5,
  kServerTypeRemapped = 6,
  kClientTypeRemapped = 7,
  kProfileGuardFailed = 8,
  kDefinitionXmlParseFailed = 9,
};

bool Readable(const void* address, std::size_t length) {
  if (!address || !length) return false;
  MEMORY_BASIC_INFORMATION info{};
  if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) return false;
  const DWORD protection = info.Protect & 0xFF;
  return protection == PAGE_READONLY || protection == PAGE_READWRITE ||
      protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
      protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

bool HasBytes(const cc2x_api* api, std::uint32_t imageVa,
              const std::uint8_t* expected, std::size_t length) {
  if (!api || !api->va || !expected) return false;
  const auto* address = reinterpret_cast<const std::uint8_t*>(api->va(imageVa));
  return Readable(address, length) && std::memcmp(address, expected, length) == 0;
}

struct EngineStringView {
  const char* data;
  std::uint32_t length;
  std::uint32_t capacity;
};
using RegisterName = void (__thiscall*)(void*, std::uint32_t, const EngineStringView*);

void SetEvent(Event event, std::uint32_t definitionId = 0) {
  InterlockedExchange(&g_eventDefinitionId, static_cast<LONG>(definitionId));
  InterlockedExchange(&g_lastEvent, event);
}

std::uint32_t ReadyMask() {
  return static_cast<std::uint32_t>(InterlockedCompareExchange(&g_readyMask, 0, 0));
}

bool ReadyFor(std::uint32_t id) {
  return api::vehicle::Registry().ReadyFor(id);
}

std::uint32_t RequiredNameCount() {
  const auto& registry=api::vehicle::Registry();
  return registry.Count()?std::max(kStockDefinitionCount,registry.Limit()):kStockDefinitionCount;
}

void __cdecl OnRegisterDefinitionName(cc2x_cpu* cpu, void*) {
  if (!cpu) return;
  const auto required=RequiredNameCount();
  if (required<=kStockDefinitionCount) return;
  InterlockedIncrement(&g_registrationAttempts);
  InterlockedExchange(&g_readyMask, 0);
  InterlockedExchange(&g_parseSucceededMask, 0);
  InterlockedExchange(&g_registeredNames, 0);
  SetEvent(kNoEvent);

  const auto context = static_cast<std::uintptr_t>(cpu->esi);
  InterlockedExchange(&g_registrationContext, static_cast<LONG>(context));
  if (!context || !Readable(reinterpret_cast<const void*>(context), 0x10) ||
      InterlockedCompareExchange(&g_nameMapExpanded, 0, 0) != 1) {
    InterlockedExchange(&g_failureReason,
        !context || !Readable(reinterpret_cast<const void*>(context), 0x10)
            ? kInvalidContext : kNameMapNotExpanded);
    InterlockedExchange(&g_lastEvent, kRegistrationFailed);
    return;
  }

  auto* fields = reinterpret_cast<std::uint32_t*>(context);
  InterlockedExchange(&g_observedNameCount, static_cast<LONG>(fields[1]));
  InterlockedExchange(&g_observedDefinitionArrayCount, static_cast<LONG>(fields[3]));
  // FUN_00853E10 owns the name-map count at +4; after the guarded resize it is
  // 103 (IDs 0..102). The definition-array count at +0xC is zero here. Leave
  // it untouched: the immediately following stock resize uses the name-map
  // count to allocate/construct all records and sets the array count itself.
  if (fields[1]!=required || fields[3]!=kInitialDefinitionArrayCount) {
    InterlockedExchange(&g_failureReason, kCountsMismatch);
    InterlockedExchange(&g_lastEvent, kRegistrationFailed);
    return;
  }

  auto registerName = reinterpret_cast<RegisterName>(g_api->va(kRegisterNameFunction));
  if (!registerName || !Readable(reinterpret_cast<const void*>(registerName), 5) ||
      std::memcmp(reinterpret_cast<const void*>(registerName), kRegisterNameExpected, 5) != 0) {
    InterlockedExchange(&g_failureReason, kRegisterFunctionGuard);
    InterlockedExchange(&g_lastEvent, kProfileGuardFailed);
    return;
  }

  struct RegistrationArgs { RegisterName registerName; void* context; } args{
      registerName, reinterpret_cast<void*>(context)};
  const auto registerNames = [](void* user) {
    const auto* args = static_cast<RegistrationArgs*>(user);
    auto& registry=api::vehicle::Registry();
    for (std::uint32_t id=registry.FirstId();id<registry.Limit();++id) {
      const auto* entry=registry.Find(id);
      if (!entry) continue;
      const auto length=static_cast<std::uint32_t>(entry->resourceName.size());
      const EngineStringView nameView{entry->resourceName.c_str(),length,length};
      args->registerName(args->context,id,&nameView);
      registry.Mark(id,api::vehicle::NameRegistered);
    }
  };
  if (!cc2x_seh_guard(registerNames, &args)) {
    InterlockedExchange(&g_failureReason, kRegisterNameException);
    InterlockedExchange(&g_lastEvent, kRegistrationFailed);
    return;
  }

  // The immediately following stock resize receives nameMapCount (103), allocates
  // and constructs the definition records, and updates the array count itself.
  // Do not pre-seed fields[3]: the engine would treat the empty array as already
  // containing 103 live records and run destructors through a null data pointer.
  InterlockedExchange(&g_registeredNames, 0);
  SetEvent(kDefinitionNameRegistered,
      api::vehicle::Registry().Count()?api::vehicle::Registry().FirstId():0);
}

void __cdecl OnXmlParseSuccess(cc2x_cpu* cpu, void*) {
  if (!cpu) return;
  if (cpu->edi%kDefinitionRecordStride==0) {
    const auto dynamicId=static_cast<std::uint32_t>(cpu->edi/kDefinitionRecordStride);
    auto& registry=api::vehicle::Registry();
    if (registry.Find(dynamicId)) {
      if ((cpu->eax&0xFFu)==0) {
        InterlockedExchange(&g_failureReason,kXmlDocumentParseFailed);
        SetEvent(kDefinitionXmlParseFailed,dynamicId);
        return;
      }
      const auto context=static_cast<std::uintptr_t>(InterlockedCompareExchange(&g_registrationContext,0,0));
      if (!Readable(reinterpret_cast<const void*>(context),0x10)) {
        InterlockedExchange(&g_failureReason,kInvalidContext);
        SetEvent(kRegistrationFailed,dynamicId);
        return;
      }
      const auto* fields=reinterpret_cast<const std::uint32_t*>(context);
      if (fields[3]<registry.Limit() || !fields[2] ||
          !registry.Mark(dynamicId,api::vehicle::ArrayInitialized) ||
          !registry.Mark(dynamicId,api::vehicle::XmlParsed)) {
        InterlockedExchange(&g_failureReason,kCountsMismatch);
        SetEvent(kRegistrationFailed,dynamicId);
        return;
      }
      SetEvent(kDefinitionXmlParsed,dynamicId);
      return;
    }
  }
}

void __cdecl OnXmlParseReturn(cc2x_cpu* cpu, void*) {
  if (!cpu) return;
  auto& registry=api::vehicle::Registry();
  if (registry.Find(cpu->edi)) {
    const auto id=static_cast<std::uint32_t>(cpu->edi);
    if (registry.Mark(id,api::vehicle::Ready)) SetEvent(kDefinitionReady,id);
    else SetEvent(kRegistrationFailed,id);
    return;
  }
}

void __cdecl OnServerSelector(cc2x_cpu* cpu, void*) {
  if (!cpu || !ReadyFor(cpu->ecx)) return;
  const auto customId = static_cast<std::uint32_t>(cpu->ecx);
  const auto dynamic=api::vehicle::Registry().CompatibilityType(customId);
  cpu->ecx = dynamic;
  InterlockedIncrement(&g_serverRemaps);
  SetEvent(kServerTypeRemapped, customId);
}

void __cdecl OnClientSelector(cc2x_cpu* cpu, void*) {
  if (!cpu) return;
  auto* stack = CC2X_STACK(cpu);
  if (!stack || !ReadyFor(stack[1])) return;
  const auto customId = static_cast<std::uint32_t>(stack[1]);
  const auto dynamic=api::vehicle::Registry().CompatibilityType(customId);
  stack[1] = dynamic;
  InterlockedIncrement(&g_clientRemaps);
  SetEvent(kClientTypeRemapped, customId);
}

bool IsSteamBuild(const cc2x_api* api) {
  return api && api->game_build && std::strcmp(api->game_build, "1.5.18-win32-r2") == 0;
}

void __cdecl OnDefinitionLoaderEntry(cc2x_cpu*, void*) {
  BeginDefinitionLoad();
}

void __cdecl OnNameMapResizeEntry(cc2x_cpu* cpu, void*) {
  OnNameMapResize(cpu);
}

}  // namespace

bool Install(const cc2x_api* api) {
  if (InterlockedCompareExchange(&g_enabled, 0, 0)) return true;
  if (!api || api->api_version < 5 || !api->config_dir || !api->log || !api->va ||
      !api->patch_begin || !api->patch_mid || !api->patch_commit || !api->patch_abort) return false;

  if (!IsSteamBuild(api) || !api->module_base ||
      !HasBytes(api, kLoaderRegisterSite, kLoaderRegisterExpected, sizeof(kLoaderRegisterExpected)) ||
      !HasBytes(api, kDefinitionLoaderSite, kDefinitionLoaderExpected, sizeof(kDefinitionLoaderExpected)) ||
      !HasBytes(api, kNameMapResizeSite, kNameMapResizeExpected, sizeof(kNameMapResizeExpected)) ||
      !HasBytes(api, kXmlParseSuccessSite, kXmlParseSuccessExpected, sizeof(kXmlParseSuccessExpected)) ||
      !HasBytes(api, kXmlParseReturnSite, kXmlParseReturnExpected, sizeof(kXmlParseReturnExpected)) ||
      !HasBytes(api, kServerClassSelector, kServerSelectorExpected, sizeof(kServerSelectorExpected)) ||
      !HasBytes(api, kClientClassSelector, kClientSelectorExpected, sizeof(kClientSelectorExpected)) ||
      !HasBytes(api, kRegisterNameFunction, kRegisterNameExpected, sizeof(kRegisterNameExpected))) {
    api->log("cc2x_vehicle_definitions", CC2X_LOG_ERROR,
        "event=PROFILE_GUARD_FAILED build=%s; custom type hooks not installed",
        api->game_build ? api->game_build : "unknown");
    return false;
  }

  g_api = api;
  api->patch_begin("cc2x_vehicle_definitions");
  const bool hooksInstalled =
      api->patch_mid(kDefinitionLoaderSite, sizeof(kDefinitionLoaderExpected),
          kDefinitionLoaderExpected, OnDefinitionLoaderEntry, nullptr) != 0 &&
      api->patch_mid(kNameMapResizeSite, sizeof(kNameMapResizeExpected),
          kNameMapResizeExpected, OnNameMapResizeEntry, nullptr) != 0 &&
      api->patch_mid(kLoaderRegisterSite, sizeof(kLoaderRegisterExpected),
          kLoaderRegisterExpected, OnRegisterDefinitionName, nullptr) != 0 &&
      api->patch_mid(kXmlParseSuccessSite, sizeof(kXmlParseSuccessExpected),
          kXmlParseSuccessExpected, OnXmlParseSuccess, nullptr) != 0 &&
      api->patch_mid(kXmlParseReturnSite, sizeof(kXmlParseReturnExpected),
          kXmlParseReturnExpected, OnXmlParseReturn, nullptr) != 0 &&
      api->patch_mid(kServerClassSelector, sizeof(kServerSelectorExpected),
          kServerSelectorExpected, OnServerSelector, nullptr) != 0 &&
      api->patch_mid(kClientClassSelector, sizeof(kClientSelectorExpected),
          kClientSelectorExpected, OnClientSelector, nullptr) != 0;
  if (!hooksInstalled || !api->patch_commit()) {
    api->patch_abort();
    g_api = nullptr;
    api->log("cc2x_vehicle_definitions", CC2X_LOG_ERROR,
        "event=HOOK_INSTALL_FAILED build=%s; prototype disabled",
        api->game_build ? api->game_build : "unknown");
    return false;
  }
  InterlockedExchange(&g_enabled, 1);
  api->log("cc2x_vehicle_definitions", CC2X_LOG_INFO,
      "event=DEFINITION_ADAPTER_HOOKS_INSTALLED build=%s stock_definition_count=102 "
      "definition_stride=0x220 mode=generic_registry",
      api->game_build);
  return true;
}

bool Enabled() { return InterlockedCompareExchange(&g_enabled, 0, 0) != 0; }
bool DefinitionReady(std::uint32_t id) {
  return Enabled() && ReadyFor(id);
}

std::uint32_t CompatibilityType(std::uint32_t type) {
  const auto dynamic=api::vehicle::Registry().CompatibilityType(type);
  return dynamic;
}

void BeginDefinitionLoad() {
  if (!Enabled()) return;
  InterlockedExchange(&g_readyMask, 0);
  InterlockedExchange(&g_parseSucceededMask, 0);
  InterlockedExchange(&g_nameMapExpanded, 0);
  InterlockedExchange(&g_registeredNames, 0);
  InterlockedExchange(&g_failureReason, kNoFailure);
  InterlockedExchange(&g_observedNameCount, 0);
  InterlockedExchange(&g_observedDefinitionArrayCount, 0);
  InterlockedExchange(&g_observedResizeReturn, 0);
  InterlockedExchange(&g_expectedResizeReturn, 0);
  InterlockedExchange(&g_observedResizeCount, 0);
  InterlockedExchange(&g_registrationContext, 0);
  InterlockedExchange(&g_registrationAttempts, 0);
  api::vehicle::Registry().BeginLoad();
  SetEvent(kNoEvent);
}

void OnNameMapResize(cc2x_cpu* cpu) {
  if (!Enabled() || !cpu) return;
  auto* stack = CC2X_STACK(cpu);
  const auto expectedReturn = g_api && g_api->va
      ? static_cast<std::uintptr_t>(g_api->va(kNameMapResizeReturn)) : 0;
  if (stack) {
    InterlockedExchange(&g_observedResizeReturn, static_cast<LONG>(stack[0]));
    InterlockedExchange(&g_expectedResizeReturn, static_cast<LONG>(expectedReturn));
    InterlockedExchange(&g_observedResizeCount, static_cast<LONG>(stack[1]));
  }
  if (!stack || expectedReturn > UINT32_MAX ||
      !ShouldExpandNameMap(true, stack[0], static_cast<std::uint32_t>(expectedReturn), stack[1])) return;
  const auto required=RequiredNameCount();
  if (required<=kStockDefinitionCount) return;
  stack[1] = required;
  InterlockedExchange(&g_nameMapExpanded, 1);
  SetEvent(kNameMapExpanded);
}

void Flush(const cc2x_api* api) {
  if (!Enabled() || !api || !api->log) return;
  const LONG event = InterlockedExchange(&g_lastEvent, kNoEvent);
  if (event == kNoEvent) return;
  const LONG eventId = InterlockedExchange(&g_eventDefinitionId, 0);
  const auto ready = ReadyMask();
  const LONG registered = InterlockedCompareExchange(&g_registeredNames, 0, 0);
  const LONG server = InterlockedCompareExchange(&g_serverRemaps, 0, 0);
  const LONG client = InterlockedCompareExchange(&g_clientRemaps, 0, 0);
  const LONG failure = InterlockedCompareExchange(&g_failureReason, 0, 0);
  const LONG mapCount = InterlockedCompareExchange(&g_observedNameCount, 0, 0);
  const LONG definitionArrayCount = InterlockedCompareExchange(&g_observedDefinitionArrayCount, 0, 0);
  const LONG resizeReturn = InterlockedCompareExchange(&g_observedResizeReturn, 0, 0);
  const LONG expectedReturn = InterlockedCompareExchange(&g_expectedResizeReturn, 0, 0);
  const LONG resizeCount = InterlockedCompareExchange(&g_observedResizeCount, 0, 0);
  const LONG registrationContext = InterlockedCompareExchange(&g_registrationContext, 0, 0);
  const LONG registrationAttempts = InterlockedCompareExchange(&g_registrationAttempts, 0, 0);
  const auto& registry=api::vehicle::Registry();
  const auto* dynamicEvent=registry.Find(static_cast<std::uint32_t>(eventId));
  const auto dynamicState=dynamicEvent?dynamicEvent->state.load(std::memory_order_acquire):0u;
  const char* name = "UNKNOWN";
  switch (event) {
    case kNameMapExpanded: name = "NAME_MAP_EXPANDED"; break;
    case kRegistrationFailed: name = "REGISTRATION_OR_XML_LOAD_FAILED"; break;
    case kDefinitionNameRegistered: name = "DEFINITION_NAME_REGISTERED"; break;
    case kDefinitionXmlParsed: name = "DEFINITION_XML_PARSE_SUCCEEDED"; break;
    case kDefinitionReady: name = "DEFINITION_READY"; break;
    case kServerTypeRemapped: name = "SERVER_NATIVE_CLASS_ALIAS"; break;
    case kClientTypeRemapped: name = "CLIENT_NATIVE_CLASS_ALIAS"; break;
    case kProfileGuardFailed: name = "INTERNAL_PROFILE_GUARD_FAILED"; break;
    case kDefinitionXmlParseFailed: name = "DEFINITION_XML_PARSE_FAILED"; break;
  }
  api->log("cc2x_vehicle_definitions", event == kRegistrationFailed ||
      event == kProfileGuardFailed || event == kDefinitionXmlParseFailed
      ? CC2X_LOG_WARN : CC2X_LOG_INFO,
      "event=%s custom_id=%ld definition_ready_mask=0x%lx registered_mask=0x%lx dynamic_count=%zu dynamic_state=0x%x server_remaps=%ld client_remaps=%ld failure_reason=%ld name_map_count=%ld definition_array_count_before_resize=%ld resize_return=0x%08lx expected_resize_return=0x%08lx resize_requested=%ld name_map_expanded=%ld registration_context=0x%08lx registration_attempts=%ld",
      name, eventId, static_cast<unsigned long>(ready), static_cast<unsigned long>(registered),
      registry.Count(),dynamicState,server, client, failure, mapCount, definitionArrayCount,
      static_cast<unsigned long>(static_cast<std::uint32_t>(resizeReturn)),
      static_cast<unsigned long>(static_cast<std::uint32_t>(expectedReturn)), resizeCount,
      InterlockedCompareExchange(&g_nameMapExpanded, 0, 0),
      static_cast<unsigned long>(static_cast<std::uint32_t>(registrationContext)),
      registrationAttempts);
}

}  // namespace cc2::api::vehicle::definition_adapter
