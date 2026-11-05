// SPDX-License-Identifier: GPL-3.0-or-later
// Own engine-call substitutes exercise the production CPU preview pipeline.
// No game DLL, function or third-party script is executed.
#include "../../src/api/vehicle/tile_sites.cpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <limits>
extern "C" void cc2x_native_hangar_spawn_result(std::uint32_t,int,std::uint32_t,std::uint32_t){}
namespace {
unsigned allocated=0,destroyed=0,mode=0;
std::vector<unsigned char> records;
void* __cdecl Allocate(std::size_t n){++allocated;auto* p=std::calloc(1,n);assert(p);return p;}
void* __thiscall Tile(void* p,std::uint32_t id,const double* matrix){
 assert(id==7&&matrix[9]==123&&matrix[11]==789);return p;
}
void* __thiscall Task(void* p,void** tile,std::uint32_t seed,const double* bounds,void* definitions,std::uint32_t biome,float difficulty){
 assert(*tile&&seed==42&&bounds[0]==-3000&&definitions==reinterpret_cast<void*>(123)&&biome==2&&difficulty==.5f);
 std::memcpy(static_cast<unsigned char*>(p)+0xd4,tile,4);*tile=nullptr;return p;
}
void __thiscall Run(void* p){
 auto* tile=*reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(p)+0xd4);
 const unsigned n=mode==1?65:3;records.assign(n*0x70,0);
 for(unsigned i=0;i<n;++i){auto* row=records.data()+i*0x70;
  const std::uint32_t kind=mode==1||i==1?0:1,definition=3;
  std::memcpy(row,&kind,4);std::memcpy(row+4,&definition,4);
  double matrix[12]{1,0,0,0,1,0,0,0,1,321,2,987};
  if(mode==2&&i==1)matrix[9]=std::numeric_limits<double>::quiet_NaN();
  std::memcpy(row+8,matrix,sizeof(matrix));
 }
 const auto ptr=reinterpret_cast<std::uintptr_t>(records.data());std::memcpy(tile+0x1dc,&ptr,4);
 std::memcpy(tile+0x1e0,&n,4);std::memcpy(tile+0x1e8,&n,4);
}
void __thiscall Destroy(void* p,unsigned deleting){
 assert(deleting==1);auto* tile=*reinterpret_cast<void**>(static_cast<unsigned char*>(p)+0xd4);
 std::free(tile);std::free(p);destroyed+=2;
}
std::uintptr_t Address(std::uint32_t address){
 switch(address){case 0xc3f9fd:return reinterpret_cast<std::uintptr_t>(Allocate);
 case 0x625fd0:return reinterpret_cast<std::uintptr_t>(Tile);case 0x626310:return reinterpret_cast<std::uintptr_t>(Task);
 case 0x626420:return reinterpret_cast<std::uintptr_t>(Run);case 0x626f50:return reinterpret_cast<std::uintptr_t>(Destroy);}
 assert(false);return 0;
}
}
int main(){
 cc2x_api a{};a.va=Address;
 for(mode=0;mode<3;++mode){
  cc2::runtime::PreviewCall call{};call.api=&a;call.tile=7;call.seed=42;call.biome=2;call.difficulty=.5f;
  call.bounds[0]=-3000;call.matrix[9]=123;call.matrix[11]=789;call.definitions=reinterpret_cast<void*>(123);call.kind=0;call.definition=3;
  cc2::runtime::Generate(&call);
  assert(allocated==destroyed);
  if(mode==0)assert(call.valid&&call.count==1&&call.sites[0].transform[9]==321&&call.sites[0].transform[11]==987);
  else assert(!call.valid);
 }
 assert(cc2::api::world::TileSiteSelector(0,3)&&!cc2::api::world::TileSiteSelector(64,3));
 std::cout<<"PASS production preview call order, input forwarding, moved ownership, exact site selector, bounds/NaN refusal and native cleanup (owned substitutes)\n";
}
