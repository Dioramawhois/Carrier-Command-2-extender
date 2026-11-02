#include "runtime/host.h"
#include "api/animation/animation_speed.h"
namespace {
cc2::runtime::Host* g_host=nullptr;
}
extern "C" int __cdecl cc2x_native_bridge_builtin_load(const cc2x_api* api) {
  // dinput8.dll owns this process-lifetime Host. Initialization subscribes to
  // CC2X callbacks only; Lua setup is deferred until the server tick.
  if(!g_host)g_host=new cc2::runtime::Host();
  return g_host->Attach(api)?1:0;
}
extern "C" int __cdecl cc2x_native_bridge_carrier_throttle(int step) {
  return g_host&&g_host->QueueThrottle(step)?1:0;
}
extern "C" int __cdecl cc2x_native_bridge_animation_speed(std::uint32_t group,std::uint32_t multiplier) {
  if(group>CC2X_ANIMATION_CRANE)return 0;
  return cc2::api::animation::SetSpeed(static_cast<cc2::api::animation::Group>(group),multiplier)?1:0;
}
extern "C" int __cdecl cc2x_native_bridge_carrier_hangar_spawn(std::uint32_t carrierId,std::uint32_t slot,std::uint32_t chassisType) {
  return g_host?static_cast<int>(g_host->QueueHangarSpawn(carrierId,slot,chassisType)):0;
}
