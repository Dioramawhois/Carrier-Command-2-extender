#include "host.h"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
#include <cmath>
#include <climits>
#include <chrono>
#include <vector>
#include <cstring>
namespace cc2::runtime {
namespace {
constexpr char Meta[]="cc2.world.selector";
struct Selector {
  Host* host;unsigned long long generation,tick;
  int list;std::uint32_t id;unsigned depth;
  struct Step {int field,index;} path[8];
};
int Failure(lua_State*L,const char*reason){lua_pushnil(L);lua_pushstring(L,reason);return 2;}
Host* Owner(lua_State*L){return static_cast<Host*>(lua_touserdata(L,lua_upvalueindex(1)));}
void* Resolve(Selector*s) {
  if(!s||!s->host->InLuaContext()||s->generation!=s->host->Generation()||
     (s->depth&&s->tick!=s->host->TickId()))return nullptr;
  const auto*a=s->host->Api();if(!a->find)return nullptr;
  void*p=a->find(s->list,s->id);
  for(unsigned i=0;p&&i<s->depth;++i){if(!a->ref)return nullptr;p=a->ref(p,s->path[i].field,s->path[i].index);}
  return p;
}
Selector* Arg(lua_State*L){return static_cast<Selector*>(luaL_testudata(L,1,Meta));}
int Domain(const Selector*s){if(!s)return 0;if(!s->depth)return s->list==CC2X_WORLD_VEHICLES?0x100:0x200;switch(s->path[s->depth-1].field){case CC2X_TILE_ISLAND:return 0x280;case CC2X_ISLAND_AIRFIELDS:return 0x300;case CC2X_VEHICLE_WAYPOINTS:return 0x180;default:return 0;}}
bool FieldAllowed(const Selector*s,int field){int d=Domain(s);return d&&field>=d&&field<(d==0x300?0x400:d+0x80);}
void* ResolveTyped(Selector*s,int domain){return Domain(s)==domain?Resolve(s):nullptr;}
void Push(lua_State*L,const Selector&s){auto*out=static_cast<Selector*>(lua_newuserdata(L,sizeof(s)));*out=s;luaL_getmetatable(L,Meta);lua_setmetatable(L,-2);}
bool Integer(lua_State*L,int index,int*out){int ok=0;auto v=lua_tointegerx(L,index,&ok);if(!ok||v<INT_MIN||v>INT_MAX)return false;*out=static_cast<int>(v);return true;}
int Find(lua_State*L) {
  auto*h=Owner(L);int list=0,ok=0;auto id=lua_tointegerx(L,2,&ok);
  if(!h->InLuaContext())return Failure(L,"wrong_execution_context");
  if(!Integer(L,1,&list)||!ok||id<0||id>UINT32_MAX||(list!=CC2X_WORLD_VEHICLES&&list!=CC2X_WORLD_TILES))return Failure(L,"invalid_id_or_list");
  Selector s{h,h->Generation(),h->TickId(),list,static_cast<std::uint32_t>(id),0,{}};
  if(!Resolve(&s))return Failure(L,"object_or_capability_unavailable");Push(L,s);return 1;
}
int Generation(lua_State*L){lua_pushinteger(L,Owner(L)->Generation());return 1;}
int Read(lua_State*L) {
  auto*s=Arg(L);void*p=Resolve(s);if(!p)return Failure(L,"stale_or_missing_object");
  int field,index=0;if(!Integer(L,2,&field)||(lua_gettop(L)>2&&!Integer(L,3,&index))||index<0||!FieldAllowed(s,field))return Failure(L,"invalid_field_or_index");
  const auto*a=s->host->Api();int op=static_cast<int>(lua_tointeger(L,lua_upvalueindex(1)));
  if(op==0&&a->geti){lua_pushinteger(L,a->geti(p,field,index));return 1;}
  if(op==1&&a->getf){lua_pushnumber(L,a->getf(p,field,index));return 1;}
  if(op==2&&a->count){lua_pushinteger(L,a->count(p,field));return 1;}
  if(op==3&&a->ref&&s->depth<8) {
    if(!((Domain(s)==0x100&&field==CC2X_VEHICLE_WAYPOINTS)||(Domain(s)==0x200&&field==CC2X_TILE_ISLAND)||(Domain(s)==0x280&&field==CC2X_ISLAND_AIRFIELDS)))return Failure(L,"unsupported_reference_type");
    Selector child=*s;child.path[child.depth++]={field,index};child.tick=s->host->TickId();
    if(!Resolve(&child))return Failure(L,"missing_reference");Push(L,child);return 1;
  }
  return Failure(L,"capability_unavailable");
}
int Write(lua_State*L) {
  auto*s=Arg(L);void*p=Resolve(s);if(!p)return Failure(L,"stale_or_missing_object");
  const auto*a=s->host->Api();int op=static_cast<int>(lua_tointeger(L,lua_upvalueindex(1))),ok=0,result=0;
  int field,index=0;if(!Integer(L,2,&field)||(op<3&&!Integer(L,3,&index))||index<0||!FieldAllowed(s,field))return Failure(L,"invalid_field_or_index");
  if(op==0&&a->seti){auto v=lua_tointegerx(L,4,&ok);if(!ok)return Failure(L,"integer_expected");result=a->seti(p,field,index,v);}
  else if(op==1&&a->setf){auto v=lua_tonumberx(L,4,&ok);if(!ok||!std::isfinite(v))return Failure(L,"finite_number_expected");result=a->setf(p,field,index,v);}
  else if(op==2&&a->list_remove)result=a->list_remove(p,field,index);
  else if(op==3&&a->list_push){auto v=lua_tointegerx(L,3,&ok);if(!ok)return Failure(L,"integer_expected");result=a->list_push(p,field,v);}
  else if(op==4&&a->list_clear)result=a->list_clear(p,field);
  else return Failure(L,"capability_unavailable");
  lua_pushboolean(L,result);return 1;
}
void Number(lua_State*L,const char*name,lua_Number n){lua_pushnumber(L,n);lua_setfield(L,-2,name);}
class QueryMetric {
 public:
  QueryMetric(Host*h,const char*operation):host_(h),operation_(operation),start_(Clock::now()){}
  ~QueryMetric(){if(host_){auto us=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start_).count();host_->RecordWorldRead(operation_,error_,us<0?0:static_cast<unsigned long long>(us),objects_,success_,wrongThread_);}}
  void Complete(unsigned long long objects){success_=true;objects_=objects;}
  void Fail(const char*reason){error_=reason;}
  void WrongThread(){wrongThread_=true;}
 private:
  using Clock=std::chrono::steady_clock;
  Host*host_;const char*operation_;const char*error_=nullptr;Clock::time_point start_;unsigned long long objects_=0;bool success_=false,wrongThread_=false;
};
void IntField(lua_State*L,const char*name,long long value){lua_pushinteger(L,static_cast<lua_Integer>(value));lua_setfield(L,-2,name);}
void BoolField(lua_State*L,const char*name,bool value){lua_pushboolean(L,value?1:0);lua_setfield(L,-2,name);}
void Vec3Field(lua_State*L,const char*name,const double*v){lua_newtable(L);Number(L,"x",v[0]);Number(L,"y",v[1]);Number(L,"z",v[2]);lua_setfield(L,-2,name);}
void QuatField(lua_State*L,const char*name,const double*v){lua_newtable(L);Number(L,"x",v[0]);Number(L,"y",v[1]);Number(L,"z",v[2]);Number(L,"w",v[3]);lua_setfield(L,-2,name);}
void IndexedVec3Field(lua_State*L,const char*name,const double*v){lua_newtable(L);for(int i=0;i<3;++i){lua_pushnumber(L,v[i]);lua_rawseti(L,-2,i+1);}lua_setfield(L,-2,name);}
bool ReadContext(lua_State*L,Host*h,QueryMetric&q){if(h->InLuaContext())return true;if(!h->OnRuntimeThread())q.WrongThread();q.Fail("wrong_execution_context_or_no_session");lua_pushnil(L);lua_pushliteral(L,"wrong_execution_context_or_no_session");return false;}
int QueryFailure(lua_State*L,QueryMetric&q,const char*reason){q.Fail(reason);return Failure(L,reason);}
bool ValidId(lua_State*L,int index,std::uint32_t*out){if(lua_type(L,index)!=LUA_TNUMBER)return false;int ok=0;auto value=lua_tointegerx(L,index,&ok);if(!ok||value<0||static_cast<unsigned long long>(value)>UINT32_MAX)return false;*out=static_cast<std::uint32_t>(value);return true;}
bool LiveCore(const cc2x_api*a){return a&&a->find&&a->count&&a->ref&&a->geti&&a->getf;}
bool PushIdList(lua_State*L,const cc2x_api*a,const void*object,int field,int limit){int n=a->count(object,field);if(n<0||n>limit)return false;lua_newtable(L);for(int i=0;i<n;++i){lua_pushinteger(L,static_cast<lua_Integer>(a->geti(object,field,i)));lua_rawseti(L,-2,i+1);}return true;}
bool PushAirfield(lua_State*L,const cc2x_api*a,const void*af,int index){
  lua_newtable(L);IntField(L,"index",index);IntField(L,"tile_id",a->geti(af,CC2X_AIRFIELD_TILE,0));
  IntField(L,"state",a->geti(af,CC2X_AIRFIELD_STATE,0));IntField(L,"mode",a->geti(af,CC2X_AIRFIELD_MODE,0));
  IntField(L,"runway_vehicle",a->geti(af,CC2X_AIRFIELD_RUNWAY_VEHICLE,0));
  if(!PushIdList(L,a,af,CC2X_AIRFIELD_HANGARS,64)){lua_pop(L,1);return false;}lua_setfield(L,-2,"hangars");
  if(!PushIdList(L,a,af,CC2X_AIRFIELD_DOCK_QUEUE,4096)){lua_pop(L,1);return false;}lua_setfield(L,-2,"landing_queue");
  if(!PushIdList(L,a,af,CC2X_AIRFIELD_UNDOCK_QUEUE,4096)){lua_pop(L,1);return false;}lua_setfield(L,-2,"takeoff_queue");
  if(a->airfield_bay){lua_newtable(L);int out=1;for(int slot=0;slot<4;++slot){double pos[3],yaw=0;if(!a->airfield_bay(af,slot,pos,&yaw))continue;lua_newtable(L);IntField(L,"slot",slot);Vec3Field(L,"position",pos);Number(L,"yaw",yaw);lua_rawseti(L,-2,out++);}lua_setfield(L,-2,"bays");}
  return true;
}
bool PushVehicle(lua_State*L,const cc2x_api*a,const void*v){
  lua_newtable(L);
  IntField(L,"id",a->geti(v,CC2X_VEHICLE_ID,0));IntField(L,"definition",a->geti(v,CC2X_VEHICLE_DEFINITION,0));
  IntField(L,"team",a->geti(v,CC2X_VEHICLE_TEAM,0));BoolField(L,"team_is_ai",a->geti(v,CC2X_VEHICLE_TEAM_IS_AI,0)!=0);
  Number(L,"hp",static_cast<double>(a->geti(v,CC2X_VEHICLE_HP,0)));Number(L,"fuel",a->getf(v,CC2X_VEHICLE_FUEL,0));
  BoolField(L,"is_air",a->geti(v,CC2X_VEHICLE_IS_AIR,0)!=0);BoolField(L,"is_wing",a->geti(v,CC2X_VEHICLE_IS_WING,0)!=0);BoolField(L,"is_rotor",a->geti(v,CC2X_VEHICLE_IS_ROTOR,0)!=0);
  IntField(L,"attached_to",a->geti(v,CC2X_VEHICLE_ATTACHED_TO,0));IntField(L,"dock_state",a->geti(v,CC2X_VEHICLE_DOCK_STATE,0));
  IntField(L,"flight_state",a->geti(v,CC2X_VEHICLE_FLIGHT_STATE,0));IntField(L,"control_mode",a->geti(v,CC2X_VEHICLE_CONTROL_MODE,0));
  double target[3];for(int i=0;i<3;++i)target[i]=a->getf(v,CC2X_VEHICLE_CONTROL_TARGET,i);Vec3Field(L,"control_target",target);
  Number(L,"control_speed",a->getf(v,CC2X_VEHICLE_CONTROL_SPEED,0));Number(L,"control_power",a->getf(v,CC2X_VEHICLE_CONTROL_POWER,0));
  BoolField(L,"control_taxi",a->geti(v,CC2X_VEHICLE_CONTROL_TAXI,0)!=0);
  cc2x_body body{};bool hasBody=a->body_get&&a->body_get(v,&body);BoolField(L,"has_body",hasBody);
  if(hasBody){Vec3Field(L,"position",body.pos);QuatField(L,"rotation",body.rot);Vec3Field(L,"velocity",body.vel);Vec3Field(L,"spin",body.spin);Number(L,"x",body.pos[0]);Number(L,"y",body.pos[1]);Number(L,"z",body.pos[2]);}
  int n=a->count(v,CC2X_VEHICLE_WAYPOINTS);if(n<0||n>1024){lua_pop(L,1);return false;}lua_newtable(L);
  for(int i=0;i<n;++i){const void*wp=a->ref(v,CC2X_VEHICLE_WAYPOINTS,i);if(!wp){lua_pop(L,2);return false;}lua_newtable(L);IntField(L,"type",a->geti(wp,CC2X_WAYPOINT_TYPE,0));IntField(L,"param",a->geti(wp,CC2X_WAYPOINT_PARAM,0));lua_rawseti(L,-2,i+1);}
  lua_setfield(L,-2,"waypoints");return true;
}
bool PushTile(lua_State*L,const cc2x_api*a,const void*t){
  lua_newtable(L);IntField(L,"id",a->geti(t,CC2X_TILE_ID,0));IntField(L,"team",a->geti(t,CC2X_TILE_TEAM,0));
  double pos[3];for(int i=0;i<3;++i)pos[i]=a->getf(t,CC2X_TILE_POS,i);Vec3Field(L,"position",pos);Number(L,"x",pos[0]);Number(L,"z",pos[2]);
  const long long airfields=static_cast<long long>(a->geti(t,CC2X_TILE_AIRFIELDS,0));IntField(L,"airfields",airfields);BoolField(L,"airfields_known",airfields>=0);
  const void*island=a->ref(t,CC2X_TILE_ISLAND,0);BoolField(L,"island_loaded",island!=nullptr);
  if(island){int n=a->count(island,CC2X_ISLAND_AIRFIELDS);if(n<0||n>256){lua_pop(L,1);return false;}lua_newtable(L);for(int i=0;i<n;++i){const void*af=a->ref(island,CC2X_ISLAND_AIRFIELDS,i);if(!af||!PushAirfield(L,a,af,i)){lua_pop(L,2);return false;}lua_rawseti(L,-2,i+1);}lua_setfield(L,-2,"airfield_details");}
  return true;
}
int Vehicles(lua_State*L){
  auto*h=Owner(L);QueryMetric metric(h,"vehicles");if(!ReadContext(L,h,metric))return 2;const auto*a=h->Api();
  if(lua_gettop(L)!=0)return QueryFailure(L,metric,"no_arguments_expected");if(!LiveCore(a))return QueryFailure(L,metric,"world_api_unavailable");
  int n=a->count(nullptr,CC2X_WORLD_VEHICLES);if(n<0||n>4096)return QueryFailure(L,metric,"vehicle_count_unavailable_or_limit_exceeded");lua_newtable(L);
  for(int i=0;i<n;++i){const void*v=a->ref(nullptr,CC2X_WORLD_VEHICLES,i);if(!v||!PushVehicle(L,a,v)){lua_settop(L,0);return QueryFailure(L,metric,"vehicle_or_waypoint_unavailable_during_read");}lua_rawseti(L,-2,i+1);}
  metric.Complete(static_cast<unsigned long long>(n));return 1;
}
int Vehicle(lua_State*L){
  auto*h=Owner(L);QueryMetric metric(h,"vehicle");if(!ReadContext(L,h,metric))return 2;const auto*a=h->Api();std::uint32_t id=0;
  if(lua_gettop(L)!=1||!ValidId(L,1,&id))return QueryFailure(L,metric,"vehicle_id_must_be_uint32");if(!LiveCore(a))return QueryFailure(L,metric,"world_api_unavailable");
  const void*v=a->find(CC2X_WORLD_VEHICLES,id);if(!v){metric.Complete(0);lua_pushnil(L);return 1;}
  if(!PushVehicle(L,a,v))return QueryFailure(L,metric,"vehicle_or_waypoint_unavailable_during_read");metric.Complete(1);return 1;
}
int Tiles(lua_State*L){
  auto*h=Owner(L);QueryMetric metric(h,"tiles");if(!ReadContext(L,h,metric))return 2;const auto*a=h->Api();
  if(lua_gettop(L)!=0)return QueryFailure(L,metric,"no_arguments_expected");if(!LiveCore(a))return QueryFailure(L,metric,"world_api_unavailable");
  int n=a->count(nullptr,CC2X_WORLD_TILES);if(n<0||n>4096)return QueryFailure(L,metric,"tile_count_unavailable_or_limit_exceeded");lua_newtable(L);
  for(int i=0;i<n;++i){const void*t=a->ref(nullptr,CC2X_WORLD_TILES,i);if(!t||!PushTile(L,a,t)){lua_settop(L,0);return QueryFailure(L,metric,"tile_or_airfield_unavailable_during_read");}lua_rawseti(L,-2,i+1);}
  metric.Complete(static_cast<unsigned long long>(n));return 1;
}
int Tile(lua_State*L){
  auto*h=Owner(L);QueryMetric metric(h,"tile");if(!ReadContext(L,h,metric))return 2;const auto*a=h->Api();std::uint32_t id=0;
  if(lua_gettop(L)!=1||!ValidId(L,1,&id))return QueryFailure(L,metric,"tile_id_must_be_uint32");if(!LiveCore(a))return QueryFailure(L,metric,"world_api_unavailable");
  const void*t=a->find(CC2X_WORLD_TILES,id);if(!t){metric.Complete(0);lua_pushnil(L);return 1;}
  if(!PushTile(L,a,t))return QueryFailure(L,metric,"tile_or_airfield_unavailable_during_read");metric.Complete(1);return 1;
}
int WorldTick(lua_State*L){auto*h=Owner(L);if(lua_gettop(L)!=0)return Failure(L,"no_arguments_expected");lua_pushinteger(L,h->InLuaContext()?static_cast<lua_Integer>(h->SessionTickId()):0);return 1;}
int Body(lua_State*L) {
  auto*s=Arg(L);void*p=Resolve(s);if(!p||Domain(s)!=0x100)return Failure(L,"stale_missing_or_wrong_object");const auto*a=s->host->Api();
  cc2x_body body{};if(!a->body_get||!a->body_get(p,&body))return Failure(L,"body_unavailable");
  lua_newtable(L);const char*names[]={"pos","rot","vel","spin"};const double*values[]={body.pos,body.rot,body.vel,body.spin};
  for(int k=0;k<4;++k){lua_newtable(L);for(int i=0;i<(k==1?4:3);++i){lua_pushnumber(L,values[k][i]);lua_rawseti(L,-2,i+1);}lua_setfield(L,-2,names[k]);}return 1;
}
bool Vector(lua_State*L,int at,double*out,int count){if(!lua_istable(L,at))return false;at=lua_absindex(L,at);for(int i=0;i<count;++i){lua_rawgeti(L,at,i+1);int ok=0;out[i]=lua_tonumberx(L,-1,&ok);lua_pop(L,1);if(!ok||!std::isfinite(out[i]))return false;}return true;}
void PushVector(lua_State*L,const double*v,int count){lua_newtable(L);for(int i=0;i<count;++i){lua_pushnumber(L,v[i]);lua_rawseti(L,-2,i+1);}}
int BodySet(lua_State*L){auto*s=Arg(L);void*p=ResolveTyped(s,0x100);if(!p)return Failure(L,"stale_or_missing_object");auto*a=s->host->Api();if(!a->body_set)return Failure(L,"capability_unavailable");int mask=CC2X_BODY_ALL;if(!lua_istable(L,2)||(lua_gettop(L)>2&&!Integer(L,3,&mask))||mask<1||mask>15)return Failure(L,"invalid_body_or_mask");cc2x_body b{};const char*names[]={"pos","rot","vel","spin"};double*values[]={b.pos,b.rot,b.vel,b.spin};for(int i=0;i<4;++i)if(mask&(1<<i)){lua_getfield(L,2,names[i]);bool ok=Vector(L,-1,values[i],i==1?4:3);lua_pop(L,1);if(!ok)return Failure(L,"invalid_body_vector");}lua_pushboolean(L,a->body_set(p,&b,mask));return 1;}
int Landing(lua_State*L){auto*s=Arg(L);auto*v=static_cast<Selector*>(luaL_testudata(L,2,Meta));void*island=ResolveTyped(s,0x280);void*vehicle=ResolveTyped(v,0x100);if(!island||!vehicle||s->host!=v->host)return Failure(L,"stale_or_missing_object");auto*a=s->host->Api();if(!a->request_landing)return Failure(L,"capability_unavailable");lua_pushboolean(L,a->request_landing(island,vehicle));return 1;}
int Bay(lua_State*L){auto*s=Arg(L);void*p=ResolveTyped(s,0x300);if(!p)return Failure(L,"stale_or_missing_object");auto*a=s->host->Api();int slot=0;if(!Integer(L,2,&slot)||slot<0||slot>3)return Failure(L,"invalid_slot");double pos[3],yaw=0;if(!a->airfield_bay||!a->airfield_bay(p,slot,pos,&yaw))return Failure(L,"bay_unavailable");PushVector(L,pos,3);lua_pushnumber(L,yaw);return 2;}
int Transform(lua_State*L){auto*s=Arg(L);void*p=ResolveTyped(s,0x300);if(!p)return Failure(L,"stale_or_missing_object");auto*a=s->host->Api();double local[3],world[3]{};if(!Vector(L,2,local,3))return Failure(L,"vector_expected");if(!a->airfield_to_world)return Failure(L,"capability_unavailable");a->airfield_to_world(p,local,world);PushVector(L,world,3);return 1;}
int TileAt(lua_State*L){auto*h=Owner(L);auto*a=h->Api();int xok=0,zok=0;double x=lua_tonumberx(L,1,&xok),z=lua_tonumberx(L,2,&zok);if(!h->InLuaContext()||!a->tile_at||!a->geti)return Failure(L,"capability_or_context_unavailable");if(!xok||!zok||!std::isfinite(x)||!std::isfinite(z))return Failure(L,"finite_numbers_expected");void*p=a->tile_at(x,z);if(!p)return Failure(L,"tile_unavailable");Selector s{h,h->Generation(),h->TickId(),CC2X_WORLD_TILES,static_cast<std::uint32_t>(a->geti(p,CC2X_TILE_ID,0)),0,{}};Push(L,s);return 1;}
int Raycast(lua_State*L){auto*h=Owner(L);auto*a=h->Api();if(!h->InLuaContext()||!a->raycast)return Failure(L,"capability_or_context_unavailable");double from[3],dir[3];int ok=0,flags=3;double distance=lua_tonumberx(L,3,&ok);if(!Vector(L,1,from,3)||!Vector(L,2,dir,3)||!ok||!std::isfinite(distance)||distance<=0||(lua_gettop(L)>3&&!Integer(L,4,&flags))||flags<1||flags>3)return Failure(L,"invalid_raycast_arguments");double norm=dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2];if(std::fabs(norm-1)>0.001)return Failure(L,"unit_direction_expected");cc2x_hit hit{};if(!a->raycast(from,dir,distance,flags,&hit))return Failure(L,"no_hit");lua_newtable(L);PushVector(L,hit.pos,3);lua_setfield(L,-2,"pos");PushVector(L,hit.normal,3);lua_setfield(L,-2,"normal");Number(L,"distance",hit.distance);return 1;}
int Count(lua_State*L){auto*h=Owner(L);int list=0;if(!h->InLuaContext()||!h->Api()->count||!Integer(L,1,&list))return Failure(L,"capability_or_context_unavailable");lua_pushinteger(L,h->Api()->count(nullptr,list));return 1;}
int VehicleCommand(lua_State*L){
  if(Domain(Arg(L))!=0x100)return Failure(L,"vehicle_expected");
  auto*s=Arg(L);void*p=Resolve(s);if(!p)return Failure(L,"stale_or_missing_object");auto*a=s->host->Api();int op=static_cast<int>(lua_tointeger(L,lua_upvalueindex(1)));
  if(op==0&&a->vehicle_gear)a->vehicle_gear(p,lua_toboolean(L,2));
  else if(op==1&&a->waypoints_sync)a->waypoints_sync(p);
  else if(op==2&&a->vehicle_brakes){int x=0,y=0;double brake=lua_tonumberx(L,2,&x),grip=lua_tonumberx(L,3,&y);if(!x||!y||!std::isfinite(brake)||!std::isfinite(grip))return Failure(L,"finite_numbers_expected");a->vehicle_brakes(p,static_cast<float>(brake),static_cast<float>(grip));}
  else return Failure(L,"capability_unavailable");lua_pushboolean(L,1);return 1;
}
int Throttle(lua_State* L) {
  auto* s=Arg(L);void* vehicle=ResolveTyped(s,0x100);
  if(!vehicle)return Failure(L,"stale_missing_or_wrong_vehicle");
  if(lua_gettop(L)==1) {
    double value=0;if(!s->host->ReadVehicleThrottle(s->id,&value))return Failure(L,"unsupported_vehicle_or_profile");
    lua_pushnumber(L,value);return 1;
  }
  int ok=0;double value=lua_tonumberx(L,2,&ok);
  if(lua_gettop(L)!=2||lua_type(L,2)!=LUA_TNUMBER||!ok||!std::isfinite(value)||value<0||value>1)return Failure(L,"expected_throttle_0_to_1");
  if(!s->host->WriteVehicleThrottle(s->id,value))return Failure(L,"unsupported_vehicle_or_profile");
  lua_pushboolean(L,1);return 1;
}
int Buttons(lua_State* L) {
  auto* h=Owner(L);std::uint32_t id=0;auto* context=h->CurrentButtons();
  if(lua_gettop(L)!=1||!ValidId(L,1,&id))return Failure(L,"expected_vehicle_id");
  if(!context||context->vehicleId!=id)return Failure(L,"vehicle_controls_callback_required");
  lua_createtable(L,static_cast<int>(context->buttons.size()),0);int i=1;
  for(const auto& b:context->buttons){lua_createtable(L,0,4);
    lua_pushlstring(L,b.name.data(),b.name.size());lua_setfield(L,-2,"name");
    lua_pushinteger(L,b.index);lua_setfield(L,-2,"index");
    lua_pushboolean(L,b.on);lua_setfield(L,-2,"on");
    lua_pushboolean(L,b.enabled);lua_setfield(L,-2,"enabled");lua_rawseti(L,-2,i++);}
  return 1;
}
int SetButtons(lua_State* L) {
  auto* h=Owner(L);std::uint32_t id=0;auto* context=h->CurrentButtons();
  if(lua_gettop(L)!=2||!ValidId(L,1,&id)||!lua_istable(L,2))return Failure(L,"expected_vehicle_id_and_button_values");
  if(!context||context->vehicleId!=id)return Failure(L,"vehicle_controls_callback_required");
  std::map<std::string,bool> values;lua_pushnil(L);
  while(lua_next(L,2)){
    std::size_t size=0;const char* name=lua_type(L,-2)==LUA_TSTRING?lua_tolstring(L,-2,&size):nullptr;
    if(!name||!size||size>127||std::memchr(name,0,size)||!lua_isboolean(L,-1)||values.size()>=64){lua_pop(L,2);return Failure(L,"expected_up_to_64_named_boolean_states");}
    values.emplace(std::string(name,size),lua_toboolean(L,-1)!=0);lua_pop(L,1);
  }
  if(!api::vehicle::StageButtons(context,values))return Failure(L,"unusable_ambiguous_or_conflicting_button_request");
  lua_pushboolean(L,1);lua_pushliteral(L,"staged");return 2;
}
}
void RegisterWorld(lua_State*L,Host*host) {
  luaL_newmetatable(L,Meta);lua_pushvalue(L,-1);lua_setfield(L,-2,"__index");
  const char*reads[]={"geti","getf","count","ref"};for(int i=0;i<4;++i){lua_pushinteger(L,i);lua_pushcclosure(L,Read,1);lua_setfield(L,-2,reads[i]);}
  const char*writes[]={"seti","setf","list_remove","list_push","list_clear"};for(int i=0;i<5;++i){lua_pushinteger(L,i);lua_pushcclosure(L,Write,1);lua_setfield(L,-2,writes[i]);}
  const char*commands[]={"gear","waypoints_sync","brakes"};for(int i=0;i<3;++i){lua_pushinteger(L,i);lua_pushcclosure(L,VehicleCommand,1);lua_setfield(L,-2,commands[i]);}
  lua_pushcfunction(L,Body);lua_setfield(L,-2,"body");lua_pop(L,1);
  luaL_getmetatable(L,Meta);
  lua_pushcfunction(L,Throttle);lua_setfield(L,-2,"throttle");
  const luaL_Reg methods[]={{"body_set",BodySet},{"request_landing",Landing},{"bay",Bay},{"to_world",Transform},{nullptr,nullptr}};luaL_setfuncs(L,methods,0);lua_pop(L,1);
  lua_newtable(L);
  auto add=[&](const char*name,lua_CFunction fn){lua_pushlightuserdata(L,host);lua_pushcclosure(L,fn,1);lua_setfield(L,-2,name);};
  add("find",Find);add("generation",Generation);add("tick",WorldTick);add("count",Count);add("tile_at",TileAt);add("raycast",Raycast);
  add("vehicles",Vehicles);add("vehicle",Vehicle);add("tiles",Tiles);add("tile",Tile);
  add("buttons",Buttons);add("set_buttons",SetButtons);
  lua_newtable(L);
#define CC2_WORLD_CONSTANT(name) lua_pushinteger(L,name);lua_setfield(L,-2,#name);
#include "world_constants.inc"
#undef CC2_WORLD_CONSTANT
  lua_setfield(L,-2,"fields");lua_setglobal(L,"cc2_bridge_world");
}
}
