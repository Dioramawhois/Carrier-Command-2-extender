#include "../src/runtime/mod_loader.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
void Write(const fs::path&p,const std::string&s){fs::create_directories(p.parent_path());std::ofstream(p,std::ios::binary)<<s;}
std::string Manifest(const char*id,const char*entry="main.lua"){return std::string("{\"schema\":1,\"id\":\"")+id+"\",\"name\":\"test\",\"version\":\"1\",\"runtime\":\"bridge\",\"entry\":\""+entry+"\",\"hooks\":[]}";}
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<"\n";return 1;}}while(0)
int main(){
  wchar_t tmp[MAX_PATH];GetTempPathW(MAX_PATH,tmp);auto root=fs::path(tmp)/(L"nb_active_packages_"+std::to_wstring(GetCurrentProcessId()));
  auto local=root/L"mods/A",workshop=root/L"steamapps/workshop/content/1489630/100",inactive=root/L"mods/inactive";
  Write(local/L"mod.xml","<mod/>");Write(local/L"bridge.json",Manifest("local"));Write(local/L"main.lua","return {}\n");
  Write(workshop/L"mod.xml","<mod/>");Write(workshop/L"content/native_bridge/bridge.json",Manifest("workshop"));Write(workshop/L"content/native_bridge/main.lua","return {}\n");
  Write(inactive/L"mod.xml","<mod/>");Write(inactive/L"bridge.json",Manifest("inactive"));Write(inactive/L"main.lua","return {}\n");
  auto result=cc2::host::DiscoverActiveBridgeMods({local.wstring(),workshop.wstring(),local.wstring()});CHECK(result.mods.size()==2);CHECK(result.mods[0].id==L"local");CHECK(fs::equivalent(result.mods[1].rootPath,workshop/L"content/native_bridge"));
  CHECK(cc2::host::DiscoverActiveBridgeMods({}).mods.empty());
  CHECK(cc2::host::DiscoverActiveBridgeMods({local.wstring(),workshop.wstring()}).mods.size()==2);
  auto noxml=root/L"mods/no_mod_xml";Write(noxml/L"bridge.json",Manifest("noxml"));Write(noxml/L"main.lua","return {}\n");CHECK(cc2::host::DiscoverActiveBridgeMods({noxml.wstring()}).mods.empty());
  auto stock=root/L"mods/stock";Write(stock/L"mod.xml","<mod/>");Write(stock/L"content/bridge.json",Manifest("stock","scripts/screen_anything.lua"));Write(stock/L"content/scripts/screen_anything.lua","return {}\n");CHECK(cc2::host::DiscoverActiveBridgeMods({stock.wstring()}).mods.empty());
  auto malformed=root/L"mods/broken";Write(malformed/L"mod.xml","<mod/>");Write(malformed/L"bridge.json","{broken");CHECK(cc2::host::DiscoverActiveBridgeMods({malformed.wstring(),local.wstring()}).mods.size()==1);
  auto traversal=root/L"mods/traversal";Write(traversal/L"mod.xml","<mod/>");Write(traversal/L"bridge.json",Manifest("traversal","../outside.lua"));CHECK(cc2::host::DiscoverActiveBridgeMods({traversal.wstring()}).mods.empty());
  auto duplicate=root/L"mods/duplicate";Write(duplicate/L"mod.xml","<mod/>");Write(duplicate/L"bridge.json",Manifest("LOCAL"));Write(duplicate/L"main.lua","return {}\n");CHECK(cc2::host::DiscoverActiveBridgeMods({duplicate.wstring(),local.wstring()}).mods.empty());
  auto priority=root/L"mods/preferred";Write(priority/L"mod.xml","<mod/>");Write(priority/L"bridge.json",Manifest("root"));Write(priority/L"main.lua","return {}\n");Write(priority/L"content/native_bridge/bridge.json",Manifest("preferred"));Write(priority/L"content/native_bridge/main.lua","return {}\n");auto selected=cc2::host::DiscoverActiveBridgeMods({priority.wstring()});CHECK(selected.mods.size()==1&&selected.mods[0].id==L"preferred");
  auto modern=root/L"mods/modern";
  Write(modern/L"mod.xml","<mod/>");
  Write(modern/L"content/bridge/main.lua","return {}\n");
  Write(modern/L"content/icons/small.png","png fixture");
  Write(modern/L"content/icons/weapon_small.png","png fixture");
  Write(modern/L"content/icons/large.png","png fixture");
  Write(modern/L"content/bridge.json",R"({"id":"example.aircraft","scripts":["bridge/main.lua"],"ui":{"vehicles":{"f16":{"small":"icons/small.png","large":"icons/large.png"}},"weapons":{"missile":{"type":"ATTACHMENT_HARDPOINT_MISSILE_IR","small":"icons/weapon_small.png","large":"icons/large.png"}}}})");
  auto modernResult=cc2::host::DiscoverActiveBridgeMods({modern.wstring()});
  CHECK(modernResult.mods.size()==1 && modernResult.mods[0].schema==2);
  CHECK(modernResult.mods[0].id==L"example.aircraft" && modernResult.mods[0].uiImages.size()==2);
  CHECK(modernResult.mods[0].uiImages[1].weapon &&
        modernResult.mods[0].uiImages[1].engineType==L"attachment_hardpoint_missile_ir");
  CHECK(modernResult.mods[0].uiImages[1].small==L"icons/weapon_small.png" &&
        modernResult.mods[0].uiImages[1].large==L"icons/large.png");
  CHECK(modernResult.mods[0].bridgeScripts.size()==1);
  auto weaponLargeOnly=root/L"mods/weapon_large_only";
  Write(weaponLargeOnly/L"mod.xml","<mod/>");
  Write(weaponLargeOnly/L"content/bridge/main.lua","return {}\n");
  Write(weaponLargeOnly/L"content/icons/large.png","png fixture");
  Write(weaponLargeOnly/L"content/bridge.json",R"({"id":"weapon.large.only","scripts":["bridge/main.lua"],"ui":{"weapons":{"missile":{"type":"attachment_hardpoint_missile_ir","large":"icons/large.png"}}}})");
  const auto largeOnlyResult=cc2::host::DiscoverActiveBridgeMods({weaponLargeOnly.wstring()});
  CHECK(largeOnlyResult.mods.size()==1 && largeOnlyResult.mods[0].uiImages.size()==1 &&
        largeOnlyResult.mods[0].uiImages[0].small.empty());
  const auto migratedLegacy=root/L"mods/legacy_content_manifest";
  Write(migratedLegacy/L"mod.xml","<mod/>");
  Write(migratedLegacy/L"content/bridge/main.lua","return {}\n");
  Write(migratedLegacy/L"content/bridge.json",Manifest("migrated_legacy","content/bridge/main.lua"));
  const auto migrated=cc2::host::DiscoverActiveBridgeMods({migratedLegacy.wstring()});
  CHECK(migrated.mods.size()==1 && migrated.mods[0].schema==1);
  CHECK(fs::equivalent(migrated.mods[0].rootPath,migratedLegacy));
  CHECK(fs::equivalent(migrated.mods[0].entryPath,migratedLegacy/L"content/bridge/main.lua"));
  Write(modern/L"content/native_bridge/bridge.json",Manifest("legacy_shadow"));
  CHECK(cc2::host::DiscoverActiveBridgeMods({modern.wstring()}).mods[0].id==L"example.aircraft");
  auto stockTail=root/L"mods/stock_tail";
  Write(stockTail/L"mod.xml","<mod/>");
  Write(stockTail/L"content/bridge/main.lua","return {}\n");
  Write(stockTail/L"content/scripts/stock.lua","return {}\n");
  Write(stockTail/L"content/bridge.json",R"({"id":"stock.tail","scripts":["bridge/main.lua","scripts/stock.lua"]})");
  CHECK(cc2::host::DiscoverActiveBridgeMods({stockTail.wstring()}).mods.empty());
  auto unsafeUi=root/L"mods/unsafe_ui";
  Write(unsafeUi/L"mod.xml","<mod/>");Write(unsafeUi/L"content/bridge/main.lua","return {}\n");
  Write(unsafeUi/L"content/bridge.json",R"({"id":"unsafe.ui","scripts":["bridge/main.lua"],"ui":{"vehicles":{"f16":{"large":"../outside.png"}}}})");
  CHECK(cc2::host::DiscoverActiveBridgeMods({unsafeUi.wstring()}).mods.empty());
  auto missingWeaponType=root/L"mods/missing_weapon_type";
  Write(missingWeaponType/L"mod.xml","<mod/>");
  Write(missingWeaponType/L"content/bridge/main.lua","return {}\n");
  Write(missingWeaponType/L"content/bridge.json",R"({"id":"missing.type","scripts":["bridge/main.lua"],"ui":{"weapons":{"missile":{"large":"icons/large.png"}}}})");
  Write(missingWeaponType/L"content/icons/large.png","png fixture");
  CHECK(cc2::host::DiscoverActiveBridgeMods({missingWeaponType.wstring()}).mods.empty());
  auto duplicateField=root/L"mods/duplicate_field";
  Write(duplicateField/L"mod.xml","<mod/>");Write(duplicateField/L"content/bridge/main.lua","return {}\n");
  Write(duplicateField/L"content/bridge.json",R"({"id":"first","id":"second","scripts":["bridge/main.lua"]})");
  CHECK(cc2::host::DiscoverActiveBridgeMods({duplicateField.wstring()}).mods.empty());
  const auto bundle=root/L"mods/bundle";
  const auto moduleRoot=bundle/L"content/cc2x-modules";
  Write(bundle/L"mod.xml","<mod/>");
  Write(moduleRoot/L"first/mod.xml","<mod/>");
  Write(moduleRoot/L"first/content/bridge.json",R"({"id":"z.first","scripts":["bridge/main.lua"]})");
  Write(moduleRoot/L"first/content/bridge/main.lua","return {}\n");
  Write(moduleRoot/L"second/mod.xml","<mod/>");
  Write(moduleRoot/L"second/content/bridge.json",R"({"id":"a.second","scripts":["bridge/main.lua"]})");
  Write(moduleRoot/L"second/content/bridge/main.lua","return {}\n");
  Write(moduleRoot/L"index.json",R"({"schema":1,"packages":["first","second"]})");
  auto bundled=cc2::host::DiscoverActiveBridgeMods({bundle.wstring()});
  CHECK(bundled.mods.size()==2 && bundled.mods[0].id==L"z.first" && bundled.mods[1].id==L"a.second");
  CHECK(fs::equivalent(bundled.mods[0].rootPath,moduleRoot/L"first/content"));
  CHECK(fs::equivalent(bundled.mods[1].rootPath,moduleRoot/L"second/content"));
  Write(moduleRoot/L"index.json",R"({"schema":1,"packages":["second","first"]})");
  bundled=cc2::host::DiscoverActiveBridgeMods({bundle.wstring()});
  CHECK(bundled.mods.size()==2 && bundled.mods[0].id==L"a.second");
  for(const auto* invalid:{R"({"schema":1,"packages":["first","../outside"]})",
      R"({"schema":1,"packages":["first","FIRST"]})",
      R"({"schema":1,"packages":["first","missing"]})",
      R"({"schema":1,"packages":["first"],"packages":["second"]})",
      R"({"schema":2,"packages":["first"]})",
      R"({"schema":1,"packages":["first"],"unknown":true})"}) {
    Write(moduleRoot/L"index.json",invalid);
    auto rejected=cc2::host::DiscoverActiveBridgeMods({bundle.wstring(),local.wstring()});
    CHECK(rejected.mods.size()==1 && rejected.mods[0].id==L"local");
    CHECK(!rejected.diagnostics.empty());
  }
  Write(moduleRoot/L"index.json",R"({"schema":1,"packages":["first","second"]})");
  Write(moduleRoot/L"second/content/cc2x-modules/index.json",R"({"schema":1,"packages":[]})");
  CHECK(cc2::host::DiscoverActiveBridgeMods({bundle.wstring()}).mods.empty());
  fs::remove(moduleRoot/L"second/content/cc2x-modules/index.json");
  auto collision=cc2::host::DiscoverActiveBridgeMods({bundle.wstring(),(moduleRoot/L"first").wstring()});
  CHECK(collision.mods.size()==1 && collision.mods[0].id==L"a.second");
  Write(moduleRoot/L"index.json",std::string(65537,' '));
  CHECK(cc2::host::DiscoverActiveBridgeMods({bundle.wstring()}).mods.empty());
  const auto rules=root/L"mods/rules";Write(rules/L"mod.xml","<mod/>");
  Write(rules/L"content/bridge/main.lua","return {}\n");Write(rules/L"content/bridge/rules.lua","return {}\n");
  Write(rules/L"content/bridge.json",R"({"id":"rules.mod","scripts":["bridge/main.lua"],"rules":"bridge/rules.lua"})");
  auto withRules=cc2::host::DiscoverActiveBridgeMods({rules.wstring()});
  CHECK(withRules.mods.size()==1&&withRules.mods[0].rulesEntry==L"bridge/rules.lua");
  for(const auto* invalid:{R"({"id":"rules.mod","scripts":["bridge/main.lua"],"rules":"../escape.lua"})",
      R"({"id":"rules.mod","scripts":["bridge/main.lua"],"rules":["bridge/rules.lua"]})",
      R"({"id":"rules.mod","scripts":["bridge/main.lua"],"rules":"scripts/screen.lua"})"}) {
    Write(rules/L"content/bridge.json",invalid);CHECK(cc2::host::DiscoverActiveBridgeMods({rules.wstring()}).mods.empty());
  }
  std::error_code error;fs::remove_all(root,error);
  std::cout<<"PASS exact CC2X activation snapshot, local/Workshop, disabled exclusion, nested/legacy manifests, isolation, traversal, stock paths, duplicate IDs, deterministic rebuild\n";return 0;
}
