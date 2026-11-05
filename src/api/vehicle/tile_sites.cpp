// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only geometry preview using the engine's isolated CPU terrain task.
#include "runtime/host.h"
#include "api/carrier/hangar_spawn.h"
#include "api/carrier/pe_relocation_guard.h"
#include "platform/seh_guard.h"
#include <Windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace cc2::runtime {
namespace {
bool Region(const void* p,std::size_t n){
 MEMORY_BASIC_INFORMATION m{};
 if(!p||!n||!VirtualQuery(p,&m,sizeof(m))||m.State!=MEM_COMMIT||(m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
 const auto a=reinterpret_cast<std::uintptr_t>(p),b=reinterpret_cast<std::uintptr_t>(m.BaseAddress);
 return a>=b&&a-b<=m.RegionSize&&n<=m.RegionSize-(a-b);
}
template<class T>T At(const void* p,std::size_t o){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+o,sizeof(v));return v;}
bool Entries(const cc2x_api* api){
 struct Guard{std::uint32_t address;std::array<BYTE,16> bytes;std::uint32_t relocation;};
 constexpr Guard guards[]{
  {0x625fd0,{0x53,0x8b,0xdc,0x83,0xec,0x08,0x83,0xe4,0xf8,0x83,0xc4,0x04,0x55,0x8b,0x6b,0x04},0},
  {0x626310,{0x55,0x8b,0xec,0x6a,0xff,0x68,0xb4,0x57,0xca,0x00,0x64,0xa1,0,0,0,0},0xca57b4},
  {0x626420,{0x55,0x8b,0xec,0x6a,0xff,0x68,0x38,0x58,0xca,0x00,0x64,0xa1,0,0,0,0},0xca5838},
  {0x626f50,{0x55,0x8b,0xec,0x6a,0xff,0x68,0x44,0x59,0xca,0x00,0x64,0xa1,0,0,0,0},0xca5944},
  {0xc3f9fd,{0x55,0x8b,0xec,0xeb,0x0d,0xff,0x75,0x08,0xe8,0xda,0xf6,0x01,0x00,0x59,0x85,0xc0},0},
 };
 for(const auto& g:guards){const pe_guard::RelocationField field{6,g.relocation};
  if(!pe_guard::MatchLoadedBytes(reinterpret_cast<const BYTE*>(api->module_base),0xaf7000,g.address-0x400000,
    g.bytes.data(),g.bytes.size(),g.relocation?&field:nullptr,g.relocation?1:0,0x400000))return false;
 }
 return true;
}
struct PreviewCall{
 const cc2x_api* api;void* definitions;std::uint32_t tile,seed,biome,kind,definition;
 float difficulty;double bounds[6],matrix[12];
 std::array<api::world::TileSite,64> sites{};unsigned count=0;bool valid=false;
};
void Generate(void* data){
 auto& c=*static_cast<PreviewCall*>(data);
 using Allocate=void*(__cdecl*)(std::size_t);
 using ConstructTile=void*(__thiscall*)(void*,std::uint32_t,const double*);
 using ConstructTask=void*(__thiscall*)(void*,void**,std::uint32_t,const double*,void*,std::uint32_t,float);
 using Run=void(__thiscall*)(void*);using Destroy=void(__thiscall*)(void*,unsigned);
 auto address=[&](std::uint32_t va){return reinterpret_cast<void*>(c.api->va(va));};
 auto allocate=reinterpret_cast<Allocate>(address(0xc3f9fd));
 void* tile=allocate(0x330);
 reinterpret_cast<ConstructTile>(address(0x625fd0))(tile,c.tile,c.matrix);
 void* task=allocate(0xd8);
 reinterpret_cast<ConstructTask>(address(0x626310))(task,&tile,c.seed,c.bounds,c.definitions,c.biome,c.difficulty);
 *reinterpret_cast<std::uint32_t*>(static_cast<unsigned char*>(task)+4)=1; // same queued task state as the native producer
 reinterpret_cast<Run>(address(0x626420))(task);
 void* result=At<void*>(task,0xd4);
 if(Region(result,0x330)){
  const auto* r=static_cast<const unsigned char*>(result);
  const auto records=At<const unsigned char*>(r,0x1dc);
  const auto capacity=At<std::uint32_t>(r,0x1e0),first=At<std::uint32_t>(r,0x1e4),count=At<std::uint32_t>(r,0x1e8);
  c.valid=count==0||(capacity&&capacity<=65536&&count<=capacity&&first<capacity&&Region(records,capacity*0x70));
  for(std::uint32_t i=0;i<count&&c.valid;++i){
   const auto* p=records+((first+i)%capacity)*0x70;
   if(At<std::uint32_t>(p,0)!=c.kind||At<std::uint32_t>(p,4)!=c.definition)continue;
   if(c.count==c.sites.size()){c.valid=false;break;}
   auto& site=c.sites[c.count++];std::memcpy(site.transform.data(),p+8,96);site.flags_low=At<std::uint32_t>(p,0x68);site.flags_high=At<std::uint32_t>(p,0x6c);
   for(const auto number:site.transform)if(!std::isfinite(number)||std::abs(number)>2000000000)c.valid=false;
  }
 }
 // The task owns its private generated tile, allocations and mutexes. Its
 // stock deleting destructor releases them. Nothing entered the world lists.
 reinterpret_cast<Destroy>(address(0x626f50))(task,1);
}
}
bool Host::ReadTileSites(std::uint32_t id,std::uint32_t kind,std::uint32_t definition,
    std::vector<api::world::TileSite>* output,std::string* reason){
 std::lock_guard<std::recursive_mutex> lock(mutex_);
 if(!output||!reason)return false;output->clear();
 auto refuse=[&](const char* why){*reason=why;return false;};
 if(!InServerContext()||!id||!api::world::TileSiteSelector(kind,definition))return refuse("invalid_tile_sites_context_or_selector");
 if(tileSitesGeneration_!=Generation()){
  tileSitesGeneration_=Generation();tileSites_.clear();tileSitesFault_=false;tileSitesBuildTick_=UINT64_MAX;
  tileSitesEnabled_=api_.game_build&&std::strcmp(api_.game_build,"1.5.18-win32-r2")==0&&ValidateHangarSpawnImage(&api_,reason)&&Entries(&api_);
 }
 if(!tileSitesEnabled_||tileSitesFault_)return refuse("tile_sites_unavailable");
 const auto key=(static_cast<std::uint64_t>(id)<<12)|(kind<<6)|definition;
 auto cached=tileSites_.find(key);if(cached!=tileSites_.end()){*output=cached->second;*reason="cached";return true;}
 if(tileSites_.size()>=128)return refuse("tile_sites_cache_full");
 if(tileSitesBuildTick_==SessionTickId())return refuse("tile_sites_tick_budget");
 if(!Entries(&api_))return refuse("tile_sites_native_entry_changed");
 void* tile=api_.find?api_.find(CC2X_WORLD_TILES,id):nullptr;
 if(!Region(tile,0x110)||At<std::uint32_t>(tile,0)!=id)return refuse("tile_not_found");
 PreviewCall c{};c.api=&api_;c.tile=id;c.kind=kind;c.definition=definition;
 c.seed=At<std::uint32_t>(tile,8);c.biome=At<std::uint32_t>(tile,0x10);c.difficulty=At<float>(tile,0xc);
 if(c.biome>=8||!std::isfinite(c.difficulty)||c.difficulty<0||c.difficulty>1)return refuse("unsupported_tile_generation_inputs");
 std::memcpy(c.bounds,static_cast<const unsigned char*>(tile)+0x18,sizeof(c.bounds));
 for(double n:c.bounds)if(!std::isfinite(n)||std::abs(n)>32768)return refuse("tile_bounds_invalid");
 if(c.bounds[3]-c.bounds[0]<128||c.bounds[3]-c.bounds[0]>16384||c.bounds[5]-c.bounds[2]<128||c.bounds[5]-c.bounds[2]>16384)return refuse("tile_extent_invalid");
 c.matrix[0]=c.matrix[4]=c.matrix[8]=1;
 std::memcpy(c.matrix+9,static_cast<const unsigned char*>(tile)+0x48,24);
 c.definitions=static_cast<unsigned char*>(server_)+0x1bc;
 if(!Region(c.definitions,0x30)||!Region(At<void*>(c.definitions,0x24),8*0xec))return refuse("tile_definitions_unavailable");
 const auto beforeIsland=At<void*>(tile,0x100),beforeRender=At<void*>(tile,0xf0);
 const auto started=GetTickCount64();tileSitesBuildTick_=SessionTickId();
 if(!cc2x_seh_guard(Generate,&c)){tileSitesFault_=true;return refuse("tile_sites_native_fault_no_retry");}
 if(beforeIsland!=At<void*>(tile,0x100)||beforeRender!=At<void*>(tile,0xf0)){tileSitesFault_=true;return refuse("tile_world_changed_during_preview");}
 if(!c.valid)return refuse("tile_sites_result_invalid");
 output->assign(c.sites.begin(),c.sites.begin()+c.count);tileSites_[key]=*output;
 char detail[160]{};std::snprintf(detail,sizeof(detail),"tile=%u kind=%u definition=%u sites=%u elapsed_ms=%llu island_unchanged=1",id,kind,definition,c.count,GetTickCount64()-started);
 Log("TILE_SITES_PREVIEW","host",detail);*reason="generated";return true;
}
}
