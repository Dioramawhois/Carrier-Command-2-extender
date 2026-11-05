// SPDX-License-Identifier: GPL-3.0-or-later
// Actual public C exports and Lua facade over the same synthetic engine.
#include "carrier_fixture.h"
#include "runtime/host.h"
#include "cc2x_settings.h"
#include <filesystem>
#include <fstream>
#include <cstdarg>
#include <vector>
extern "C" const cc2x_carrier_api* cc2x_query_carrier_v1(uint32_t,uint32_t);
extern "C" const cc2x_settings_api* cc2x_query_settings_v1(uint32_t,uint32_t);
extern "C" void cc2x_native_hangar_spawn_result(uint32_t,int,uint32_t,uint32_t){}
namespace {
namespace fs=std::filesystem;
std::wstring package;std::vector<std::string> logs;
void Log(const char*,int,const char* format,...){char s[4096]{};va_list args;va_start(args,format);vsnprintf(s,sizeof(s),format,args);va_end(args);logs.emplace_back(s);}
bool Has(const char* text){for(const auto& s:logs)if(s.find(text)!=std::string::npos)return true;return false;}
int Mods(){return 1;}
int Path(int index,wchar_t* out,uint32_t n){if(index||package.size()+1>n)return 0;std::wcscpy(out,package.c_str());return 1;}
void OnTick(void(*)(void*,void*),void*){}
void OnSession(void(*)(void*,int,void*),void*){}
void Write(const fs::path& path,const std::string& text){fs::create_directories(path.parent_path());std::ofstream(path,std::ios::binary)<<text;}
}
int main(){
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
 bool worker=true;std::thread t([&]{worker=s->number("native.parity","brake",0,0,1,&value)!=0;});t.join();assert(!worker);
 h.Session(server,false);assert(!c->dock_result("native.parity",token,&result));
 assert(!s->number("native.parity","brake",0,0,1,&value));h.Shutdown();
 }
 cc2::runtime::ModuleSettings saved(root.wstring(),"lua.recover.lua");std::string why;double value=0;
 assert(saved.Reload(&why)&&saved.Number("brake",&value)&&value==0.75);
 assert(fs::weakly_canonical(root).parent_path()==fs::weakly_canonical(fs::temp_directory_path()));
 fs::remove_all(root);
 std::cout<<"PASS paired C / real Lua carrier commands, window ownership, settings persistence, error cancellation and old sandbox\n";
}
