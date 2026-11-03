#include "runtime/host.h"
#include "api/animation/animation_speed.h"
#include "api/mods/workshop_upload.h"
#include "api/vehicle/definition_adapter.h"
#include "api/vehicle/ui_library_injection.h"
#include "api/vehicle/buttons.h"
#include <cstring>
namespace cc2::runtime {
namespace {
Host* target=nullptr;
using CarrierUpdate=std::uint32_t(__thiscall*)(void*,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t);
CarrierUpdate original=nullptr;
std::uint32_t __fastcall Update(void*self,void*,std::uint32_t a,std::uint32_t b,std::uint32_t c,std::uint32_t d,std::uint32_t e,std::uint32_t f,std::uint32_t g) {
  auto result=original(self,a,b,c,d,e,f,g);if(target)target->AfterCarrierUpdate(self);return result;
}
void __cdecl ObserveEarlyResourceInit(cc2x_cpu*,void*) {
  if(target)target->ObserveGameResourceInit();
}
void OnButtons(api::vehicle::ButtonContext* context,void* user){static_cast<Host*>(user)->ButtonsEvent(context);}
int OnButtonQuery(api::vehicle::ButtonContext* context,void* user){return static_cast<Host*>(user)->ButtonQueryEvent(context);}
bool ButtonsInterested(const char* name,void* user){return static_cast<Host*>(user)->HasEventSubscribers(name);}
}
bool InstallNativeExtensions(const cc2x_api* api,Host* host) {
  if(!api->va||!api->patch_ptr||!api->patch_begin||!api->patch_commit||!api->patch_abort||!api->patch_bytes)return false;
  // Workshop staging is an engine boundary: preserve arbitrary package files
  // instead of letting the stock content-type worker reject them.
  (void)api::mods::InstallWorkshopUploadSupport(api);
  std::uint32_t vtableVa=0,expected55=0,updateVa=0,expected57=0;
  if(api->game_build&&std::strcmp(api->game_build,"1.5.18-win32")==0) {
    vtableVa=0xD596CC;expected55=0x5ECAE0;updateVa=0x705AC0;expected57=0x706B80;
  } else if(api->game_build&&std::strcmp(api->game_build,"1.5.18-win32-r2")==0) {
    // c_vehicle_server_carrier RTTI/COL points at this vtable. Current Ghidra
    // evidence: slots 55/56/57 = 0x5ECC20/0x705BD0/0x706C90. Slot 56 still
    // receives seven stack words and updates control_factors at this+0xC90.
    vtableVa=0xD596EC;expected55=0x5ECC20;updateVa=0x705BD0;expected57=0x706C90;
  } else {
    if(api->log)api->log("native_bridge",CC2X_LOG_WARN,
      "event=NATIVE_PROFILE_UNAVAILABLE build=%s; throttle, hangar and animation disabled",
      api->game_build?api->game_build:"unknown");
    return false;
  }
  (void)api::vehicle::InstallStockUiLibraryInjection(api);
  if(!api::vehicle::ConfigureButtons(api,OnButtons,host,OnButtonQuery,ButtonsInterested)&&api->log)
    api->log("cc2x_controls",CC2X_LOG_WARN,"event=BUTTON_PROFILE_UNAVAILABLE; named control writes disabled");
  auto**vtable=reinterpret_cast<void**>(api->va(vtableVa));
  const auto actual55=reinterpret_cast<std::uintptr_t>(vtable[55])-static_cast<std::uintptr_t>(api->module_base)+0x400000;
  const auto actual57=reinterpret_cast<std::uintptr_t>(vtable[57])-static_cast<std::uintptr_t>(api->module_base)+0x400000;
  if(actual55!=expected55||actual57!=expected57) {
    if(api->log)api->log("native_bridge",CC2X_LOG_ERROR,
      "event=THROTTLE_VTABLE_MISMATCH build=%s slot55=0x%08lx expected55=0x%08lx slot57=0x%08lx expected57=0x%08lx",
      api->game_build?api->game_build:"unknown",static_cast<unsigned long>(actual55),static_cast<unsigned long>(expected55),
      static_cast<unsigned long>(actual57),static_cast<unsigned long>(expected57));
    return false;
  }
  api->patch_begin("native_bridge.throttle");void*saved=nullptr;
  if(!api->patch_ptr(vtableVa+56*4,updateVa,reinterpret_cast<void*>(&Update),&saved)){
    api->patch_abort();
    if(api->log)api->log("native_bridge",CC2X_LOG_ERROR,
      "event=THROTTLE_HOOK_INSTALL_FAILED build=%s slot56_va=0x%08lx reason=expected_pointer_mismatch",
      api->game_build?api->game_build:"unknown",static_cast<unsigned long>(vtableVa+56*4));
    return false;
  }
  original=reinterpret_cast<CarrierUpdate>(saved);target=host;
  if(!api->patch_commit()){
    original=nullptr;target=nullptr;
    if(api->log)api->log("native_bridge",CC2X_LOG_ERROR,"event=THROTTLE_HOOK_COMMIT_FAILED build=%s",api->game_build?api->game_build:"unknown");
    return false;
  }
  if(api->log)api->log("native_bridge",CC2X_LOG_INFO,
    "event=THROTTLE_HOOK_INSTALLED build=%s slot56_va=0x%08lx target_va=0x%08lx",
    api->game_build?api->game_build:"unknown",static_cast<unsigned long>(vtableVa+56*4),static_cast<unsigned long>(updateVa));
  if(api->game_build&&std::strcmp(api->game_build,"1.5.18-win32-r2")==0) {
    // FUN_00771BA0 is the cold game-context initializer. Its first resource
    // requests (button/medal atlases) precede FUN_005A2E70 definition loading.
    // This passive hook records whether the process catalog was already ready;
    // no filesystem or Lua work is performed on the game thread here.
    constexpr std::uint8_t expected[]{0x55,0x8B,0xEC,0x6A,0xFF};
    api->patch_begin("native_bridge.early_resource_init_observer");
    if(!api->patch_mid(0x00771BA0u,sizeof(expected),expected,
          ObserveEarlyResourceInit,nullptr)||!api->patch_commit()) {
      api->patch_abort();
      if(api->log)api->log("native_bridge",CC2X_LOG_WARN,
          "event=EARLY_RESOURCE_INIT_OBSERVER_UNAVAILABLE build=%s",api->game_build);
    }
  }
  const auto animationStatus=api::animation::InitializeWithApi(api);
  if(api->log)api->log("native_bridge",animationStatus==ERROR_SUCCESS?CC2X_LOG_INFO:CC2X_LOG_WARN,
    animationStatus==ERROR_SUCCESS?"event=ANIMATION_PROFILE_READY build=%s":"event=ANIMATION_PROFILE_UNAVAILABLE build=%s status=%lu",
    api->game_build?api->game_build:"unknown",static_cast<unsigned long>(animationStatus));
  if(!cc2::api::vehicle::definition_adapter::Install(api))
    api->log("cc2x_vehicle_definitions",CC2X_LOG_WARN,
      "event=DEFINITION_ADAPTER_UNAVAILABLE; established native APIs remain active");
  // Throttle remains available if another plugin legitimately owns an
  // animation site; readiness of animation is reported independently.
  return true;
}
}
