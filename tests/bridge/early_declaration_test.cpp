#include "runtime/early_declaration.h"
#include "api/vehicle/definition_registry.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs=std::filesystem;
#define CHECK(x) do {if (!(x)) {std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<"\n";return 1;}} while (0)

void Write(const fs::path& path,const std::string& content) {
  fs::create_directories(path.parent_path());
  std::ofstream(path,std::ios::binary|std::ios::trunc)<<content;
}

int main(int argc,char** argv) {
  wchar_t temp[MAX_PATH]{};
  CHECK(GetTempPathW(MAX_PATH,temp)>0);
  const auto root=fs::path(temp)/(L"cc2x_early_"+std::to_wstring(GetCurrentProcessId())+
      L"_"+std::to_wstring(GetTickCount64()));
  const auto make=[&](const wchar_t* folder,const char* id,const std::string& source) {
    const auto path=root/folder;
    Write(path/L"mod.xml","<mod/>");
    Write(path/L"content/bridge.json",std::string("{\"id\":\"")+id+"\",\"scripts\":[\"bridge/main.lua\"]}");
    Write(path/L"content/bridge/main.lua",source);
    Write(path/L"content/game_objects/aircraft.xml","<definition/>");
    return path;
  };
  const auto good=make(L"good","sample.aircraft",
      "local cc2=require('cc2')\n"
      "local handle=cc2.vehicles.register({id='f16',name='F-16',base='wing_light',definition='game_objects/aircraft.xml'})\n"
      "assert(handle=='sample.aircraft.f16')\nreturn {}\n");
  auto discovery=cc2::host::DiscoverActiveBridgeMods({good.wstring()});
  CHECK(discovery.mods.size()==1);
  auto result=cc2::host::CollectEarlyDeclarations(discovery);
  cc2::host::DeclarationCatalog failure;
  CHECK(result.diagnostics.empty() && result.vehicles.size()==1);
  CHECK(result.vehicles[0].identity==L"sample.aircraft.f16");
  CHECK(result.vehicles[0].resourceName==L"aircraft");
  CHECK(result.vehicles[0].nativeBaseType==8);
  CHECK(result.vehicles[0].definitionId==0);
  cc2x_vehicle_declaration native{};native.size=sizeof(native);native.id="f16";native.name="F-16";
  native.base="wing_light";native.definition="game_objects/aircraft.xml";native.order=UINT32_MAX;
  cc2::host::VehicleDeclaration fromC;std::string nativeError;
  CHECK(cc2::host::BuildVehicleDeclarationFromC(discovery.mods[0],native,&fromC,&nativeError));
  CHECK(fromC.identity==result.vehicles[0].identity&&fromC.resourceName==result.vehicles[0].resourceName&&
    fromC.definitionPath==result.vehicles[0].definitionPath&&fromC.nativeBaseType==result.vehicles[0].nativeBaseType&&fromC.order==result.vehicles[0].order);
  native.base="unsupported";CHECK(!cc2::host::BuildVehicleDeclarationFromC(discovery.mods[0],native,&fromC,&nativeError));native.base="wing_light";
  native.definition="../aircraft.xml";CHECK(!cc2::host::BuildVehicleDeclarationFromC(discovery.mods[0],native,&fromC,&nativeError));native.definition="game_objects/aircraft.xml";
  native.order=65536;CHECK(!cc2::host::BuildVehicleDeclarationFromC(discovery.mods[0],native,&fromC,&nativeError));native.order=UINT32_MAX;
  native.size=0;CHECK(!cc2::host::BuildVehicleDeclarationFromC(discovery.mods[0],native,&fromC,&nativeError));
  const auto ranked=make(L"ranked","sample.ranked",
      "cc2.vehicles.register({id='f16',name='F-16',base='wing_light',definition='game_objects/aircraft_f16.xml',order=0})\n"
      "cc2.vehicles.register({id='f15',name='F-15',base='wing_heavy',definition='game_objects/aircraft_f15.xml',order=1})\n"
      "return {}\n");
  Write(ranked/L"content/game_objects/aircraft_f16.xml","<definition/>\n");
  Write(ranked/L"content/game_objects/aircraft_f15.xml","<definition/>\n");
  result=cc2::host::CollectEarlyDeclarations(cc2::host::DiscoverActiveBridgeMods({ranked.wstring()}));
  CHECK(result.diagnostics.empty() && result.vehicles.size()==2);
  CHECK(result.vehicles[0].localId==L"f16" && result.vehicles[0].order==0);
  CHECK(result.vehicles[1].localId==L"f15" && result.vehicles[1].order==1);
  const auto badOrder=make(L"bad_order","sample.badorder",
      "cc2.vehicles.register({id='jet',name='Jet',base='wing_light',definition='game_objects/aircraft.xml',order=1.5})\n");
  failure=cc2::host::CollectEarlyDeclarations(cc2::host::DiscoverActiveBridgeMods({badOrder.wstring()}));
  CHECK(failure.vehicles.empty() && failure.diagnostics.size()==1);
  const auto blocked=make(L"blocked","sample.blocked",
      "cc2.vehicles.register({id='f15',name='F-15',base='wing_heavy',definition='game_objects/aircraft.xml'})\n"
      "assert(io == nil and os == nil and debug == nil and cc2.world == nil)\n"
      "error('rollback')\n");
  failure=cc2::host::CollectEarlyDeclarations(cc2::host::DiscoverActiveBridgeMods({blocked.wstring()}));
  CHECK(failure.vehicles.empty() && failure.diagnostics.size()==1);
  const auto unsafe=make(L"unsafe","sample.unsafe",
      "cc2.vehicles.register({id='jet',name='Jet',base='wing_light',definition='../outside.xml'})\n");
  failure=cc2::host::CollectEarlyDeclarations(cc2::host::DiscoverActiveBridgeMods({unsafe.wstring()}));
  CHECK(failure.vehicles.empty() && failure.diagnostics.size()==1);
  const auto invalidBase=make(L"base","sample.base",
      "cc2.vehicles.register({id='jet',name='Jet',base='unsupported',definition='game_objects/aircraft.xml'})\n");
  failure=cc2::host::CollectEarlyDeclarations(cc2::host::DiscoverActiveBridgeMods({invalidBase.wstring()}));
  CHECK(failure.vehicles.empty() && failure.diagnostics.size()==1);
  const auto wrongRoot=make(L"wrong_root","sample.wrongroot",
      "cc2.vehicles.register({id='jet',name='Jet',base='wing_light',definition='bridge/descriptor.xml'})\n");
  Write(wrongRoot/L"content/bridge/descriptor.xml","<data/>");
  failure=cc2::host::CollectEarlyDeclarations(cc2::host::DiscoverActiveBridgeMods({wrongRoot.wstring()}));
  CHECK(failure.vehicles.empty() && failure.diagnostics.size()==1);
  if (argc>1) {
    const auto fixture=cc2::host::DiscoverActiveBridgeMods({fs::u8path(argv[1]).wstring()});
    CHECK(fixture.mods.size()==1);
    const auto checked=cc2::host::CollectEarlyDeclarations(fixture);
    CHECK(checked.diagnostics.empty() && checked.vehicles.size()==1);
    CHECK(checked.vehicles[0].resourceName==L"cc2x_third_aircraft");
  }
  if (argc>2) {
    const auto fixture=cc2::host::DiscoverActiveBridgeMods({fs::u8path(argv[2]).wstring()});
    CHECK(fixture.mods.size()==1 && fixture.mods[0].schema==2);
    CHECK(fixture.mods[0].uiImages.size()==2);
    const auto checked=cc2::host::CollectEarlyDeclarations(fixture);
    CHECK(checked.diagnostics.empty() && checked.vehicles.size()==2);
    cc2::api::vehicle::DefinitionRegistry registry;
    std::string registryError;
    CHECK(registry.Publish(checked.vehicles,102,&registryError));
    const auto* f16=registry.Find(L"cc2x_custom_vehicle_type_prototype:f16");
    const auto* f15=registry.Find(L"cc2x_custom_vehicle_type_prototype:f15");
    CHECK(f16 && f15 && f16->descriptor.definitionId==102 && f15->descriptor.definitionId==103);
    CHECK(f16->descriptor.nativeBaseType==8 && f15->descriptor.nativeBaseType==10);
  }
  std::error_code ec;
  fs::remove_all(root,ec);
  CHECK(!ec);
  std::cout<<"PASS early isolated Lua descriptors, deterministic migration order, ownership, path/base validation and transaction rollback\n";
  return 0;
}
