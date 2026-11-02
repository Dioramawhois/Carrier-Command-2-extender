#include "custom_registration_probe.h"
#include "custom_registration_probe_contract.h"
#include "api/vehicle/definition_adapter.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>

namespace cc2::research::custom_registration_probe {
namespace {
// The CC2X patch API accepts preferred image VAs (the same address form used
// by its profile tables), not RVAs. Keep the conversion explicit here so a
// profile offset cannot underflow inside va() and silently fail readability.
constexpr std::uint32_t kLegacyFactoryVa=0x005EF970;
constexpr std::uint32_t kSteamHangarWrapperVa=0x005EE840;
constexpr std::uint32_t kExpectedServerSize=0xE50;
constexpr std::uint32_t kContextOffset=0x1BC;
constexpr std::uint32_t kDefinitionLoaderVa=0x005A2E70;
constexpr std::uint32_t kDefinitionResizeVa=0x0084F550;
constexpr std::uint32_t kNameMapResizeVa=0x00853E10;
constexpr std::uint32_t kDefinitionParseVa=0x005A5E80;
constexpr std::uint32_t kDefinitionSlots=0x66;
constexpr std::size_t kDefinitionObservationCapacity=32;
constexpr LONG kIdle=0,kArmed=1,kCaptured=2,kFlushing=3,kCapturing=4,kPreparing=5;
volatile LONG g_enabled=0;
volatile LONG g_state=kIdle;
volatile LONG g_generation=0;
volatile LONG g_armTick=0;
volatile LONG g_boundary=0;
void* volatile g_server=nullptr;
FactoryObservation g_observation{};

enum class DefinitionEventKind : std::uint32_t { LoaderEntry=1, DefinitionResize=2, NameMapResize=3 };
struct DefinitionObservation {
  volatile LONG ready=0;
  std::uint32_t kind=0, thisPointer=0, returnAddress=0;
  std::uint32_t args[4]{};
  std::uint32_t rawWords[4]{};
  std::uint32_t generation=0, server=0;
};
DefinitionObservation g_definitionObservations[kDefinitionObservationCapacity]{};
volatile LONG g_definitionObservationCount=0;
volatile LONG g_definitionObservationOverflow=0;
volatile LONG g_definitionFlushCursor=0;
volatile LONG g_parseCalls=0;
volatile LONG g_parseMaxIndex=-1;
volatile LONG g_parseLastIndex=-1;

bool ReadableProtection(DWORD protect) {
  if(protect&(PAGE_GUARD|PAGE_NOACCESS))return false;
  switch(protect&0xff) {
    case PAGE_READONLY: case PAGE_READWRITE: case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ: case PAGE_EXECUTE_READWRITE: case PAGE_EXECUTE_WRITECOPY:return true;
    default:return false;
  }
}

bool QueryRegion(std::uintptr_t address,MEMORY_BASIC_INFORMATION* mbi) {
  return address&&mbi&&VirtualQuery(reinterpret_cast<const void*>(address),mbi,sizeof(*mbi))==sizeof(*mbi)&&
      mbi->State==MEM_COMMIT&&ReadableProtection(mbi->Protect);
}

bool CopyReadableWords(std::uint32_t address,std::uint32_t* words,std::size_t count) {
  if(!address||!words||!count||address>UINTPTR_MAX-count*sizeof(std::uint32_t))return false;
  MEMORY_BASIC_INFORMATION mbi{};
  if(!QueryRegion(address,&mbi))return false;
  const auto base=reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
  const auto end=base+mbi.RegionSize;
  const auto begin=static_cast<std::uintptr_t>(address);
  if(begin<base||begin>end||count*sizeof(std::uint32_t)>end-begin)return false;
  std::memcpy(words,reinterpret_cast<const void*>(begin),count*sizeof(std::uint32_t));
  return true;
}

void CaptureDefinitionEvent(DefinitionEventKind kind,const cc2x_cpu* cpu,const std::uint32_t* stack,
                            std::uint32_t argCount) {
  if(!cpu||!stack)return;
  const auto index=InterlockedIncrement(&g_definitionObservationCount)-1;
  if(index<0||static_cast<std::size_t>(index)>=kDefinitionObservationCapacity) {
    InterlockedIncrement(&g_definitionObservationOverflow);
    return;
  }
  auto& event=g_definitionObservations[index];
  event.ready=0;
  event.kind=static_cast<std::uint32_t>(kind);
  event.thisPointer=cpu->ecx;
  event.returnAddress=stack[0];
  for(std::uint32_t i=0;i<4;++i)event.args[i]=i<argCount?stack[i+1]:0;
  std::memset(event.rawWords,0,sizeof(event.rawWords));
  (void)CopyReadableWords(event.thisPointer,event.rawWords,4);
  event.generation=static_cast<std::uint32_t>(InterlockedCompareExchange(&g_generation,0,0));
  event.server=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(
      InterlockedCompareExchangePointer(&g_server,nullptr,nullptr)));
  InterlockedExchange(&event.ready,1);
}

void __cdecl OnDefinitionLoaderEntry(cc2x_cpu* cpu,void*) {
  cc2::api::vehicle::definition_adapter::BeginDefinitionLoad();
  CaptureDefinitionEvent(DefinitionEventKind::LoaderEntry,cpu,CC2X_STACK(cpu),4);
}

void __cdecl OnDefinitionResizeEntry(cc2x_cpu* cpu,void*) {
  CaptureDefinitionEvent(DefinitionEventKind::DefinitionResize,cpu,CC2X_STACK(cpu),1);
}

void __cdecl OnNameMapResizeEntry(cc2x_cpu* cpu,void*) {
  CaptureDefinitionEvent(DefinitionEventKind::NameMapResize,cpu,CC2X_STACK(cpu),1);
  cc2::api::vehicle::definition_adapter::OnNameMapResize(cpu);
}

void __cdecl OnDefinitionParseEntry(cc2x_cpu* cpu,void*) {
  const auto* stack=CC2X_STACK(cpu);
  if(!stack)return;
  InterlockedIncrement(&g_parseCalls);
  const auto index=static_cast<LONG>(stack[3]);
  InterlockedExchange(&g_parseLastIndex,index);
  LONG previous=InterlockedCompareExchange(&g_parseMaxIndex,0,0);
  while(index>previous) {
    const LONG observed=InterlockedCompareExchange(&g_parseMaxIndex,index,previous);
    if(observed==previous)break;
    previous=observed;
  }
}

bool ReadDefinitionBase(std::uint32_t context,std::uint32_t* base,MEMORY_BASIC_INFORMATION* contextRegion) {
  if(!context||!base)return false;
  if(static_cast<std::uintptr_t>(context)>UINTPTR_MAX-8)return false;
  const auto field=static_cast<std::uintptr_t>(context)+8;
  MEMORY_BASIC_INFORMATION mbi{};
  if(field>UINTPTR_MAX-sizeof(std::uint32_t)||!QueryRegion(field,&mbi))return false;
  const auto regionEnd=reinterpret_cast<std::uintptr_t>(mbi.BaseAddress)+mbi.RegionSize;
  if(field+sizeof(std::uint32_t)>regionEnd)return false;
  std::memcpy(base,reinterpret_cast<const void*>(field),sizeof(*base));
  if(contextRegion)*contextRegion=mbi;
  return true;
}

void __cdecl OnFactoryEntry(cc2x_cpu* cpu,void*) {
  if(InterlockedCompareExchange(&g_state,kCapturing,kArmed)!=kArmed)return;
  const auto* stack=CC2X_STACK(cpu);
  const auto generation=static_cast<std::uint32_t>(InterlockedCompareExchange(&g_generation,0,0));
  const auto server=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(
      InterlockedCompareExchangePointer(&g_server,nullptr,nullptr)));
  // Keep this hook integer-only: no formatting, floating point, heap access,
  // engine calls, or pointer dereferences on the factory's entry path.
  g_observation=InterlockedCompareExchange(&g_boundary,0,0)==1
      ? DecodeFactoryWrapperEntry(cpu,stack,generation,server)
      : DecodeFactoryEntry(cpu,stack,generation,server);
  InterlockedExchange(&g_state,kCaptured);
}
}

bool Install(const cc2x_api* api) {
  if(InterlockedCompareExchange(&g_enabled,0,0))return true;
  if(!api||api->api_version<5||!api->config_dir||!api->log||!api->patch_begin||!api->patch_mid||
     !api->patch_commit||!api->patch_abort)return false;
  char marker[MAX_PATH*2]{};
  const int n=sprintf_s(marker,sizeof(marker),"%s\\native_bridge\\custom_entity_registration_probe.enabled",api->config_dir);
  if(n<0||static_cast<std::size_t>(n)>=sizeof(marker)) {
    api->log("custom_entity_registration_probe",CC2X_LOG_ERROR,"event=PROBE_OPT_IN_PATH_INVALID");
    return false;
  }
  const bool probeRequested=GetFileAttributesA(marker)!=INVALID_FILE_ATTRIBUTES;
  const bool definitionProfile=api->game_build&&
      std::strcmp(api->game_build,"1.5.18-win32-r2")==0;
  if(!probeRequested&&!definitionProfile) {
    api->log("custom_entity_registration_probe",CC2X_LOG_INFO,
        "event=PROBE_DISABLED opt_in_file=%s",marker);
    return true;
  }

  const std::uint8_t* expected=nullptr;
  std::uint32_t siteVa=0;
  std::uint32_t siteLength=0;
  LONG boundary=0;
  bool installFactoryHook=true;
  static const std::uint8_t legacyExpected[6]={0x55,0x8B,0xEC,0x83,0xEC,0x08};
  static const std::uint8_t steamWrapperExpected[5]={0x55,0x8B,0xEC,0x6A,0xFF};
  static const std::uint8_t definitionLoaderExpected[5]={0x55,0x8B,0xEC,0x6A,0xFF};
  // Include the complete MOV ESI,ECX instruction (8B F1). A six-byte detour
  // would split this instruction and corrupt execution through the trampoline.
  static const std::uint8_t definitionResizeExpected[7]={0x55,0x8B,0xEC,0x53,0x56,0x8B,0xF1};
  static const std::uint8_t nameMapResizeExpected[6]={0x55,0x8B,0xEC,0x51,0x53,0x56};
  static const std::uint8_t definitionParseExpected[5]={0x55,0x8B,0xEC,0x6A,0xFF};
  if(probeRequested&&api->game_build&&std::strcmp(api->game_build,"1.5.18-win32")==0) {
    siteVa=kLegacyFactoryVa;siteLength=sizeof(legacyExpected);expected=legacyExpected;boundary=0;
  } else if(api->game_build&&std::strcmp(api->game_build,"1.5.18-win32-r2")==0) {
    // The current Hangar backend validates and hooks this same entry. A probe
    // detour here would make its later exact-byte validation fail, so this
    // build observes only definition-loader paths and exposes no F7 arm API.
    // Keep the wrapper address/guard for static contract tests and legacy
    // diagnostics, but never patch it in the current integrated profile.
    siteVa=kSteamHangarWrapperVa;siteLength=sizeof(steamWrapperExpected);expected=steamWrapperExpected;boundary=1;
    installFactoryHook=false;
  } else {
    api->log("custom_entity_registration_probe",CC2X_LOG_WARN,
        "event=PROBE_PROFILE_UNAVAILABLE build=%s",api->game_build?api->game_build:"unknown");
    return false;
  }
  api->patch_begin("custom_entity_registration_probe");
  bool hooksInstalled=!installFactoryHook||api->patch_mid(siteVa,siteLength,expected,OnFactoryEntry,nullptr)!=0;
  if(hooksInstalled&&boundary==1) {
    hooksInstalled=api->patch_mid(kDefinitionLoaderVa,sizeof(definitionLoaderExpected),
        definitionLoaderExpected,OnDefinitionLoaderEntry,nullptr)!=0 &&
      api->patch_mid(kDefinitionResizeVa,sizeof(definitionResizeExpected),
        definitionResizeExpected,OnDefinitionResizeEntry,nullptr)!=0 &&
      api->patch_mid(kNameMapResizeVa,sizeof(nameMapResizeExpected),
        nameMapResizeExpected,OnNameMapResizeEntry,nullptr)!=0 &&
      api->patch_mid(kDefinitionParseVa,sizeof(definitionParseExpected),
        definitionParseExpected,OnDefinitionParseEntry,nullptr)!=0;
  }
  if(!hooksInstalled||!api->patch_commit()) {
    api->patch_abort();
    api->log("custom_entity_registration_probe",CC2X_LOG_ERROR,
        "event=PROBE_HOOK_INSTALL_FAILED va=0x%08X rva=0x%08X length=%lu definition_hook_va={loader:0x%08X,resize:0x%08X,map:0x%08X,parse:0x%08X} build=%s",
        siteVa,siteVa-0x00400000u,siteLength,kDefinitionLoaderVa,kDefinitionResizeVa,
        kNameMapResizeVa,kDefinitionParseVa,api->game_build?api->game_build:"unknown");
    return false;
  }
  InterlockedExchange(&g_boundary,boundary);
  InterlockedExchange(&g_enabled,installFactoryHook?1:0);
  if(installFactoryHook) {
    api->log("custom_entity_registration_probe",CC2X_LOG_INFO,
        "event=PROBE_HOOK_INSTALLED va=0x%08X rva=0x%08X runtime=%p build=%s boundary=%s mode=one_shot_passive",
        siteVa,siteVa-0x00400000u,reinterpret_cast<void*>(api->va?api->va(siteVa):0),api->game_build,
        boundary?"hangar_wrapper_arguments":"core_factory_arguments");
  } else {
    api->log("custom_entity_registration_probe",CC2X_LOG_INFO,
        "event=FACTORY_PROBE_SKIPPED va=0x%08X reason=overlaps_hangar_backend_exact_entry_validation build=%s",
        siteVa,api->game_build?api->game_build:"unknown");
  }
  if(boundary==1) {
    api->log("custom_entity_registration_probe",CC2X_LOG_INFO,
        "event=DEFINITION_OBSERVER_HOOKS_INSTALLED loader_va=0x%08X definition_resize_va=0x%08X name_map_resize_va=0x%08X parser_va=0x%08X stock_name_slots=0x%X factory_hook=%s mode=passive_no_engine_state_changes",
        kDefinitionLoaderVa,kDefinitionResizeVa,kNameMapResizeVa,kDefinitionParseVa,kDefinitionSlots,
        installFactoryHook?"installed":"skipped_overlap");
  }
  return true;
}

bool Enabled(){return InterlockedCompareExchange(&g_enabled,0,0)!=0;}
bool ArmOneFactoryCall(){
  if(!Enabled()||InterlockedCompareExchange(&g_state,kPreparing,kIdle)!=kIdle)return false;
  InterlockedExchange(&g_armTick,static_cast<LONG>(GetTickCount()));
  InterlockedExchange(&g_state,kArmed);
  return true;
}
void CancelArm(){InterlockedCompareExchange(&g_state,kIdle,kArmed);}

void OnSession(void* server,bool started,std::uint64_t generation) {
  if(started) {
    InterlockedExchangePointer(&g_server,server);
    InterlockedExchange(&g_generation,static_cast<LONG>(generation));
  } else if(InterlockedCompareExchangePointer(&g_server,nullptr,server)==server) {
    InterlockedExchange(&g_generation,static_cast<LONG>(generation));
    CancelArm();
  }
}

void Flush(const cc2x_api* api) {
  if(!api||!api->log)return;
  if(InterlockedCompareExchange(&g_state,0,0)==kArmed) {
    const auto armedAt=static_cast<DWORD>(InterlockedCompareExchange(&g_armTick,0,0));
    if(static_cast<DWORD>(GetTickCount()-armedAt)>10000&&InterlockedCompareExchange(&g_state,kIdle,kArmed)==kArmed)
      api->log("custom_entity_registration_probe",CC2X_LOG_WARN,"event=ARM_EXPIRED timeout_ms=10000");
  }
  if(InterlockedCompareExchange(&g_state,kFlushing,kCaptured)==kCaptured) {
    const FactoryObservation event=g_observation;
    std::uint32_t base=0,candidate=0;
    MEMORY_BASIC_INFORMATION contextMbi{},baseMbi{};
    const auto currentGeneration=static_cast<std::uint32_t>(InterlockedCompareExchange(&g_generation,0,0));
    const auto currentServer=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(
        InterlockedCompareExchangePointer(&g_server,nullptr,nullptr)));
    const bool sameSession=event.generation==currentGeneration&&event.server==currentServer&&currentServer!=0;
    const bool contextReadable=sameSession&&event.boundary==0&&ReadDefinitionBase(event.definitionContext,&base,&contextMbi);
    const bool baseReadable=contextReadable&&base&&QueryRegion(base,&baseMbi);
    const bool candidateKnown=contextReadable&&ComputeDefinitionEntryCandidate(base,event.type,&candidate);
    const auto server=static_cast<std::uintptr_t>(event.server);
    const auto expectedContext=(event.boundary==0&&server&&server<=UINTPTR_MAX-kContextOffset)?server+kContextOffset:0;
    char detail[1024]{};
    sprintf_s(detail,sizeof(detail),
        "generation=%lu session_match=%d boundary=%s server=%p manager=%p return=%p object_id=%lu object_id_status=%s type=%lu transform=%p owner=%p context_argument=%p context_owner_relation=%s context_readable=%d definition_base=%p context_vm={alloc=%p,region=%p,size=0x%zx,protect=0x%lX} base_vm={alloc=%p,region=%p,size=0x%zx,protect=0x%lX} candidate_entry=%s%p candidate_dereferenced=0 declared_server_size=0x%X",
        event.generation,sameSession?1:0,event.boundary?"hangar_wrapper_arguments":"core_factory_arguments",reinterpret_cast<void*>(server),reinterpret_cast<void*>(event.manager),
        reinterpret_cast<void*>(event.returnAddress),event.objectId,event.boundary?"assigned_inside_wrapper":"captured_at_factory_entry",event.type,
        reinterpret_cast<void*>(event.transform),reinterpret_cast<void*>(event.owner),
        reinterpret_cast<void*>(event.definitionContext),event.boundary?"not_dereferenced":(expectedContext&&expectedContext==event.definitionContext?"server_plus_0x1bc_match":"unresolved"),
        contextReadable?1:0,reinterpret_cast<void*>(base),
        contextReadable?contextMbi.AllocationBase:nullptr,contextReadable?contextMbi.BaseAddress:nullptr,
        contextReadable?static_cast<std::size_t>(contextMbi.RegionSize):0,contextReadable?contextMbi.Protect:0,
        baseReadable?baseMbi.AllocationBase:nullptr,baseReadable?baseMbi.BaseAddress:nullptr,
        baseReadable?static_cast<std::size_t>(baseMbi.RegionSize):0,baseReadable?baseMbi.Protect:0,
        candidateKnown?"":"not-computed-",reinterpret_cast<void*>(candidate),kExpectedServerSize);
    api->log("custom_entity_registration_probe",CC2X_LOG_INFO,"event=%s %s",
        event.boundary?"FACTORY_WRAPPER_ENTRY":"FACTORY_ENTRY",detail);
    InterlockedExchange(&g_state,kIdle);
  }

  LONG total=InterlockedCompareExchange(&g_definitionObservationCount,0,0);
  LONG cursor=InterlockedCompareExchange(&g_definitionFlushCursor,0,0);
  const LONG end=(total<static_cast<LONG>(kDefinitionObservationCapacity))?
      total:static_cast<LONG>(kDefinitionObservationCapacity);
  LONG nextCursor=cursor;
  for(LONG i=cursor;i<end;++i) {
    auto& observation=g_definitionObservations[i];
    if(InterlockedCompareExchange(&observation.ready,0,0)!=1)break;
    const char* kind=observation.kind==static_cast<std::uint32_t>(DefinitionEventKind::LoaderEntry)?"DEFINITION_LOADER_ENTRY":
        observation.kind==static_cast<std::uint32_t>(DefinitionEventKind::DefinitionResize)?"DEFINITION_ARRAY_RESIZE_ENTRY":"NAME_MAP_RESIZE_ENTRY";
    api->log("custom_entity_registration_probe",CC2X_LOG_INFO,
        "event=%s generation=%lu server=%p this=%p return=%p args={0x%08X,0x%08X,0x%08X,0x%08X} raw_words={0x%08X,0x%08X,0x%08X,0x%08X} stock_name_slots=0x%X",
        kind,observation.generation,reinterpret_cast<void*>(static_cast<std::uintptr_t>(observation.server)),
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(observation.thisPointer)),
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(observation.returnAddress)),
        observation.args[0],observation.args[1],observation.args[2],observation.args[3],
        observation.rawWords[0],observation.rawWords[1],observation.rawWords[2],observation.rawWords[3],kDefinitionSlots);
    InterlockedExchange(&observation.ready,2);
    nextCursor=i+1;
  }
  if(nextCursor>cursor)InterlockedExchange(&g_definitionFlushCursor,nextCursor);
  const LONG overflow=InterlockedExchange(&g_definitionObservationOverflow,0);
  if(overflow)api->log("custom_entity_registration_probe",CC2X_LOG_WARN,
      "event=DEFINITION_OBSERVATION_OVERFLOW dropped=%ld capacity=%lu",overflow,
      static_cast<unsigned long>(kDefinitionObservationCapacity));
  const LONG parseCalls=InterlockedExchange(&g_parseCalls,0);
  if(parseCalls) {
    const LONG maxIndex=InterlockedExchange(&g_parseMaxIndex,-1);
    const LONG lastIndex=InterlockedExchange(&g_parseLastIndex,-1);
    api->log("custom_entity_registration_probe",CC2X_LOG_INFO,
        "event=DEFINITION_PARSE_BATCH generation=%lu calls=%ld max_index=%ld last_index=%ld declared_stock_name_slots=0x%X",
        static_cast<unsigned long>(InterlockedCompareExchange(&g_generation,0,0)),parseCalls,maxIndex,lastIndex,kDefinitionSlots);
  }
}
}
