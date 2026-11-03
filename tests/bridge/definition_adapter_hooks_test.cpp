#include "api/vehicle/definition_adapter.h"
#include "api/vehicle/definition_registry.h"
#include <cstdio>
#include <cstring>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;} } while (0)
namespace {
struct Site {std::uint32_t va; std::vector<std::uint8_t> bytes; cc2x_mid_fn fn=nullptr;};
std::vector<Site> sites={
 {0x005A5C0D,{0xFF,0x76,0x04,0x8D,0x4E,0x08}},
 {0x005A5FD5,{0x84,0xC0,0x74,0x14,0x56,0x51}},
 {0x005A5C42,{0x8B,0x4D,0x0C,0x8B,0x55,0x08}},
 {0x00734B60,{0x55,0x8B,0xEC,0x6A,0xFF}},
 {0x00730660,{0x55,0x8B,0xEC,0x6A,0xFF}},
 {0x005A5CE0,{0x55,0x8B,0xEC,0x6A,0xFF}},
 {0x005A2E70,{0x55,0x8B,0xEC,0x6A,0xFF}},
 {0x00853E10,{0x55,0x8B,0xEC,0x51,0x53,0x56}},
};
Site* Find(std::uint32_t va) {for(auto& site:sites)if(site.va==va)return &site;return nullptr;}
std::uintptr_t Va(std::uint32_t va) {
  if(va==0x005A2EA1)return 0x009A2EA1;
  auto* site=Find(va);return site?reinterpret_cast<std::uintptr_t>(site->bytes.data()):0;
}
int patches=0,aborts=0; bool rejectMap=false;
void Begin(const char*) {patches=0;for(auto& site:sites)site.fn=nullptr;}
int Mid(std::uint32_t va,std::uint32_t length,const std::uint8_t* expected,cc2x_mid_fn fn,void*) {
  auto* site=Find(va);
  if(!site || site->bytes.size()!=length || std::memcmp(site->bytes.data(),expected,length)
      || (rejectMap&&va==0x00853E10))return 0;
  ++patches;site->fn=fn;return 1;
}
int Commit() {return 1;}
void Abort() {++aborts;for(auto& site:sites)site.fn=nullptr;}
void Log(const char*,int,const char*,...) {}
struct Frame {cc2x_cpu cpu{};std::uint32_t stack[4]{};};
}
int main() {
  using namespace cc2::api::vehicle;
  cc2x_api api{};api.api_version=8;api.size=sizeof(api);api.module_base=0x00400000;
  api.game_build="1.5.18-win32-r2";api.config_dir="fixture";
  api.va=Va;api.log=Log;api.patch_begin=Begin;api.patch_mid=Mid;
  api.patch_commit=Commit;api.patch_abort=Abort;
  std::vector<cc2::host::VehicleDeclaration> declarations;
  for(int i=0;i<4;++i) {
    cc2::host::VehicleDeclaration d;
    d.owner=L"fixture";d.localId=std::to_wstring(i);d.identity=L"fixture."+d.localId;
    d.resourceName=L"fixture_"+d.localId;d.name=d.identity;d.nativeBaseType=8;
    declarations.push_back(d);
  }
  std::string error;
  CHECK(Registry().Publish(std::move(declarations),102,&error));
  // The production adapter, not a policy-only stand-in, must own these hooks.
  Find(0x00853E10)->bytes[0]=0x90;
  CHECK(!definition_adapter::Install(&api) && patches==0);
  Find(0x00853E10)->bytes[0]=0x55;
  rejectMap=true;
  CHECK(!definition_adapter::Install(&api) && aborts==1);
  rejectMap=false;
  CHECK(definition_adapter::Install(&api));
  CHECK(patches==7 && Find(0x005A2E70)->fn && Find(0x00853E10)->fn);
  Frame frame;
  frame.stack[0]=0x009A2EA0;frame.stack[1]=102;
  Find(0x00853E10)->fn(&frame.cpu,nullptr);
  CHECK(frame.stack[1]==102);
  frame.stack[0]=0x009A2EA1;frame.stack[1]=103;
  Find(0x00853E10)->fn(&frame.cpu,nullptr);
  CHECK(frame.stack[1]==103);
  frame.stack[1]=102;
  Find(0x00853E10)->fn(&frame.cpu,nullptr);
  CHECK(frame.stack[1]==106); // generic catalog size, no aircraft-specific count
  CHECK(Registry().Mark(102,NameRegistered));CHECK(Registry().Mark(102,ArrayInitialized));
  CHECK(Registry().Mark(102,XmlParsed));CHECK(Registry().Mark(102,Ready));
  CHECK(Registry().ReadyFor(102));
  Find(0x005A2E70)->fn(&frame.cpu,nullptr);
  CHECK(!Registry().ReadyFor(102));
  std::puts("PASS production definition loader/name-map hooks, relocation, guards, transaction and generic catalog reset");
}
