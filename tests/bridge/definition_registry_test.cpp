#include "api/vehicle/definition_registry.h"
#include "api/vehicle/ui_catalog.h"
#include "api/ui/resource_registry.h"
#include <iostream>

#define CHECK(x) do {if (!(x)) {std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<"\n";return 1;}} while (0)

int main() {
  using namespace cc2::api::vehicle;
  cc2::host::VehicleDeclaration light,heavy;
  light.owner=L"sample";light.localId=L"light";light.identity=L"sample.light";
  light.name=L"Light";light.resourceName=L"sample_light";light.nativeBaseType=8;light.ui=L"light";
  heavy.owner=L"sample";heavy.localId=L"heavy";heavy.identity=L"sample.heavy";
  heavy.name=L"Heavy";heavy.resourceName=L"sample_heavy";heavy.nativeBaseType=10;
  DefinitionRegistry registry;
  std::string error;
  CHECK(registry.Publish({heavy,light},104,&error));
  CHECK(registry.Count()==2 && registry.Limit()==106);
  CHECK(registry.Find(104) && registry.Find(105) && !registry.Find(102));
  CHECK(registry.Find(L"sample.heavy")->descriptor.definitionId==104);
  CHECK(registry.Find(L"sample.light")->descriptor.definitionId==105);
  CHECK(registry.Find(L"sample:light")->descriptor.definitionId==105);
  CHECK(!registry.Find(L"sample:light:duplicate"));
  CHECK(registry.MatchesCatalog({light,heavy},&error)); // declaration order is irrelevant
  auto changed=light;
  changed.resourceName=L"renamed_light";
  CHECK(!registry.MatchesCatalog({heavy,changed},&error));
  CHECK(error.find("vehicle declaration changed for identity sample.light")==0);
  CHECK(!registry.MatchesCatalog({light},&error));
  CHECK(error.find("vehicle declaration count changed")==0);
  CHECK(registry.CompatibilityType(104)==104);
  CHECK(!registry.Mark(104,Ready));
  CHECK(registry.Mark(105,NameRegistered));
  CHECK(!registry.Mark(105,XmlParsed));
  CHECK(registry.Mark(105,ArrayInitialized));
  CHECK(registry.Mark(105,XmlParsed));
  CHECK(registry.Mark(105,Ready));
  CHECK(registry.ReadyFor(105) && !registry.ReadyFor(104));
  CHECK(registry.CompatibilityType(104)==104 && registry.CompatibilityType(105)==8);

  // Migration ordering preserves historical IDs without hard-coded type names
  // or numeric IDs in the engine adapter. Unranked entries remain identity-sorted.
  auto migratedLight=light;
  auto migratedHeavy=heavy;
  migratedLight.order=0;
  migratedHeavy.order=1;
  cc2::host::VehicleDeclaration unranked;
  unranked.owner=L"sample";unranked.localId=L"other";unranked.identity=L"sample.other";
  unranked.name=L"Other";unranked.resourceName=L"sample_other";unranked.nativeBaseType=8;
  DefinitionRegistry migration;
  CHECK(migration.Publish({unranked,migratedHeavy,migratedLight},102,&error));
  CHECK(migration.Find(L"sample.light")->descriptor.definitionId==102);
  CHECK(migration.Find(L"sample.heavy")->descriptor.definitionId==103);
  CHECK(migration.Find(L"sample.other")->descriptor.definitionId==104);
  CHECK(migration.MatchesCatalog({migratedLight,unranked,migratedHeavy},&error));
  migratedLight.order=2;
  CHECK(!migration.MatchesCatalog({migratedLight,unranked,migratedHeavy},&error));

  using namespace cc2::api::ui;
  ResourceRegistry resources;
  std::vector<ResourceDescriptor> images;
  const wchar_t* roles[]={L"small",L"large",L"loadout"};
  const std::uint32_t tokens[]={0x40000005u,0x40000006u,0x40000007u};
  for (int i=0;i<3;++i) {
    ResourceDescriptor image;
    image.owner=L"sample";
    image.logicalId=std::wstring(L"vehicles/light/")+roles[i];
    image.sourcePath=L"fixture.png";
    image.virtualPath="textures/cc2x_runtime/surface_0000000"+std::to_string(i+1)+".txtr";
    image.type=ResourceType::TxtrSurface;
    image.generatedPath=L"cache/surface.txtr";
    image.regionName="cx00000"+std::to_string(i+1);
    image.regionToken=tokens[i];
    images.push_back(std::move(image));
  }
  CHECK(resources.Publish(std::move(images),&error));
  CHECK(ResolveUiRegionTokens(registry.Find(105)->descriptor,resources).icon16==0);
  for (int i=0;i<3;++i) {
    const auto logical=std::wstring(L"vehicles/light/")+roles[i];
    CHECK(resources.Mark(L"sample",logical,EngineRequested));
    CHECK(resources.Mark(L"sample",logical,EngineReady));
  }
  const auto tokensReady=ResolveUiRegionTokens(registry.Find(105)->descriptor,resources);
  CHECK(tokensReady.icon16==tokens[0] && tokensReady.icon64==tokens[1] &&
      tokensReady.loadout64==tokens[2]);

  registry.BeginLoad();
  CHECK(!registry.ReadyFor(105));
  CHECK(!registry.Publish({light},102,&error));
  DefinitionRegistry duplicate;
  CHECK(!duplicate.Publish({light,light},102,&error));
  DefinitionRegistry overflow;
  CHECK(!overflow.Publish({light},UINT32_MAX/0x220+1u,&error));
  std::cout<<"PASS order-normalized definition IDs, readiness ordering, class alias gating and duplicate/overflow guards\n";
  return 0;
}
