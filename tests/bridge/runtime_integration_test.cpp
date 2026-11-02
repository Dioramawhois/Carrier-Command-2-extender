#include "runtime/host.h"
#include "entity_registry.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <cstdarg>
#include <cstring>
#include <limits>
#include <unordered_map>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace fs=std::filesystem;
using cc2::runtime::Host;
namespace {
void(*tick)(void*,void*)=nullptr;void*tickUser=nullptr;
void(*session)(void*,int,void*)=nullptr;void*sessionUser=nullptr;
int tickSubscriptions=0,sessionSubscriptions=0;
std::vector<std::wstring> roots;
std::vector<std::string> logs;
std::unordered_map<std::string,cc2x_lua_fn> stockLuaFunctions;
std::unordered_map<std::string,cc2x_lua_fn> stockLuaGlobals;
std::vector<std::string> stockLuaFeatures;
int lastNativeThrottle=99,lastNativeGroup=-1,lastNativeMultiplier=0;
std::uint32_t lastHangarCarrier=0,lastHangarSlot=0,lastHangarChassis=0;
std::uint32_t lastHangarResultRequest=0,lastHangarResultVehicle=0,lastHangarResultError=0;
int lastHangarResultSuccess=-1;
void(*hangarResultCallback)(std::uint32_t,int,std::uint32_t,std::uint32_t,void*)=nullptr;void*hangarResultUser=nullptr;
int NativeThrottle(int step){lastNativeThrottle=step;return step>=-1&&step<=1;}
int NativeAnimation(std::uint32_t group,std::uint32_t multiplier){lastNativeGroup=static_cast<int>(group);lastNativeMultiplier=static_cast<int>(multiplier);return 1;}
int NativeHangarSpawn(std::uint32_t carrier,std::uint32_t slot,std::uint32_t chassis){lastHangarCarrier=carrier;lastHangarSlot=slot;lastHangarChassis=chassis;return carrier>0?73:0;}
int HangarResultRegister(void(*fn)(std::uint32_t,int,std::uint32_t,std::uint32_t,void*),void*user){hangarResultCallback=fn;hangarResultUser=user;return fn!=nullptr;}
int StockLuaAdd(const char*ns,const char*name,cc2x_lua_fn fn){stockLuaFunctions[std::string(ns?ns:"")+"."+name]=fn;return 1;}
int StockLuaAddGlobal(const char*name,cc2x_lua_fn fn){if(!name||!fn||stockLuaGlobals.count(name))return 0;stockLuaGlobals[name]=fn;return 1;}
void StockLuaFeature(const char*name){stockLuaFeatures.emplace_back(name);}
int LuaToInteger(cc2x_lua*L,int index,std::int64_t*out){int ok=0;auto v=lua_tointegerx(reinterpret_cast<lua_State*>(L),index,&ok);if(ok&&out)*out=v;return ok;}
void LuaPushBoolean(cc2x_lua*L,int value){lua_pushboolean(reinterpret_cast<lua_State*>(L),value);}
void LuaPushInteger(cc2x_lua*L,std::int64_t value){lua_pushinteger(reinterpret_cast<lua_State*>(L),static_cast<lua_Integer>(value));}
void LuaPushString(cc2x_lua*L,const char*value){lua_pushstring(reinterpret_cast<lua_State*>(L),value);}
void LuaPushNil(cc2x_lua*L){lua_pushnil(reinterpret_cast<lua_State*>(L));}
void LuaNewTable(cc2x_lua*L){lua_newtable(reinterpret_cast<lua_State*>(L));}
void Log(const char*,int,const char*format,...){char text[8192];va_list a;va_start(a,format);vsnprintf(text,sizeof(text),format,a);va_end(a);logs.emplace_back(text);}
extern "C" void cc2x_native_hangar_spawn_result(std::uint32_t request,int success,std::uint32_t vehicle,std::uint32_t error){lastHangarResultRequest=request;lastHangarResultSuccess=success;lastHangarResultVehicle=vehicle;lastHangarResultError=error;if(hangarResultCallback)hangarResultCallback(request,success,vehicle,error,hangarResultUser);}
void TickRegister(void(*fn)(void*,void*),void*u){tick=fn;tickUser=u;++tickSubscriptions;}
void SessionRegister(void(*fn)(void*,int,void*),void*u){session=fn;sessionUser=u;++sessionSubscriptions;}
int ModCount(){return static_cast<int>(roots.size());}
int ModPath(int n,wchar_t*out,std::uint32_t size){if(n<0||n>=static_cast<int>(roots.size())||roots[n].size()>=size)return 0;wcscpy_s(out,size,roots[n].c_str());return 1;}
struct MockWaypoint{int type=0,param=0;};
struct MockVehicle{std::uint32_t id=0,definition=0,team=0,attached=0,dock=0,flight=0,mode=0,hp=0;bool teamAi=false,air=false,wing=false,rotor=false,available=true,hasBody=false,taxi=false;double fuel=0,speed=0,power=0,target[3]{};cc2x_body body{};MockWaypoint waypoints[2]{};int waypointCount=0;};
struct MockAirfield{std::uint32_t tile=0,state=0,mode=0;int runwayVehicle=-1;std::uint32_t hangars[4]{};int hangarCount=0;std::uint32_t landing[3]{};int landingCount=0;std::uint32_t takeoff[3]{};int takeoffCount=0;};
struct MockIsland{MockAirfield*airfields[2]{};int count=0;};
struct MockTile{std::uint32_t id=0,team=0;double pos[3]{};int airfields=-1;MockIsland*island=nullptr;};
MockVehicle vehicleA,vehicleB;MockAirfield airfieldA,airfieldB;MockIsland islandA;MockTile tileA,tileB,tileC;
bool emptyWorld=false;
void ResetWorld(){
  vehicleA={};vehicleA.id=7;vehicleA.definition=40;vehicleA.team=1;vehicleA.hp=5000;vehicleA.fuel=0.75;vehicleA.air=true;vehicleA.wing=true;vehicleA.mode=3;vehicleA.speed=80;vehicleA.power=0.6;vehicleA.target[0]=10;vehicleA.target[1]=20;vehicleA.target[2]=30;vehicleA.hasBody=true;vehicleA.body.pos[0]=1;vehicleA.body.pos[1]=2;vehicleA.body.pos[2]=3;vehicleA.body.rot[3]=1;vehicleA.body.vel[0]=4;vehicleA.body.vel[1]=5;vehicleA.body.vel[2]=6;vehicleA.body.spin[0]=0.1;vehicleA.waypointCount=1;vehicleA.waypoints[0]={CC2X_WAYPOINT_MOVE,17};
  vehicleB={};vehicleB.id=9;vehicleB.definition=52;vehicleB.team=2;vehicleB.attached=7;vehicleB.dock=CC2X_DOCK_DOCKED;vehicleB.hp=900;vehicleB.fuel=0.25;vehicleB.rotor=true;vehicleB.hasBody=false;
  airfieldA={};airfieldA.tile=101;airfieldA.state=2;airfieldA.mode=1;airfieldA.runwayVehicle=7;airfieldA.hangars[0]=9;airfieldA.hangars[1]=0;airfieldA.hangarCount=2;airfieldA.landing[0]=9;airfieldA.landingCount=1;airfieldA.takeoff[0]=7;airfieldA.takeoffCount=1;
  airfieldB={};airfieldB.tile=101;airfieldB.state=0;airfieldB.mode=0;airfieldB.runwayVehicle=-1;
  islandA={};islandA.airfields[0]=&airfieldA;islandA.airfields[1]=&airfieldB;islandA.count=2;
  tileA={};tileA.id=101;tileA.team=1;tileA.pos[0]=100;tileA.pos[1]=0;tileA.pos[2]=200;tileA.airfields=2;tileA.island=&islandA;
  tileB={};tileB.id=102;tileB.team=0;tileB.pos[0]=300;tileB.pos[2]=400;tileB.airfields=-1;tileB.island=nullptr;
  tileC={};tileC.id=103;tileC.team=2;tileC.pos[0]=500;tileC.pos[2]=600;tileC.airfields=0;tileC.island=nullptr;
}
int WorldCount(const void*object,int list){
  if(!object&&emptyWorld&&(list==CC2X_WORLD_VEHICLES||list==CC2X_WORLD_TILES))return 0;
  if(!object&&list==CC2X_WORLD_VEHICLES)return (vehicleA.available?1:0)+1;
  if(!object&&list==CC2X_WORLD_TILES)return 3;
  if(object==&vehicleA&&list==CC2X_VEHICLE_WAYPOINTS)return vehicleA.waypointCount;
  if(object==&islandA&&list==CC2X_ISLAND_AIRFIELDS)return islandA.count;
  if(object==&airfieldA&&list==CC2X_AIRFIELD_HANGARS)return airfieldA.hangarCount;
  if(object==&airfieldA&&list==CC2X_AIRFIELD_DOCK_QUEUE)return airfieldA.landingCount;
  if(object==&airfieldA&&list==CC2X_AIRFIELD_UNDOCK_QUEUE)return airfieldA.takeoffCount;
  return 0;
}
void*WorldRef(const void*object,int field,int index){
  if(!object&&emptyWorld&&(field==CC2X_WORLD_VEHICLES||field==CC2X_WORLD_TILES))return nullptr;
  if(!object&&field==CC2X_WORLD_VEHICLES){if(vehicleA.available){if(index==0)return &vehicleA;--index;}return index==0?&vehicleB:nullptr;}
  if(!object&&field==CC2X_WORLD_TILES)return index==0?static_cast<void*>(&tileA):index==1?static_cast<void*>(&tileB):index==2?static_cast<void*>(&tileC):nullptr;
  if(object==&vehicleA&&field==CC2X_VEHICLE_WAYPOINTS)return index>=0&&index<vehicleA.waypointCount?&vehicleA.waypoints[index]:nullptr;
  if(object==&tileA&&field==CC2X_TILE_ISLAND)return &islandA;
  if(object==&islandA&&field==CC2X_ISLAND_AIRFIELDS)return index>=0&&index<islandA.count?islandA.airfields[index]:nullptr;
  return nullptr;
}
void*Find(int list,std::uint32_t id){
  if(emptyWorld)return nullptr;
  if(list==CC2X_WORLD_VEHICLES){if(vehicleA.available&&id==vehicleA.id)return &vehicleA;if(id==vehicleB.id)return &vehicleB;}
  if(list==CC2X_WORLD_TILES){if(id==tileA.id)return &tileA;if(id==tileB.id)return &tileB;if(id==tileC.id)return &tileC;}
  return nullptr;
}
std::int64_t GetI(const void*object,int field,int index){
  if(object==&vehicleA||object==&vehicleB){auto*v=static_cast<const MockVehicle*>(object);switch(field){case CC2X_VEHICLE_ID:return v->id;case CC2X_VEHICLE_DEFINITION:return v->definition;case CC2X_VEHICLE_TEAM:return v->team;case CC2X_VEHICLE_TEAM_IS_AI:return v->teamAi;case CC2X_VEHICLE_IS_AIR:return v->air;case CC2X_VEHICLE_IS_WING:return v->wing;case CC2X_VEHICLE_IS_ROTOR:return v->rotor;case CC2X_VEHICLE_HP:return v->hp;case CC2X_VEHICLE_ATTACHED_TO:return v->attached;case CC2X_VEHICLE_DOCK_STATE:return v->dock;case CC2X_VEHICLE_FLIGHT_STATE:return v->flight;case CC2X_VEHICLE_CONTROL_MODE:return v->mode;case CC2X_VEHICLE_CONTROL_TAXI:return v->taxi;case CC2X_VEHICLE_WAYPOINTS:return v->waypointCount;}}
  if(object==&vehicleA.waypoints[0]||object==&vehicleA.waypoints[1]){auto*w=static_cast<const MockWaypoint*>(object);if(field==CC2X_WAYPOINT_TYPE)return w->type;if(field==CC2X_WAYPOINT_PARAM)return w->param;}
  if(object==&tileA||object==&tileB||object==&tileC){auto*t=static_cast<const MockTile*>(object);if(field==CC2X_TILE_ID)return t->id;if(field==CC2X_TILE_TEAM)return t->team;if(field==CC2X_TILE_AIRFIELDS)return t->airfields;}
  if(object==&airfieldA||object==&airfieldB){auto*af=static_cast<const MockAirfield*>(object);if(field==CC2X_AIRFIELD_TILE)return af->tile;if(field==CC2X_AIRFIELD_STATE)return af->state;if(field==CC2X_AIRFIELD_MODE)return af->mode;if(field==CC2X_AIRFIELD_RUNWAY_VEHICLE)return af->runwayVehicle;if(field==CC2X_AIRFIELD_HANGARS&&index>=0&&index<af->hangarCount)return af->hangars[index];if(field==CC2X_AIRFIELD_DOCK_QUEUE&&index>=0&&index<af->landingCount)return af->landing[index];if(field==CC2X_AIRFIELD_UNDOCK_QUEUE&&index>=0&&index<af->takeoffCount)return af->takeoff[index];}
  return 0;
}
double GetF(const void*object,int field,int index){
  if(object==&vehicleA||object==&vehicleB){auto*v=static_cast<const MockVehicle*>(object);if(field==CC2X_VEHICLE_FUEL)return v->fuel;if(field==CC2X_VEHICLE_CONTROL_SPEED)return v->speed;if(field==CC2X_VEHICLE_CONTROL_POWER)return v->power;if(field==CC2X_VEHICLE_CONTROL_TARGET&&index>=0&&index<3)return v->target[index];if(field==CC2X_VEHICLE_POS&&v->hasBody&&index>=0&&index<3)return v->body.pos[index];}
  if(object==&tileA||object==&tileB||object==&tileC){auto*t=static_cast<const MockTile*>(object);if(field==CC2X_TILE_POS&&index>=0&&index<3)return t->pos[index];}
  return 0;
}
int BodyGet(const void*object,cc2x_body*out){if(!object||!out)return 0;if(object==&vehicleA&&vehicleA.hasBody){*out=vehicleA.body;return 1;}return 0;}
int SnapshotVehicles(cc2x_vehicle_info*out,int max){int n=(vehicleA.available?1:0)+1;if(out&&max>0){int i=0;if(vehicleA.available)out[i++]={vehicleA.id,vehicleA.definition,vehicleA.team,vehicleA.dock,vehicleA.attached,vehicleA.hp,static_cast<float>(vehicleA.fuel),vehicleA.body.pos[0],vehicleA.body.pos[1],vehicleA.body.pos[2]};if(i<max)out[i]={vehicleB.id,vehicleB.definition,vehicleB.team,vehicleB.dock,vehicleB.attached,vehicleB.hp,static_cast<float>(vehicleB.fuel),0,0,0};}return n;}
void Write(const fs::path&p,const std::string&s){fs::create_directories(p.parent_path());std::ofstream(p,std::ios::binary)<<s;}
bool Has(const char*text){for(auto&s:logs)if(s.find(text)!=std::string::npos)return true;return false;}
#define CHECK(x) do {if(!(x)){std::cerr<<"FAIL line "<<__LINE__<<": "<<#x<<"\n";for(auto&s:logs)std::cerr<<s<<"\n";return 1;}}while(0)
}
int main(){
  ResetWorld();
  wchar_t tmp[MAX_PATH];GetTempPathW(MAX_PATH,tmp);
  auto root=fs::path(tmp)/(L"cc2x_bridge_runtime_"+std::to_wstring(GetCurrentProcessId()));
  auto a=root/L"mods"/L"A",b=root/L"mods"/L"B",disabled=root/L"mods"/L"disabled",loadFail=root/L"mods"/L"load_fail";
  const std::string manifest=R"({"schema":1,"id":"test_a","name":"A","version":"1","runtime":"bridge","entry":"main.lua","hooks":[{"target":"bridge.update","mode":"after","handler":"after"}]})";
  Write(a/L"mod.xml","<mod/>");Write(a/L"content/native_bridge/bridge.json",manifest);
  Write(a/L"content/native_bridge/modules/helper.lua","return {value=42}");
  Write(a/L"content/native_bridge/main.lua",R"(
local h=require('modules.helper'); assert(h.value==42);assert(require('cc2')==cc2)
local old, generation, checkedMissingAfterReload=false
return {
 on_load=function()
  local ok,reason=cc2.entities.register('test:drone',{base='vehicle',definition='stock_carrier'})
  assert(ok and reason=='registered')
  assert(cc2.entities.count()==1)
  local types=cc2.entities.types();assert(#types==1 and types[1].id=='test:drone' and types[1].owner=='test_a')
  local duplicate,why=cc2.entities.register('test:drone',{base='vehicle',definition='other'})
  assert(not duplicate and why=='type_id_already_registered')
  print('ENTITY_TYPE_REGISTRY_PASS')
  print('TEST_LOAD_A')
 end,
 on_update=function(dt)
  assert(type(dt)=='number' and dt>=0 and dt<=0.25)
  assert(type(cc2.input.key_down(38))=='boolean')
  local g=cc2.world.generation()
  if old and generation~=g then local x,why=old:geti(256,0);assert(x==nil and why=='stale_or_missing_object');print('STALE_HANDLE_REJECTED') end
  generation=g
  assert(cc2.world.tick()>0)
  if g==3 then
    assert(#cc2.world.vehicles()==0 and #cc2.world.tiles()==0)
    assert(cc2.world.vehicle(7)==nil and cc2.world.tile(101)==nil)
    old=nil;print('EMPTY_WORLD_READ_PASS')
  else
    local live=cc2.world.vehicles()
    if g==2 and not checkedMissingAfterReload then assert(#live==1 and cc2.world.vehicle(7)==nil);checkedMissingAfterReload=true;print('WORLD_ID_RE_RESOLVED_AFTER_RELOAD') end
    local current=cc2.world.vehicle(7)
    if current then assert(current.hp==(g==1 and 5000 or 4200));assert(current.definition==40 and current.team==1 and current.team_is_ai==false)
      assert(current.is_air and current.is_wing and not current.is_rotor);assert(current.has_body and current.position.x==1 and current.position.y==2 and current.position.z==3)
      assert(current.rotation.w==1 and current.velocity.x==4 and current.spin.x==0.1);assert(current.control_target.x==10 and current.control_speed==80 and current.control_power==0.6 and not current.control_taxi)
      assert(#current.waypoints==1 and current.waypoints[1].type==0 and current.waypoints[1].param==17)
      old=cc2.world.find(cc2.world.fields.CC2X_WORLD_VEHICLES,7);assert(old and old:geti(256,0)==7 and old:geti(cc2.world.fields.CC2X_TILE_ID)==nil)
    end
    local noBody=cc2.world.vehicle(9);assert(noBody and not noBody.has_body and noBody.position==nil and noBody.rotation==nil and noBody.velocity==nil and noBody.spin==nil)
    local missing=cc2.world.find(cc2.world.fields.CC2X_WORLD_VEHICLES,999);assert(missing==nil)
    assert(cc2.world.vehicle(999)==nil)
    local bad,badReason=cc2.world.vehicle('9');assert(bad==nil and badReason=='vehicle_id_must_be_uint32')
    assert(#live==(cc2.world.vehicle(7) and 2 or 1));if live[1].id==7 then assert(live[1].position.x==1) else assert(live[1].id==9 and not live[1].has_body) end
    local tiles=cc2.world.tiles();assert(#tiles==3);assert(tiles[1].id==101 and tiles[1].position.x==100 and tiles[1].position.z==200)
    assert(tiles[1].airfields==2 and tiles[1].airfields_known and tiles[1].island_loaded and #tiles[1].airfield_details==2)
    local af=tiles[1].airfield_details[1];assert(af.index==0 and af.tile_id==101 and af.state==2 and af.mode==1 and af.runway_vehicle==7)
    assert(af.hangars[1]==9 and af.hangars[2]==0 and af.landing_queue[1]==9 and af.takeoff_queue[1]==7)
    assert(tiles[2].airfields==-1 and not tiles[2].airfields_known and not tiles[2].island_loaded and tiles[2].airfield_details==nil)
    assert(tiles[3].airfields==0 and tiles[3].airfields_known and not tiles[3].island_loaded)
    assert(cc2.world.tile(102).id==102 and cc2.world.tile(999)==nil)
    for i=1,100 do assert(cc2.world.vehicle(9).id==9) end
  end
  assert(cc2.carrier.throttle(1))
 end,
 after=function() print('AFTER_A') end,
 on_unload=function() print('TEST_UNLOAD_A') end
})");
  Write(b/L"mod.xml","<mod/>");Write(b/L"bridge.json",R"({"schema":1,"id":"test_error","name":"Error","version":"1","runtime":"bridge","entry":"main.lua","hooks":[]})");
  Write(b/L"main.lua",R"(local ok,why=cc2.entities.register('test:drone',{base='vehicle',definition='other'});assert(not ok and why=='type_id_already_registered');return {on_update=function() error('EXPECTED_CALLBACK_ERROR') end})");
  Write(disabled/L"mod.xml","<mod/>");Write(disabled/L"bridge.json",manifest);Write(disabled/L"main.lua","error('DISABLED_MUST_NOT_EXECUTE')");
  Write(loadFail/L"mod.xml","<mod/>");Write(loadFail/L"bridge.json",R"({"schema":1,"id":"test_load_fail","name":"LoadFail","version":"1","runtime":"bridge","entry":"main.lua","hooks":[]})");
  Write(loadFail/L"main.lua",R"(local ok,why=cc2.entities.register('partial:leak',{base='vehicle',definition='candidate'});assert(ok,why);error('EXPECTED_ENTRY_LOAD_ERROR'))");
  roots={a.wstring(),b.wstring(),a.wstring(),loadFail.wstring()};
  cc2x_api api{};api.api_version=CC2X_API_VERSION;api.size=sizeof(api);api.module_base=0x1234;
  api.log=Log;api.on_server_tick=TickRegister;api.on_session=SessionRegister;api.active_mod_count=ModCount;api.active_mod_path=ModPath;
  api.find=Find;api.count=WorldCount;api.ref=WorldRef;api.geti=GetI;api.getf=GetF;api.body_get=BodyGet;api.vehicles=SnapshotVehicles;
  api.lua_add=StockLuaAdd;api.lua_feature=StockLuaFeature;api.lua_tointeger=LuaToInteger;
  api.lua_pushboolean=LuaPushBoolean;api.lua_pushinteger=LuaPushInteger;api.lua_pushstring=LuaPushString;api.lua_pushnil=LuaPushNil;api.lua_newtable=LuaNewTable;
  api.lua_add_global=StockLuaAddGlobal;
  api.carrier_throttle=NativeThrottle;api.animation_speed=NativeAnimation;api.carrier_hangar_spawn=NativeHangarSpawn;api.on_hangar_spawn_result=HangarResultRegister;
  std::string config=root.string();api.config_dir=config.c_str();
  {Host wrong;auto bad=api;bad.api_version=4;CHECK(!wrong.Attach(&bad,false));bad=api;bad.size=offsetof(cc2x_api,active_mod_path);CHECK(!wrong.Attach(&bad,false));bad=api;bad.on_server_tick=nullptr;CHECK(!wrong.Attach(&bad,false));}
  Host host;CHECK(host.Attach(&api,false));CHECK(host.Attach(&api,false));CHECK(tickSubscriptions==1&&sessionSubscriptions==1);
  CHECK(stockLuaFunctions.count("carrier.throttle")==1);
  CHECK(stockLuaFunctions.count(".spawn_in_hangar")==1);
  CHECK(stockLuaFunctions.count("animation.routes")==1&&stockLuaFunctions.count("animation.runway")==1);
  CHECK(stockLuaFunctions.count("animation.lift")==1&&stockLuaFunctions.count("animation.crane")==1);
  CHECK(std::find(stockLuaFeatures.begin(),stockLuaFeatures.end(),"carrier_control")!=stockLuaFeatures.end());
  CHECK(std::find(stockLuaFeatures.begin(),stockLuaFeatures.end(),"carrier_hangar_spawn")!=stockLuaFeatures.end());
  CHECK(std::find(stockLuaFeatures.begin(),stockLuaFeatures.end(),"animation_speed")!=stockLuaFeatures.end());
  {
    lua_State* state=luaL_newstate();CHECK(state!=nullptr);
    lua_pushinteger(state,1);CHECK(stockLuaFunctions["carrier.throttle"](reinterpret_cast<cc2x_lua*>(state))==2);
    CHECK(lua_toboolean(state,-2)&&std::string(lua_tostring(state,-1))=="queued"&&lastNativeThrottle==1);
    lua_settop(state,0);lua_pushinteger(state,7);lua_pushinteger(state,3);lua_pushinteger(state,12);
    CHECK(stockLuaFunctions[".spawn_in_hangar"](reinterpret_cast<cc2x_lua*>(state))==1);
    CHECK(lua_tointeger(state,-1)==73);
    CHECK(lastHangarCarrier==7&&lastHangarSlot==3&&lastHangarChassis==12);
    lua_settop(state,0);lua_pushinteger(state,7);lua_pushinteger(state,-1);lua_pushinteger(state,12);
    CHECK(stockLuaFunctions[".spawn_in_hangar"](reinterpret_cast<cc2x_lua*>(state))==2);
    CHECK(lua_isnil(state,-2)&&std::string(lua_tostring(state,-1)).find("invalid_argument:")==0);
    lua_settop(state,0);lua_pushinteger(state,2);
    CHECK(stockLuaFunctions["carrier.throttle"](reinterpret_cast<cc2x_lua*>(state))==2);
    CHECK(lua_isnil(state,-2)&&std::string(lua_tostring(state,-1))=="expected_step_minus1_zero_or_plus1");
    lua_settop(state,0);lua_pushinteger(state,2);
    CHECK(stockLuaFunctions["animation.routes"](reinterpret_cast<cc2x_lua*>(state))==2);
    CHECK(lua_toboolean(state,-2)&&std::string(lua_tostring(state,-1))=="applied");
    CHECK(lastNativeGroup==CC2X_ANIMATION_ROUTES&&lastNativeMultiplier==2);
    lua_settop(state,0);lua_pushinteger(state,101);
    CHECK(stockLuaFunctions["animation.runway"](reinterpret_cast<cc2x_lua*>(state))==2);
    CHECK(lua_toboolean(state,-2)&&lastNativeMultiplier==101);
    lua_settop(state,0);lua_pushinteger(state,0);
    CHECK(stockLuaFunctions["animation.runway"](reinterpret_cast<cc2x_lua*>(state))==2);
    CHECK(lua_isnil(state,-2)&&std::string(lua_tostring(state,-1))=="multiplier_out_of_range");
    lua_settop(state,0);lua_pushnumber(state,1.5);
    CHECK(stockLuaFunctions["animation.runway"](reinterpret_cast<cc2x_lua*>(state))==2);
    CHECK(lua_isnil(state,-2)&&std::string(lua_tostring(state,-1))=="expected_integer_multiplier");
    lua_settop(state,0);lua_pushnumber(state,std::numeric_limits<double>::quiet_NaN());
    CHECK(stockLuaFunctions["animation.runway"](reinterpret_cast<cc2x_lua*>(state))==2);
    CHECK(lua_isnil(state,-2));
    lua_settop(state,0);lua_pushnumber(state,std::numeric_limits<double>::infinity());
    CHECK(stockLuaFunctions["animation.runway"](reinterpret_cast<cc2x_lua*>(state))==2);
    CHECK(lua_isnil(state,-2));
    lua_settop(state,0);lua_pushinteger(state,4294967295LL);
    CHECK(stockLuaFunctions["animation.runway"](reinterpret_cast<cc2x_lua*>(state))==2);
    CHECK(lua_toboolean(state,-2));
    lua_settop(state,0);lua_pushinteger(state,5);
    CHECK(stockLuaFunctions["animation.crane"](reinterpret_cast<cc2x_lua*>(state))==2);
    CHECK(lua_toboolean(state,-2)&&lastNativeGroup==CC2X_ANIMATION_CRANE&&lastNativeMultiplier==5);
    lua_close(state);
  }
  CHECK(host.Stats().modules==0);CHECK(!host.QueueThrottle(1));
  auto server=reinterpret_cast<void*>(0x12340000);session(server,1,sessionUser);tick(server,tickUser);
  auto m=host.Stats();CHECK(m.modules==2&&m.loads==2&&m.errors>0&&m.requested==1);CHECK(Has("TEST_LOAD_A")&&Has("AFTER_A")&&Has("ENTITY_TYPE_REGISTRY_PASS"));CHECK(Has("EXPECTED_ENTRY_LOAD_ERROR")&&!Has("DISABLED_MUST_NOT_EXECUTE"));CHECK(cc2x_entity_type_count()==1);
  alignas(16) unsigned char carrier[0xD00]{};auto*throttle=reinterpret_cast<float*>(carrier+0xC94);
  host.AfterCarrierUpdate(carrier);CHECK(*throttle==0.025f&&host.Stats().applied==1);
  tick(server,tickUser);auto requests=host.Stats().requested;CHECK(requests==2);
  session(server,0,sessionUser);CHECK(host.Stats().dropped==1);vehicleA.hp=4200;vehicleA.available=false;session(server,1,sessionUser);CHECK(cc2x_entity_type_count()==1);
  host.AfterCarrierUpdate(carrier);CHECK(*throttle==0.025f);tick(server,tickUser);CHECK(host.Stats().loads==2&&Has("STALE_HANDLE_REJECTED")&&Has("WORLD_ID_RE_RESOLVED_AFTER_RELOAD"));
  vehicleA.available=true;tick(server,tickUser);CHECK(Has("WORLD_API_REGISTERED"));
  auto before=host.Stats().updates;std::thread wrong([&]{tick(server,tickUser);});wrong.join();CHECK(host.Stats().updates==before&&host.Stats().wrongThread==1);
  session(server,0,sessionUser);auto noSessionUpdates=host.Stats().updates;tick(server,tickUser);CHECK(host.Stats().updates==noSessionUpdates);
  emptyWorld=true;session(server,1,sessionUser);std::thread migrated([&]{tick(server,tickUser);});migrated.join();emptyWorld=false;CHECK(host.Stats().updates==before+1&&host.Stats().wrongThread==1&&Has("EMPTY_WORLD_READ_PASS"));
  session(server,0,sessionUser);
  for(int i=0;i<50;++i){session(server,0,sessionUser);session(server,1,sessionUser);tick(server,tickUser);}
  CHECK(host.Stats().modules==2&&host.Stats().loads==2&&tickSubscriptions==1);
  for(int i=0;i<10;++i){host.RequestReload();tick(server,tickUser);CHECK(cc2x_entity_type_count()==1);CHECK(lastNativeGroup==CC2X_ANIMATION_CRANE&&lastNativeMultiplier==5);}
  CHECK(host.Stats().loads==22&&host.Stats().unloads==20&&host.Stats().modules==2);
  CHECK(host.Stats().vmGeneration==33);CHECK(Has("TEST_UNLOAD_A"));
  Write(a/L"content/native_bridge/main.lua",R"(return {on_update=function() local n=0;for i=1,100 do if cc2.carrier.throttle(1) then n=n+1 end end;assert(n==64) end,after=function() end})");
  Write(b/L"main.lua","return {on_update=function() error('EXPECTED_CALLBACK_ERROR') end}");
  auto accepted=host.Stats().requested;host.RequestReload();tick(server,tickUser);CHECK(host.Stats().requested==accepted+64);CHECK(cc2x_entity_type_count()==0);host.AfterCarrierUpdate(carrier);CHECK(*throttle==1.0f);
  Write(a/L"content/native_bridge/main.lua",R"(return {on_update=function() while true do end end,after=function() end})");
  host.RequestReload();tick(server,tickUser);CHECK(Has("instruction budget exceeded"));
  CHECK(host.QueueHangarSpawn(7,3,12)==1);session(server,0,sessionUser);
  CHECK(lastHangarResultRequest==1&&lastHangarResultSuccess==0&&lastHangarResultError==CC2X_HANGAR_SPAWN_SESSION_CHANGED);
  Host::HangarSpawnResult staleResult{};
  CHECK(!host.FindHangarSpawnResult(1,&staleResult));
  session(server,1,sessionUser);tick(server,tickUser);
  CHECK(host.QueueHangarSpawn(7,4,12)==2);session(server,0,sessionUser);
  CHECK(lastHangarResultRequest==2&&lastHangarResultSuccess==0&&lastHangarResultError==CC2X_HANGAR_SPAWN_SESSION_CHANGED);
  host.Shutdown();CHECK(host.Stats().modules==0&&host.Stats().unloads==26);CHECK(cc2x_entity_type_count()==0);
  tick(server,tickUser);session(server,0,sessionUser);CHECK(host.Stats().modules==0);
  // The same native callback subscription survives removal of every module.
  { Host removal;CHECK(removal.Attach(&api,false));removal.Session(server,true);removal.Tick(server);roots.clear();removal.RequestReload();removal.Tick(server);CHECK(removal.Stats().modules==0&&removal.Stats().unloads==2);removal.Shutdown(); }
  {
    Host api5Prefix;
    auto oldApi=api;oldApi.api_version=5;oldApi.size=static_cast<std::uint32_t>(offsetof(cc2x_api,carrier_throttle));
    const auto registrationsBefore=stockLuaFunctions.size();
    CHECK(api5Prefix.Attach(&oldApi,false));
    CHECK(stockLuaFunctions.size()==registrationsBefore);
    api5Prefix.Shutdown();
  }
  std::error_code error;fs::remove_all(root,error);
  std::cout<<"PASS real Lua + mock CC2X: registration/delegation, API 5 prefix compatibility, callbacks, World Read API, session re-resolution, thread rejection, 50 sessions, shutdown; API bytes="<<sizeof(api)<<"\n";
  return 0;
}
