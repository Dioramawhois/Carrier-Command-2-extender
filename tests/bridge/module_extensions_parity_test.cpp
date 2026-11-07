// SPDX-License-Identifier: GPL-3.0-or-later
// Actual public C exports and Lua facade over the same synthetic engine.
#include "carrier_fixture.h"
#include "runtime/host.h"
#include "cc2x_settings.h"
#include "cc2x_equipment.h"
#include "cc2x_tile_sites.h"
#include "cc2x_motion.h"
#include "cc2x_navigation.h"
#include "cc2x_framework.h"
extern "C" const cc2x_motion_api* cc2x_query_motion_v1(uint32_t,uint32_t);
extern "C" const cc2x_navigation_api* cc2x_query_navigation_v1(uint32_t,uint32_t);
extern "C" const cc2x_tile_sites_api* cc2x_query_tile_sites_v1(uint32_t,uint32_t);
extern "C" const cc2x_equipment_api* cc2x_query_equipment_v1(uint32_t,uint32_t);
#include <filesystem>
#include <fstream>
#include <cstdarg>
#include <limits>
#include <vector>
extern "C" const cc2x_carrier_api* cc2x_query_carrier_v1(uint32_t,uint32_t);
extern "C" const cc2x_settings_api* cc2x_query_settings_v1(uint32_t,uint32_t);
extern "C" void cc2x_native_hangar_spawn_result(uint32_t,int,uint32_t,uint32_t){}
namespace {
namespace fs=std::filesystem;
static_assert(CC2X_API_VERSION==8&&CC2X_FRAMEWORK_VERSION==1&&CC2X_MOTION_VERSION==1,
              "navigation is a separate named extension, not an older API version change");
static_assert(offsetof(cc2x_api,lua_add_global)+sizeof(((cc2x_api*)nullptr)->lua_add_global)==sizeof(cc2x_api),
              "API 8 retains its existing final field");
static_assert(offsetof(cc2x_motion_api,drive)==32&&sizeof(cc2x_motion_api)==40,
              "motion API 1 retains its existing x86 layout");
static_assert(CC2X_NAVIGATION_VERSION==1&&CC2X_NAVIGATION_ID==UINT64_C(0x434332584e415631),
              "navigation query identity/version");
static_assert(sizeof(cc2x_navigation_options)==24&&offsetof(cc2x_navigation_options,minimum_depth)==8&&
              offsetof(cc2x_navigation_options,arrival_radius)==16,"navigation options ABI");
static_assert(sizeof(cc2x_navigation_state)==56&&offsetof(cc2x_navigation_state,id)==4&&
              offsetof(cc2x_navigation_state,flags)==8&&offsetof(cc2x_navigation_state,waypoint_id)==12&&
              offsetof(cc2x_navigation_state,target_x)==16&&offsetof(cc2x_navigation_state,target_z)==24&&
              offsetof(cc2x_navigation_state,distance)==32&&offsetof(cc2x_navigation_state,minimum_depth)==40&&
              offsetof(cc2x_navigation_state,arrival_radius)==48,"copied navigation state ABI");
static_assert(sizeof(void*)==4&&sizeof(cc2x_navigation_api)==32&&
              offsetof(cc2x_navigation_api,interface_id)==8&&offsetof(cc2x_navigation_api,available)==16&&
              offsetof(cc2x_navigation_api,read)==20&&offsetof(cc2x_navigation_api,configure)==24,
              "natural x86 named table layout includes trailing alignment padding");
std::wstring package;std::vector<std::string> logs;
const cc2x_navigation_api* navigationCandidate=nullptr;
const cc2x_navigation_api* NavigationCandidate(uint32_t version,uint32_t size){
 assert(version==CC2X_NAVIGATION_VERSION&&size==sizeof(cc2x_navigation_api));return navigationCandidate;
}
void CheckNavigationResolver(const cc2x_navigation_api* navigation){
 assert(navigation&&std::strcmp(CC2X_NAVIGATION_QUERY,"cc2x_query_navigation_v1")==0);
 assert(!cc2x_navigation_resolve(nullptr)&&!cc2x_navigation_from_core(nullptr));
 for(auto version:{0u,2u,UINT32_MAX})assert(!cc2x_query_navigation_v1(version,sizeof(*navigation)));
 for(auto size:{0u,static_cast<uint32_t>(sizeof(*navigation)-1),static_cast<uint32_t>(sizeof(*navigation)+1)})
  assert(!cc2x_query_navigation_v1(1,size));
 auto candidate=*navigation;navigationCandidate=&candidate;
 assert(cc2x_navigation_resolve(NavigationCandidate)==&candidate);
 candidate.version=2;assert(!cc2x_navigation_resolve(NavigationCandidate));candidate=*navigation;
 candidate.size=sizeof(candidate)-1;assert(!cc2x_navigation_resolve(NavigationCandidate));candidate=*navigation;
 candidate.interface_id^=1;assert(!cc2x_navigation_resolve(NavigationCandidate));candidate=*navigation;
 candidate.available=nullptr;assert(!cc2x_navigation_resolve(NavigationCandidate));candidate=*navigation;
 candidate.read=nullptr;assert(!cc2x_navigation_resolve(NavigationCandidate));candidate=*navigation;
 candidate.configure=nullptr;assert(!cc2x_navigation_resolve(NavigationCandidate));
 navigationCandidate=nullptr;
}
void CheckNavigationRefusals(const cc2x_navigation_api* navigation,uint64_t generation){
 assert(navigation&&!navigation->available());
 cc2x_navigation_options options{sizeof(options),CC2X_NAVIGATION_SURFACE,0,25};
 assert(!navigation->configure("native.parity",generation,1,&options));
 assert(!navigation->configure("native.parity",generation,1,nullptr));
 for(const auto* owner:{static_cast<const char*>(nullptr),"","native.","parity","lua.recover.lua"})
  assert(!navigation->configure(owner,generation,1,&options));
 const auto longOwner=std::string("native.")+std::string(97,'x');
 assert(!navigation->configure(longOwner.c_str(),generation,1,&options));
 assert(!navigation->configure("native.parity",0,1,&options));
 assert(!navigation->configure("native.parity",generation+1,1,&options));
 for(auto id:{0u,7u,UINT32_MAX})assert(!navigation->configure("native.parity",generation,id,&options));
 for(auto size:{0u,static_cast<uint32_t>(sizeof(options)-1),static_cast<uint32_t>(sizeof(options)+1)}){
  auto invalid=options;invalid.size=size;assert(!navigation->configure("native.parity",generation,1,&invalid));
 }
 for(auto flags:{2u,UINT32_MAX}){
  auto invalid=options;invalid.flags=flags;assert(!navigation->configure("native.parity",generation,1,&invalid));
 }
 for(double depth:{-1.,1000.001,std::numeric_limits<double>::quiet_NaN(),
                   std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()}){
  auto invalid=options;invalid.minimum_depth=depth;assert(!navigation->configure("native.parity",generation,1,&invalid));
 }
 for(double radius:{0.,.999,150.001,std::numeric_limits<double>::quiet_NaN(),
                    std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()}){
  auto invalid=options;invalid.arrival_radius=radius;assert(!navigation->configure("native.parity",generation,1,&invalid));
 }
 for(auto flags:{0u,static_cast<uint32_t>(CC2X_NAVIGATION_SURFACE)}){
  options.flags=flags;assert(!navigation->configure("native.parity",generation,1,&options));
 }
 cc2x_navigation_state state{sizeof(state)};
 for(auto id:{0u,1u,7u,UINT32_MAX})assert(!navigation->read(id,&state));
 assert(!navigation->read(1,nullptr));
 state.size=sizeof(state)-1;const auto before=state;
 assert(!navigation->read(1,&state)&&std::memcmp(&state,&before,sizeof(state))==0);
}
void Log(const char*,int,const char* format,...){char s[4096]{};va_list args;va_start(args,format);vsnprintf(s,sizeof(s),format,args);va_end(args);logs.emplace_back(s);}
bool Has(const char* text){for(const auto& s:logs)if(s.find(text)!=std::string::npos)return true;return false;}
int Mods(){return 1;}
int Path(int index,wchar_t* out,uint32_t n){if(index||package.size()+1>n)return 0;std::wcscpy(out,package.c_str());return 1;}
void OnTick(void(*)(void*,void*),void*){}
void OnSession(void(*)(void*,int,void*),void*){}
void Write(const fs::path& path,const std::string& text){fs::create_directories(path.parent_path());std::ofstream(path,std::ios::binary)<<text;}
}
int main(){
 const auto* navigation=cc2x_navigation_resolve(cc2x_query_navigation_v1);
 CheckNavigationResolver(navigation);CheckNavigationRefusals(navigation,0);
 auto a=Make();a.size=sizeof(a);a.api_version=CC2X_API_VERSION;a.active_mod_count=Mods;a.active_mod_path=Path;a.log=Log;
 a.on_server_tick=OnTick;a.on_session=OnSession;
 auto root=fs::temp_directory_path()/("cc2x_extensions_"+std::to_string(GetCurrentProcessId()));
 package=(root/"mod").wstring();const auto config=root.string();a.config_dir=config.c_str();
 Write(fs::path(package)/"mod.xml","<mod/>");
 Write(fs::path(package)/"content/bridge.json",R"({"id":"recover.lua","scripts":["bridge/main.lua"]})");
 Write(fs::path(package)/"content/bridge/main.lua",R"(
local cc2=require('cc2')
local n,request=0,nil
return {on_load=function()
 assert(io==nil and os==nil and debug==nil and package==nil)
 assert(type(cc2.carrier.navigation_available)=='function')
 assert(type(cc2.carrier.navigation_state)=='function' and type(cc2.carrier.set_navigation)=='function')
 assert(cc2.carrier.navigation_available()==false)
 assert(select('#',cc2.carrier.navigation_available())==1)
 local function refused(reason,id,options)
  local ok,why=cc2.carrier.set_navigation(id,options)
  assert(ok==false and why==reason,tostring(why))
 end
 local function invalid(id,options) refused('invalid_navigation_arguments',id,options) end
 for _,options in ipairs({{}, {enabled=true}, {enabled=false}, {minimum_depth=0,arrival_radius=1},
                          {minimum_depth=1000,arrival_radius=150,enabled=true}}) do
  refused('navigation_refused',1,options)
 end
 assert(select('#',cc2.carrier.set_navigation(1,{}))==2)
 for _,id in ipairs({'1',true,false,-1,.5,0/0,math.huge,4294967296,{}}) do invalid(id,{}) end
 invalid(nil,{})
 invalid(1,nil);invalid(1,false);invalid(1,0);invalid(1,'options')
 for _,value in ipairs({true,false,'0',-1,1000.001,0/0,math.huge,-math.huge}) do
  invalid(1,{minimum_depth=value})
 end
 for _,value in ipairs({true,false,'25',0,.999,150.001,0/0,math.huge,-math.huge}) do
  invalid(1,{arrival_radius=value})
 end
 for _,value in ipairs({0,1,'true',{}}) do invalid(1,{enabled=value}) end
 invalid(1,{flags=1});invalid(1,{owner='native.forged'});invalid(1,{unknown=true})
 invalid(1,{[1]=true});invalid(1,{[false]=true});invalid(1,{[{}]=true})
 invalid(1,{minimum_depth=0,arrival_radius=25,enabled=true,extra=false})
 invalid(1,setmetatable({},{__index=function() error('navigation must not invoke option metamethods') end}))
 local options={minimum_depth=0,arrival_radius=25,enabled=false}
 refused('navigation_refused',1,options)
 assert(options.minimum_depth==0 and options.arrival_radius==25 and options.enabled==false)
 for _,id in ipairs({0,1,7,'1',true,false,-1,.5,0/0,math.huge,4294967296,{}}) do
  local state,why=cc2.carrier.navigation_state(id)
  assert(state==nil and why=='navigation_state_unavailable')
 end
 local state,why=cc2.carrier.navigation_state(nil)
 assert(state==nil and why=='navigation_state_unavailable')
 assert(select('#',cc2.carrier.navigation_state(1))==2)
 assert(type(cc2.carrier.drive_motion)=='function' and not cc2.carrier.motion_available())
 assert(not cc2.carrier.drive_motion(1,1,0.5))
 assert(not cc2.carrier.drive_motion('1',1,0.5))
 assert(not cc2.carrier.drive_motion(1,1,0/0))
 assert(not cc2.carrier.drive_motion(1,1,'0.5'))
 assert(cc2.tiles.sites(7,0,3)==nil)
 assert(cc2.tiles.sites(7,64,3)==nil)
 assert(cc2.tiles.sites('7',0,3)==nil)
 assert(not cc2.equipment.available())
 assert(not cc2.equipment.set_attachment(7,0,30))
 assert(not cc2.equipment.set_attachment(7,-1,30))
 assert(not cc2.equipment.set_attachment(7,0,102))
 assert(not cc2.equipment.set_attachment(7,0,'30'))
 assert(cc2.carrier.dock_available() and cc2.carrier.recovery_available())
 assert(cc2.settings.get('brake',0.25,0,1)==0.25)
 assert(cc2.settings.set('brake',0.75))
 assert(cc2.settings.get('brake',0,0,0.5)==0.5)
 assert(cc2.settings.set('hold_idle',true) and cc2.settings.get('hold_idle',false))
 assert(cc2.settings.get('../invalid',false)==nil)
 assert(not cc2.settings.set('bad',0/0))
 assert(not cc2.carrier.dock_request('1',7))
 assert(not cc2.carrier.recovery_arm(1,7,{grants=1.5}))
 assert(not cc2.carrier.recovery_disarm(7))
end,on_update=function()
 n=n+1
 if n==3 then
  assert(cc2.carrier.recovery_status(7)==nil)
  local ok,why,id=cc2.carrier.dock_request(1,7);assert(ok and why=='queued' and id)
  request=id;assert(cc2.carrier.dock_result(id).status=='pending')
 elseif n==4 then
  assert(cc2.carrier.dock_result(request).status=='queued')
  assert(cc2.carrier.recovery_arm(1,7,{window_ticks=300}))
  local s=assert(cc2.carrier.recovery_status(7));assert(s.armed and s.remaining_ticks==300)
  assert(not cc2.carrier.recovery_arm(1,7,{}))
 elseif n==5 then
  assert(cc2.carrier.recovery_disarm(7))
  assert(not cc2.carrier.recovery_status(7).armed)
  print('EXTENSIONS_LUA_PASS')
 elseif n==6 then
  assert(cc2.carrier.dock_request(1,7))
  error('intentional failure must cancel the pending engine operation')
 end
end}
)");
 {
 cc2::runtime::Host h(nullptr,Dock);assert(h.Attach(&a,false));h.Session(server,true);h.Tick(server);
 const auto* c=cc2x_carrier_resolve(cc2x_query_carrier_v1);const auto* s=cc2x_settings_resolve(cc2x_query_settings_v1);
 const auto* sites=cc2x_query_tile_sites_v1(1,sizeof(cc2x_tile_sites_api));
 const auto* motion=cc2x_motion_resolve(cc2x_query_motion_v1);
 CheckNavigationRefusals(navigation,h.Generation());
 assert(motion&&!motion->available()&&!motion->drive("native.parity",h.Generation(),1,1,.5));
 assert(!motion->drive("lua.recover.lua",h.Generation(),1,1,.5));
 assert(!cc2x_query_motion_v1(2,sizeof(*motion))&&!cc2x_query_motion_v1(1,sizeof(*motion)-1));
 std::uint32_t siteCount=0;assert(sites&&!sites->read(7,0,3,nullptr,0,&siteCount));
 assert(!cc2x_query_tile_sites_v1(2,sizeof(*sites))&&!cc2x_query_tile_sites_v1(1,sizeof(*sites)-1));
 const auto* e=cc2x_equipment_resolve(cc2x_query_equipment_v1);
 assert(e&&!e->available()&&!e->set_attachment(7,0,30,1));
 assert(!cc2x_query_equipment_v1(2,sizeof(*e))&&!cc2x_query_equipment_v1(1,sizeof(*e)-1));
 assert(c&&s&&!cc2x_query_carrier_v1(2,sizeof(*c))&&!cc2x_query_settings_v1(1,sizeof(*s)-1));
 assert(c->available(CC2X_CARRIER_DOCK)&&c->available(CC2X_CARRIER_RECOVERY));
 double value=0;int boolean=0;
 assert(s->number("native.parity","brake",0.1,0,1,&value)&&value==0.1);
 assert(s->set_number("native.parity","brake",0.4)&&s->number("native.parity","brake",0,0,1,&value)&&value==0.4);
 assert(s->set_boolean("native.parity","hold_idle",1)&&s->boolean("native.parity","hold_idle",0,&boolean)&&boolean);
 assert(!s->set_number("lua.recover.lua","brake",0.1));
 auto token=c->dock_request("native.parity",h.Generation(),1,7);assert(token&&calls==0);
 cc2x_dock_result result{sizeof(result)};assert(c->dock_result("native.parity",token,&result)&&result.code==CC2X_DOCK_PENDING);
 h.Tick(server);assert(calls==1&&c->dock_result("native.parity",token,&result)&&result.code==CC2X_DOCK_ACCEPTED);
 cc2x_recovery_options o{sizeof(o),300,3,30};assert(c->recovery_arm("native.parity",h.Generation(),1,7,&o));
 cc2x_recovery_status status{sizeof(status)};assert(c->recovery_status("native.parity",7,&status)&&status.armed);
 assert(c->recovery_disarm("native.parity",7));dock=0;h.Tick(server);
 assert(calls==1);h.Tick(server);assert(calls==2);
 assert(!c->recovery_status("native.parity",7,&status));
 assert(h.Carrier().Status("lua.recover.lua",7,&status)&&status.armed);
 h.Tick(server);assert(Has("EXTENSIONS_LUA_PASS")&&h.Stats().errors==0);
 dock=0;h.Tick(server);assert(Has("intentional failure"));h.Tick(server);assert(calls==2);
 bool worker=true;std::thread t([&]{
  worker=s->number("native.parity","brake",0,0,1,&value)!=0;
  CheckNavigationRefusals(navigation,h.Generation());
 });t.join();assert(!worker);
 h.Session(server,false);assert(!c->dock_result("native.parity",token,&result));
 CheckNavigationRefusals(navigation,h.Generation());
 assert(!s->number("native.parity","brake",0,0,1,&value));h.Shutdown();
 }
 CheckNavigationRefusals(navigation,0);
 cc2::runtime::ModuleSettings saved(root.wstring(),"lua.recover.lua");std::string why;double value=0;
 assert(saved.Reload(&why)&&saved.Number("brake",&value)&&value==0.75);
 assert(fs::weakly_canonical(root).parent_path()==fs::weakly_canonical(fs::temp_directory_path()));
 fs::remove_all(root);
 std::cout<<"PASS paired C / real Lua carrier commands, additive navigation ABI/refusals, window ownership, settings persistence, error cancellation and old sandbox\n";
}
