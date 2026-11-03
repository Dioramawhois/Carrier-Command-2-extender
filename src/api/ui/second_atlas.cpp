#include "api/ui/second_atlas.h"
#include "api/ui/second_atlas_contract.h"
#include "api/ui/resource_registry.h"

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cc2::api::ui::second_atlas {
namespace {

constexpr std::uint32_t kResourceLoadCall = 0x005AD5BE;
// Capture the name at the first call site inside FUN_005B2170, after its
// stack frame is established but before the function reuses [EBP+8].
constexpr std::uint32_t kRegionLookupCapture = 0x005B219A;
constexpr std::uint32_t kRegionLookupReturn = 0x005B2272;
constexpr std::uint32_t kUpdateImageEntry = 0x005B26D0;
constexpr std::uint32_t kUpdateImagePowerEntry = 0x005B2750;
constexpr std::uint32_t kUpdateImageRotEntry = 0x005B27D0;
constexpr std::uint32_t kGetImageSizeEntry = 0x005B2850;
constexpr std::uint32_t kImageReturn = 0x005B2746;
constexpr std::uint32_t kImagePowerReturn = 0x005B27C9;
constexpr std::uint32_t kImageRotReturn = 0x005B2840;
constexpr std::uint32_t kSizeReturn = 0x005B28C2;
constexpr std::uint32_t kResourceRequest = 0x0043C1C0;
// FUN_00439800 is the engine's atlas/resource population path. These two
// passive probes distinguish a failed/missing asset parse from an atlas whose
// region vector was populated but later read incorrectly.
// Wrap the exact five-byte parser CALL to observe its real return value and
// parsed-region count without relocating the following conditional branch.
constexpr std::uint32_t kAtlasParserCall = 0x00439878;
// This is the final SEH-chain restore before callee-saved registers/EBP are
// popped, so both the atlas `this` in EDI and the request frame are intact.
constexpr std::uint32_t kAtlasPopulateReturn = 0x00439A37;
constexpr std::uint32_t kSetUiPosition = 0x005AF870;
constexpr std::uint32_t kQueueUiImage = 0x005AFE30;
constexpr std::uint32_t kQueueUiRotatedImage = 0x005B0050;
// Typed resource-task calls to FUN_0041D7F0. They all pass the same
// file-request object in ECX and the destination buffer in EDX.
constexpr std::uint32_t kResourceReadCalls[] = {
    0x008B27F6, 0x008B28A8, 0x008B2A63, 0x008B2B63, 0x008B2C43};
constexpr std::uint8_t kResourceReadCallExpected[][5] = {
    {0xE8, 0xF5, 0xAF, 0xB6, 0xFF},
    {0xE8, 0x43, 0xAF, 0xB6, 0xFF},
    {0xE8, 0x88, 0xAD, 0xB6, 0xFF},
    {0xE8, 0x88, 0xAC, 0xB6, 0xFF},
    {0xE8, 0xA8, 0xAB, 0xB6, 0xFF}};

constexpr std::uint8_t kResourceLoadExpected[7] = {0xC6, 0x45, 0xFC, 0x00, 0x8B, 0x45, 0xDC};
constexpr std::uint8_t kAtlasParserCallExpected[5] = {0xE8, 0xA3, 0x9D, 0x43, 0x00};
constexpr std::uint8_t kAtlasPopulateReturnExpected[7] = {0x64, 0x89, 0x0D, 0x00, 0x00, 0x00, 0x00};
constexpr std::uint8_t kRegionLookupCaptureExpected[6] = {0xFF, 0x75, 0x08, 0x8D, 0x4D, 0xE0};
constexpr std::uint8_t kRegionLookupReturnExpected[5] = {0x8B, 0xC6, 0x8B, 0x4D, 0xF4};
constexpr std::uint8_t kImageEntryExpected[6] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8};
constexpr std::uint8_t kImagePowerEntryExpected[6] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8};
constexpr std::uint8_t kImageRotEntryExpected[6] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8};
constexpr std::uint8_t kImageSizeEntryExpected[6] = {0x55, 0x8B, 0xEC, 0x8B, 0x41, 0x64};

struct ResourceRequest {
  std::uint32_t type;
  const char* path;
  std::uint32_t length;
  std::uint32_t capacity;
};

constexpr char kAtlasPath[] = "textures/cc2x_runtime/index.atlas";
const ResourceRequest kRequest{3u, kAtlasPath, sizeof(kAtlasPath) - 1u, sizeof(kAtlasPath) - 1u};

volatile LONG g_installed = 0;
volatile LONG g_dynamicLoadAttempted = 0;
volatile LONG g_dynamicAtlasHandle = 0;
void* g_originalAtlasParser = nullptr;
volatile LONG g_loggedUnavailable[4] = {};
volatile LONG g_loggedLookup[4] = {};
volatile LONG g_loggedRender[4] = {};
thread_local LookupContextStack g_lookupContexts;
const cc2x_api* g_api = nullptr;
void* g_originalResourceRead = nullptr;

constexpr std::size_t kPhysicalPathCapacity = 1024;
thread_local char g_physicalAssetPath[kPhysicalPathCapacity]{};
std::mutex g_loggedGeneratedPathMutex;
std::set<std::string> g_loggedGeneratedPaths;
bool FirstEvent(volatile LONG* flags, Region region) {
  const auto id = static_cast<std::uint32_t>(region);
  if (id >= 1 && id <= 4)
    return InterlockedCompareExchange(&flags[id - 1], 1, 0) == 0;
  static std::mutex mutex;
  static std::set<std::pair<const volatile LONG*, std::uint32_t>> seen;
  std::lock_guard<std::mutex> lock(mutex);
  return seen.emplace(flags, id).second;
}

const char* RegionName(Region region) {
  const auto token = EncodedIndex(region);
  const auto* resource = Registry().FindByRegionToken(token);
  return resource ? resource->descriptor.regionName.c_str() : "";
}

bool PhysicalPath(const std::wstring& wide, char* output, std::size_t capacity) {
  if (!output || !capacity || wide.empty() || wide.size() >= INT_MAX) return false;
  BOOL usedDefault = FALSE;
  const int written = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.c_str(), -1,
      output, static_cast<int>(capacity), nullptr, &usedDefault);
  return written > 0 && !usedDefault && static_cast<std::size_t>(written) <= capacity;
}

bool Readable(const void* address, std::size_t length) {
  if (!address || !length) return false;
  MEMORY_BASIC_INFORMATION info{};
  if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) return false;
  const DWORD protection = info.Protect & 0xFF;
  if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
      protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READ &&
      protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return false;
  const auto start = reinterpret_cast<std::uintptr_t>(address);
  const auto end = start + length;
  const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
  return end >= start && end <= regionEnd;
}

bool Writable(void* address, std::size_t length) {
  if (!address || !length) return false;
  MEMORY_BASIC_INFORMATION info{};
  if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) return false;
  const DWORD protection = info.Protect & 0xFF;
  if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
      protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return false;
  const auto start = reinterpret_cast<std::uintptr_t>(address);
  const auto end = start + length;
  const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
  return end >= start && end <= regionEnd;
}

bool IsCustomName(const char* value, Region* region) {
  if (!value || !Readable(value, 1)) return false;
  char bounded[16]{};
  std::size_t length = 0;
  while (length < sizeof(bounded) - 1 && Readable(value + length, 1) && value[length]) {
    bounded[length] = value[length];
    ++length;
  }
  if (length == sizeof(bounded) - 1 || !Readable(value + length, 1) || value[length]) return false;
  if (const auto* resource = Registry().FindByRegionName(bounded)) {
    if (region) *region = static_cast<Region>(resource->descriptor.regionToken & ~kCustomRegionTag);
    return true;
  }
  return false;
}

bool IsCustomAtlasRequest(std::uintptr_t address) {
  if (!Readable(reinterpret_cast<const void*>(address), sizeof(ResourceRequest))) return false;
  const auto* request = reinterpret_cast<const ResourceRequest*>(address);
  if (request->type != 3u || request->length > 512u || !request->path ||
      !Readable(request->path, request->length + 1u)) return false;
  if (request->path[request->length] != '\0') return false;
  return (request->length == sizeof(kAtlasPath) - 1u &&
          std::memcmp(request->path, kAtlasPath, request->length) == 0);
}

int __fastcall OnAtlasParser(void* request, void* parseOutput) {
  using ParserFn = int (__fastcall*)(void*, void*);
  const auto original = reinterpret_cast<ParserFn>(g_originalAtlasParser);
  const int status = original ? original(request, parseOutput) : 0;
  if (g_api && g_api->log && IsCustomAtlasRequest(
          reinterpret_cast<std::uintptr_t>(request))) {
    const auto* resourceRequest = static_cast<const ResourceRequest*>(request);
    std::uint32_t regionCount = 0;
    const auto countAddress = reinterpret_cast<std::uintptr_t>(parseOutput) + 0x1Cu;
    if (Readable(reinterpret_cast<const void*>(countAddress), sizeof(regionCount)))
      regionCount = *reinterpret_cast<const std::uint32_t*>(countAddress);
    g_api->log("cc2x_ui_second_atlas", status == 1 ? CC2X_LOG_INFO : CC2X_LOG_WARN,
        "event=SECOND_ATLAS_POPULATE_PARSE_RESULT path=%s request=%p output=%p status=%d pending_regions=%u",
        resourceRequest->path, request, parseOutput, status, regionCount);
  }
  return status;
}

int __fastcall OnResourceRead(void* rawRequest, void* outputBuffer) {
  // All five call sites load request into ECX and the output buffer into EDX.
  // Preserve that fastcall ABI when forwarding; using __thiscall shifts EDX
  // into the stack and corrupts the caller's frame on return.
  using ResourceReadFn = int (__fastcall*)(void*, void*);
  const auto original = reinterpret_cast<ResourceReadFn>(g_originalResourceRead);
  if (!original) return 2;
  if (!Readable(rawRequest, sizeof(ResourceRequest))) return original(rawRequest, outputBuffer);
  auto* request = static_cast<ResourceRequest*>(rawRequest);
  if (request->type != 3u || request->length > 512u ||
      !Readable(request->path, request->length + 1u) || request->path[request->length] != '\0')
    return original(rawRequest, outputBuffer);

  const std::string virtualPath(request->path, request->length);
  const auto* registered = Registry().FindByVirtualPath(virtualPath);
  bool resolved = false;
  if (registered && !registered->descriptor.generatedPath.empty()) {
    resolved = PhysicalPath(registered->descriptor.generatedPath, g_physicalAssetPath,
        sizeof(g_physicalAssetPath));
    if (!resolved && g_api && g_api->log) {
      g_api->log("cc2x_ui_second_atlas", CC2X_LOG_ERROR,
          "event=RESOURCE_REGISTRY_PATH_UNREPRESENTABLE path=%s owner=%ls",
          virtualPath.c_str(), registered->descriptor.owner.c_str());
    }
  }
  if (!resolved) return original(rawRequest, outputBuffer);

  const auto savedType = request->type;
  const auto* savedPath = request->path;
  const auto savedLength = request->length;
  const auto savedCapacity = request->capacity;
  request->type = 1u;  // FUN_0041D7F0 mode 1 performs _fopen(path, "rb").
  request->path = g_physicalAssetPath;
  request->length = static_cast<std::uint32_t>(std::strlen(g_physicalAssetPath));
  request->capacity = request->length;
  const int status = original(rawRequest, outputBuffer);
  request->type = savedType;
  request->path = savedPath;
  request->length = savedLength;
  request->capacity = savedCapacity;

  if (registered) {
    Registry().Mark(registered->descriptor.owner, registered->descriptor.logicalId, EngineRequested);
    if (status == 1)
      Registry().Mark(registered->descriptor.owner, registered->descriptor.logicalId, EngineReady);
    std::lock_guard<std::mutex> lock(g_loggedGeneratedPathMutex);
    if (g_loggedGeneratedPaths.insert(virtualPath).second && g_api && g_api->log) {
      g_api->log("cc2x_ui_second_atlas", status == 1 ? CC2X_LOG_INFO : CC2X_LOG_ERROR,
          "event=RESOURCE_REGISTRY_DIRECT_READ path=%s owner=%ls source=%ls generated=%s status=%d",
          virtualPath.c_str(), registered->descriptor.owner.c_str(),
          registered->descriptor.sourcePath.c_str(), g_physicalAssetPath, status);
    }
    return status;
  }

  return status;
}

void __cdecl OnAtlasPopulateReturn(cc2x_cpu* cpu, void*) {
  if (!cpu || !g_api || !g_api->log) return;
  const auto ebp = static_cast<std::uintptr_t>(cpu->ebp);
  if (!Readable(reinterpret_cast<const void*>(ebp + 0x10), sizeof(std::uint32_t))) return;
  const auto request = *reinterpret_cast<const std::uint32_t*>(ebp + 0x10);
  if (!IsCustomAtlasRequest(request)) return;
  const auto* resourceRequest = reinterpret_cast<const ResourceRequest*>(request);
  const auto object = static_cast<std::uintptr_t>(cpu->edi);
  if (!Readable(reinterpret_cast<const void*>(object), 16)) return;
  const auto* fields = reinterpret_cast<const std::uint32_t*>(object);
  g_api->log("cc2x_ui_second_atlas", fields[3] ? CC2X_LOG_INFO : CC2X_LOG_WARN,
      "event=SECOND_ATLAS_POPULATE_RETURN path=%s object=%p items=%p capacity=%u head=%u count=%u",
      resourceRequest->path, reinterpret_cast<void*>(object),
      reinterpret_cast<void*>(static_cast<std::uintptr_t>(fields[0])),
      fields[1], fields[2], fields[3]);
}

bool EnsureAtlasLoaded(cc2x_cpu* cpu) {
  if (!cpu || !g_api || !g_api->va) return false;
  const bool needDynamic = Registry().FindByVirtualPath(kAtlasPath) != nullptr;
  bool ready = true;

  auto load = [&](volatile LONG* handleStorage, volatile LONG* attemptedStorage,
                  const ResourceRequest* requestData, const char* path) {
    if (InterlockedCompareExchange(handleStorage, 0, 0) != 0) return true;
    if (InterlockedCompareExchange(attemptedStorage, 1, 0) != 0) return false;

    const auto ebp = static_cast<std::uintptr_t>(cpu->ebp);
    if (!Readable(reinterpret_cast<const void*>(ebp + 8), 8)) {
      InterlockedExchange(attemptedStorage, 0);
      if (g_api->log) g_api->log("cc2x_ui_second_atlas", CC2X_LOG_ERROR,
          "event=SECOND_ATLAS_LOAD_FAILED path=%s reason=screen_resource_arguments_unreadable", path);
      return false;
    }
    auto* frame = reinterpret_cast<const std::uint32_t*>(ebp);
    void* owner = reinterpret_cast<void*>(frame[2]);
    void* manager = reinterpret_cast<void*>(frame[3]);
    if (!owner || !manager || !Readable(manager, 0x34)) {
      InterlockedExchange(attemptedStorage, 0);
      if (g_api->log) g_api->log("cc2x_ui_second_atlas", CC2X_LOG_ERROR,
          "event=SECOND_ATLAS_LOAD_FAILED path=%s reason=screen_resource_context_invalid owner=%p manager=%p",
          path, owner, manager);
      return false;
    }

    using RequestFn = void (__thiscall*)(void*, void*, const ResourceRequest*, void*);
    auto request = reinterpret_cast<RequestFn>(g_api->va(kResourceRequest));
    request(manager, owner, requestData, const_cast<LONG*>(handleStorage));
    const auto handle = InterlockedCompareExchange(handleStorage, 0, 0);
    std::uintptr_t resource = 0;
    if (handle && Readable(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(handle)),
                          sizeof(resource))) {
      resource = *reinterpret_cast<const std::uintptr_t*>(static_cast<std::uintptr_t>(handle));
    }
    if (!handle) InterlockedExchange(attemptedStorage, 0);
    if (g_api->log) g_api->log("cc2x_ui_second_atlas", handle ? CC2X_LOG_INFO : CC2X_LOG_WARN,
        "event=SECOND_ATLAS_LOAD_REQUESTED path=%s manager=%p owner=%p storage=%p handle=%p resource=%p state=%s",
        path, manager, owner, const_cast<LONG*>(handleStorage),
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(handle)),
        reinterpret_cast<void*>(resource), handle ? "pending_or_ready" : "no_handle");
    return handle != 0;
  };

  if (needDynamic) ready = load(&g_dynamicAtlasHandle, &g_dynamicLoadAttempted, &kRequest,
      kAtlasPath) && ready;
  return ready;
}

struct AtlasView {
  std::uint32_t* items;
  std::uint32_t capacity;
  std::uint32_t head;
  std::uint32_t count;
};

struct AtlasDiagnostics {
  std::uintptr_t handle = 0;
  std::uintptr_t resource = 0;
  std::uint32_t items = 0;
  std::uint32_t capacity = 0;
  std::uint32_t head = 0;
  std::uint32_t count = 0;
  bool vectorReadable = false;
};

bool GetAtlasView(volatile LONG* handleStorage, AtlasView* out,
                  AtlasDiagnostics* diagnostics, const char** failure) {
  if (failure) *failure = "invalid_output";
  if (!handleStorage || !out) return false;
  const auto handle = static_cast<std::uintptr_t>(
      InterlockedCompareExchange(handleStorage, 0, 0));
  if (diagnostics) diagnostics->handle = handle;
  if (!handle) {
    if (failure) *failure = "resource_handle_null";
    return false;
  }
  if (!Readable(reinterpret_cast<const void*>(handle), sizeof(void*))) {
    if (failure) *failure = "resource_handle_unreadable";
    return false;
  }
  const auto atlas = *reinterpret_cast<const std::uintptr_t*>(handle);
  if (diagnostics) diagnostics->resource = atlas;
  if (!atlas) {
    if (failure) *failure = "resource_not_ready";
    return false;
  }
  if (!Readable(reinterpret_cast<const void*>(atlas), 16)) {
    if (failure) *failure = "atlas_header_unreadable";
    return false;
  }
  const auto* fields = reinterpret_cast<const std::uint32_t*>(atlas);
  if (diagnostics) {
    diagnostics->items = fields[0];
    diagnostics->capacity = fields[1];
    diagnostics->head = fields[2];
    diagnostics->count = fields[3];
    diagnostics->vectorReadable = true;
  }
  // The native lookup treats [2] as a monotonic ring-buffer head and wraps it
  // by subtracting [1]. It is therefore valid for head >= capacity.
  if (!fields[0] || !fields[1] || !fields[3]) {
    if (failure) *failure = "atlas_vector_empty";
    return false;
  }
  if (fields[3] > fields[1]) {
    if (failure) *failure = "atlas_count_exceeds_capacity";
    return false;
  }
  if (!Readable(reinterpret_cast<const void*>(fields[0]),
      static_cast<std::size_t>(fields[1]) * sizeof(std::uint32_t))) {
    if (failure) *failure = "atlas_items_unreadable";
    return false;
  }
  *out = {reinterpret_cast<std::uint32_t*>(fields[0]), fields[1], fields[2], fields[3]};
  if (failure) *failure = nullptr;
  return true;
}

void LogUnavailableOnce(std::uint32_t encoded, const char* reason,
                        const AtlasView* atlas, const AtlasDiagnostics* diagnostics);

std::uint32_t* FindRegion(Region target) {
  const auto token = EncodedIndex(target);
  const auto* registered = Registry().FindByRegionToken(token);
  if (!registered) return nullptr;
  auto* handleStorage = &g_dynamicAtlasHandle;
  AtlasView atlas{};
  AtlasDiagnostics diagnostics{};
  const char* failure = nullptr;
  if (!GetAtlasView(handleStorage, &atlas, &diagnostics, &failure)) {
    LogUnavailableOnce(EncodedIndex(target), failure, &atlas, &diagnostics);
    return nullptr;
  }
  const char* name = registered->descriptor.regionName.c_str();
  if (!name || !*name) {
    LogUnavailableOnce(token, "region_name_unregistered", &atlas, &diagnostics);
    return nullptr;
  }
  for (std::uint32_t i = 0; i < atlas.count; ++i) {
    const auto slot = CircularSlot(atlas.head, atlas.capacity, i);
    if (slot >= atlas.capacity) return nullptr;
    auto* region = reinterpret_cast<std::uint32_t*>(atlas.items[slot]);
    if (!region || !Readable(region, 0x78)) continue;
    const auto length = region[0x74 / 4];
    if (length != std::strlen(name) || !region[0x70 / 4]) continue;
    const auto* regionName = reinterpret_cast<const char*>(region[0x70 / 4]);
    if (!Readable(regionName, length + 1)) continue;
    if (std::memcmp(regionName, name, length) == 0 && regionName[length] == '\0') {
      return region;
    }
  }
  LogUnavailableOnce(EncodedIndex(target), "region_name_missing", &atlas, &diagnostics);
  return nullptr;
}

void LogUnavailableOnce(std::uint32_t encoded, const char* reason, const AtlasView* atlas,
                        const AtlasDiagnostics* diagnostics) {
  std::uint32_t id = 0;
  if (DecodeCustomRegionToken(encoded, &id) &&
      FirstEvent(g_loggedUnavailable, static_cast<Region>(id)) &&
      g_api && g_api->log) {
    const auto name = RegionName(static_cast<Region>(id));
    if (atlas && atlas->items) {
      g_api->log("cc2x_ui_second_atlas", CC2X_LOG_WARN,
          "event=SECOND_ATLAS_REGION_NOT_READY region=%s reason=%s items=%p capacity=%u head=%u count=%u",
          name, reason ? reason : "unknown", atlas->items,
          atlas->capacity, atlas->head, atlas->count);
    } else {
      g_api->log("cc2x_ui_second_atlas", CC2X_LOG_WARN,
          "event=SECOND_ATLAS_REGION_NOT_READY region=%s reason=%s handle=%p resource=%p vector=(items=%p,capacity=%u,head=%u,count=%u,readable=%u); no vanilla substitution performed",
          name, reason ? reason : "unknown",
          reinterpret_cast<void*>(diagnostics ? diagnostics->handle : 0),
          reinterpret_cast<void*>(diagnostics ? diagnostics->resource : 0),
          reinterpret_cast<void*>(static_cast<std::uintptr_t>(diagnostics ? diagnostics->items : 0)),
          diagnostics ? diagnostics->capacity : 0,
          diagnostics ? diagnostics->head : 0,
          diagnostics ? diagnostics->count : 0,
          diagnostics && diagnostics->vectorReadable ? 1u : 0u);
    }
  }
}

void __cdecl OnResourceLoad(cc2x_cpu* cpu, void*) {
  EnsureAtlasLoaded(cpu);
}

void __cdecl OnRegionLookupCapture(cc2x_cpu* cpu, void*) {
  if (!cpu) return;
  Region region{};
  const auto argumentAddress = static_cast<std::uintptr_t>(cpu->ebp) + 8;
  if (!Readable(reinterpret_cast<const void*>(argumentAddress), sizeof(std::uint32_t))) {
    g_lookupContexts.Push(static_cast<Region>(0));
    return;
  }
  const auto* name = reinterpret_cast<const char*>(
      *reinterpret_cast<const std::uint32_t*>(argumentAddress));
  if (IsCustomName(name, &region)) {
    g_lookupContexts.Push(region);
  } else {
    g_lookupContexts.Push(static_cast<Region>(0));
  }
}

void __cdecl OnRegionLookupReturn(cc2x_cpu* cpu, void*) {
  if (!cpu) return;
  const Region region = g_lookupContexts.Pop();
  if (region != static_cast<Region>(0)) {
    const auto token = EncodedIndex(region);
    cpu->esi = token;
    if (FirstEvent(g_loggedLookup, region) && g_api && g_api->log) {
      const auto name = RegionName(region);
      g_api->log("cc2x_ui_second_atlas", CC2X_LOG_INFO,
          "event=SECOND_ATLAS_REGION_LOOKUP region=%s token=0x%08X name=%s",
          name, token, name);
    }
  }
}

void ApplyPosition(void* ui, std::uint32_t xBits, std::uint32_t yBits) {
  float position[2]{};
  std::memcpy(&position[0], &xBits, sizeof(float));
  std::memcpy(&position[1], &yBits, sizeof(float));
  using PositionFn = void (__thiscall*)(void*, float*);
  reinterpret_cast<PositionFn>(g_api->va(kSetUiPosition))(ui, position);
}

bool DecodeAndFindRegion(std::uint32_t value, std::uint32_t** region) {
  std::uint32_t id = 0;
  if (!DecodeCustomRegionToken(value, &id)) return false;
  const auto which = static_cast<Region>(id);
  *region = FindRegion(which);
  if (*region && FirstEvent(g_loggedRender, which) && g_api && g_api->log) {
    const auto name = RegionName(which);
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(*region);
    const auto surface = *reinterpret_cast<const std::uint32_t*>(*region);
    const auto x0 = *reinterpret_cast<const float*>(bytes + 0x50);
    const auto x1 = *reinterpret_cast<const float*>(bytes + 0x54);
    const auto y0 = *reinterpret_cast<const float*>(bytes + 0x58);
    const auto y1 = *reinterpret_cast<const float*>(bytes + 0x5C);
    g_api->log("cc2x_ui_second_atlas", CC2X_LOG_INFO,
        "event=SECOND_ATLAS_REGION_RENDER region=%s token=0x%08X record=%p surface=0x%08X uv=(%.3f,%.3f,%.3f,%.3f)",
        name, value, *region, surface, x0, x1, y0, y1);
  }
  return true;
}

void __cdecl OnUpdateImage(cc2x_cpu* cpu, void*) {
  if (!cpu) return;
  auto* stack = CC2X_STACK(cpu);
  std::uint32_t* region = nullptr;
  if (!DecodeAndFindRegion(stack[3], &region)) return;
  if (region) {
    ApplyPosition(reinterpret_cast<void*>(cpu->ecx), stack[1], stack[2]);
    using QueueFn = void (__thiscall*)(void*, std::uint32_t*, std::uint32_t,
                                      std::uint32_t, std::uint32_t);
    reinterpret_cast<QueueFn>(g_api->va(kQueueUiImage))(
        reinterpret_cast<void*>(cpu->ecx), region, stack[4], stack[5], 0x3F800000u);
  }
  cpu->jump_to = static_cast<std::uint32_t>(g_api->va(kImageReturn));
}

void __cdecl OnUpdateImagePower(cc2x_cpu* cpu, void*) {
  if (!cpu) return;
  auto* stack = CC2X_STACK(cpu);
  std::uint32_t* region = nullptr;
  if (!DecodeAndFindRegion(stack[3], &region)) return;
  if (region) {
    ApplyPosition(reinterpret_cast<void*>(cpu->ecx), stack[1], stack[2]);
    using QueueFn = void (__thiscall*)(void*, std::uint32_t*, std::uint32_t,
                                      std::uint32_t, std::uint32_t);
    reinterpret_cast<QueueFn>(g_api->va(kQueueUiImage))(
        reinterpret_cast<void*>(cpu->ecx), region, stack[4], stack[5], stack[6]);
  }
  cpu->jump_to = static_cast<std::uint32_t>(g_api->va(kImagePowerReturn));
}

void __cdecl OnUpdateImageRot(cc2x_cpu* cpu, void*) {
  if (!cpu) return;
  auto* stack = CC2X_STACK(cpu);
  std::uint32_t* region = nullptr;
  if (!DecodeAndFindRegion(stack[3], &region)) return;
  if (region) {
    ApplyPosition(reinterpret_cast<void*>(cpu->ecx), stack[1], stack[2]);
    const auto self = reinterpret_cast<void*>(cpu->ecx);
    const auto color = stack[4];
    float angle{};
    std::memcpy(&angle, &stack[5], sizeof(angle));
    auto queue = g_api->va(kQueueUiRotatedImage);
    __asm {
      movss xmm3, angle
      push color
      push region
      mov ecx, self
      call queue
    }
  }
  cpu->jump_to = static_cast<std::uint32_t>(g_api->va(kImageRotReturn));
}

void __cdecl OnGetImageSize(cc2x_cpu* cpu, void*) {
  if (!cpu) return;
  auto* stack = CC2X_STACK(cpu);
  std::uint32_t* region = nullptr;
  if (!DecodeAndFindRegion(stack[2], &region)) return;
  if (region && Readable(reinterpret_cast<void*>(stack[1]), sizeof(float) * 2)) {
    auto* size = reinterpret_cast<float*>(stack[1]);
    size[0] = *reinterpret_cast<float*>(reinterpret_cast<std::uint8_t*>(region) + 0x54) -
              *reinterpret_cast<float*>(reinterpret_cast<std::uint8_t*>(region) + 0x50);
    size[1] = *reinterpret_cast<float*>(reinterpret_cast<std::uint8_t*>(region) + 0x5C) -
              *reinterpret_cast<float*>(reinterpret_cast<std::uint8_t*>(region) + 0x58);
    cpu->eax = stack[1];
  } else if (!region) {
    if (Readable(reinterpret_cast<void*>(stack[1]), sizeof(float) * 2)) {
      auto* size = reinterpret_cast<float*>(stack[1]);
      size[0] = 0.0f;
      size[1] = 0.0f;
      cpu->eax = stack[1];
    }
  }
  cpu->jump_to = static_cast<std::uint32_t>(g_api->va(kSizeReturn));
}

bool StageCall(const cc2x_api* api, const char* name, std::uint32_t address,
               std::size_t length, const std::uint8_t* expected, void* callback,
               void** original) {
  const int staged = api->patch_call(address, static_cast<std::uint32_t>(length),
                                     expected, callback, original);
  if (!staged) {
    api->log("cc2x_ui_second_atlas", CC2X_LOG_ERROR,
        "event=SECOND_ATLAS_HOOK_STAGE_FAILED hook=%s va=0x%08X len=%u; transaction will be aborted",
        name, address, static_cast<unsigned>(length));
    return false;
  }
  return true;
}

bool StageMidHook(const cc2x_api* api, const char* name, std::uint32_t address,
                  std::size_t length, const std::uint8_t* expected,
                  cc2x_mid_fn callback) {
  const int staged = api->patch_mid(address, static_cast<std::uint32_t>(length),
                                    expected, callback, nullptr);
  if (!staged) {
    api->log("cc2x_ui_second_atlas", CC2X_LOG_ERROR,
        "event=SECOND_ATLAS_HOOK_STAGE_FAILED hook=%s va=0x%08X len=%u; transaction will be aborted",
        name, address, static_cast<unsigned>(length));
    return false;
  }
  return true;
}

}  // namespace

bool Install(const cc2x_api* api) {
  if (InterlockedCompareExchange(&g_installed, 0, 0)) return true;
  if (!api || api->api_version < 5 || !api->game_build || !api->va || !api->log ||
      !api->patch_begin || !api->patch_call || !api->patch_mid ||
      !api->patch_commit || !api->patch_abort) return false;
  const bool hasDynamicAtlas = Registry().FindByVirtualPath(kAtlasPath) != nullptr;
  if (!hasDynamicAtlas) {
    api->log("cc2x_ui_second_atlas", CC2X_LOG_INFO,
        "event=SECOND_ATLAS_SKIPPED reason=no_custom_ui_resources");
    return true;
  }
  if (std::strcmp(api->game_build, "1.5.18-win32-r2") != 0) {
    api->log("cc2x_ui_second_atlas", CC2X_LOG_WARN,
        "event=SECOND_ATLAS_PROFILE_UNAVAILABLE build=%s", api->game_build);
    return false;
  }

  g_api = api;
  api->patch_begin("cc2x_ui_second_atlas");
  bool staged = true;
  for (std::size_t i = 0; i < std::size(kResourceReadCalls); ++i) {
    staged = StageCall(api, "active_mod_direct_read", kResourceReadCalls[i],
        sizeof(kResourceReadCallExpected[i]), kResourceReadCallExpected[i],
        reinterpret_cast<void*>(OnResourceRead), &g_originalResourceRead) && staged;
  }
  staged = StageMidHook(api, "resource_load", kResourceLoadCall,
      sizeof(kResourceLoadExpected), kResourceLoadExpected, OnResourceLoad) && staged;
  staged = StageCall(api, "atlas_parser_call", kAtlasParserCall,
      sizeof(kAtlasParserCallExpected), kAtlasParserCallExpected,
      reinterpret_cast<void*>(OnAtlasParser), &g_originalAtlasParser) && staged;
  staged = StageMidHook(api, "atlas_populate_return", kAtlasPopulateReturn,
      sizeof(kAtlasPopulateReturnExpected), kAtlasPopulateReturnExpected,
      OnAtlasPopulateReturn) && staged;
  staged = StageMidHook(api, "region_lookup_capture", kRegionLookupCapture,
      sizeof(kRegionLookupCaptureExpected), kRegionLookupCaptureExpected,
      OnRegionLookupCapture) && staged;
  staged = StageMidHook(api, "region_lookup_return", kRegionLookupReturn,
      sizeof(kRegionLookupReturnExpected), kRegionLookupReturnExpected,
      OnRegionLookupReturn) && staged;
  staged = StageMidHook(api, "image_update", kUpdateImageEntry,
      sizeof(kImageEntryExpected), kImageEntryExpected, OnUpdateImage) && staged;
  staged = StageMidHook(api, "image_power_update", kUpdateImagePowerEntry,
      sizeof(kImagePowerEntryExpected), kImagePowerEntryExpected,
      OnUpdateImagePower) && staged;
  staged = StageMidHook(api, "image_rotated_update", kUpdateImageRotEntry,
      sizeof(kImageRotEntryExpected), kImageRotEntryExpected,
      OnUpdateImageRot) && staged;
  staged = StageMidHook(api, "image_size", kGetImageSizeEntry,
      sizeof(kImageSizeEntryExpected), kImageSizeEntryExpected,
      OnGetImageSize) && staged;
  if (!staged) {
    api->patch_abort();
    g_api = nullptr;
    api->log("cc2x_ui_second_atlas", CC2X_LOG_ERROR,
        "event=SECOND_ATLAS_HOOK_INSTALL_FAILED stage=patch_mid build=%s; no hooks committed",
        api->game_build);
    return false;
  }
  if (!api->patch_commit()) {
    api->patch_abort();
    g_api = nullptr;
    api->log("cc2x_ui_second_atlas", CC2X_LOG_ERROR,
        "event=SECOND_ATLAS_HOOK_INSTALL_FAILED stage=patch_commit build=%s; no hooks committed",
        api->game_build);
    return false;
  }
  InterlockedExchange(&g_installed, 1);
  api->log("cc2x_ui_second_atlas", CC2X_LOG_INFO,
      "event=SECOND_ATLAS_HOOKS_INSTALLED build=%s dynamic_path=%s dynamic_regions=%u",
      api->game_build, kAtlasPath,
      static_cast<unsigned>(Registry().Count() ? Registry().Count() - 1 : 0));
  return true;
}

}  // namespace cc2::api::ui::second_atlas
