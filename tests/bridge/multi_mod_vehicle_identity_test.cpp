#include "api/ui/resource_registry.h"
#include "api/vehicle/definition_registry.h"
#include "api/vehicle/ui_catalog.h"
#include "runtime/early_declaration.h"

#include <Windows.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " << #x << '\n'; return 1; } } while (0)

void Write(const fs::path& path, const std::string& contents) {
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

fs::path MakeMod(const fs::path& root, const wchar_t* folder, const char* id,
                 const char* display, const char* definition) {
  const auto path = root / folder;
  Write(path / L"mod.xml", "<mod/>");
  Write(path / L"content/bridge.json",
      std::string("{\"id\":\"") + id +
      "\",\"scripts\":[\"bridge/main.lua\"],\"ui\":{\"vehicles\":{\"fighter\":{\"large\":\"icons/fighter.png\"}}}}");
  Write(path / L"content/bridge/main.lua",
      std::string("local identity=cc2.vehicles.register({id='fighter',name='") + display +
      "',base='wing_light',definition='game_objects/" + definition +
      "',ui='fighter'})\nassert(identity=='" + id + ".fighter')\nreturn {}\n");
  Write(path / (L"content/game_objects/" + std::wstring(definition, definition + std::strlen(definition))),
      "<definition/>");
  Write(path / L"content/icons/fighter.png", "test image bytes; manifest path validation only");
  return path;
}

void Ready(cc2::api::vehicle::DefinitionRegistry& registry, std::uint32_t id) {
  registry.Mark(id, cc2::api::vehicle::NameRegistered);
  registry.Mark(id, cc2::api::vehicle::ArrayInitialized);
  registry.Mark(id, cc2::api::vehicle::XmlParsed);
  registry.Mark(id, cc2::api::vehicle::Ready);
}

int main(int argc, char** argv) {
  wchar_t temp[MAX_PATH]{};
  CHECK(GetTempPathW(MAX_PATH, temp) > 0);
  const auto root = fs::path(temp) / (L"cc2x_multi_mod_" +
      std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64()));

  const auto alpha = MakeMod(root, L"alpha", "sample.alpha", "Alpha fighter", "alpha_fighter.xml");
  const auto beta = MakeMod(root, L"beta", "sample.beta", "Beta fighter", "beta_fighter.xml");
  // Deliberately reverse activation order: discovery must normalize the order.
  auto discovery = cc2::host::DiscoverActiveBridgeMods({beta.wstring(), alpha.wstring()});
  CHECK(discovery.mods.size() == 2 && discovery.diagnostics.empty());
  const auto catalog = cc2::host::CollectEarlyDeclarations(discovery);
  CHECK(catalog.diagnostics.empty() && catalog.vehicles.size() == 2);
  CHECK(catalog.vehicles[0].identity == L"sample.alpha.fighter");
  CHECK(catalog.vehicles[1].identity == L"sample.beta.fighter");
  CHECK(catalog.vehicles[0].localId == catalog.vehicles[1].localId);
  CHECK(catalog.vehicles[0].ui == catalog.vehicles[1].ui);
  CHECK(catalog.vehicles[0].resourceName != catalog.vehicles[1].resourceName);

  cc2::api::vehicle::DefinitionRegistry definitions;
  std::string error;
  auto reverseDeclarationOrder = catalog.vehicles;
  std::reverse(reverseDeclarationOrder.begin(), reverseDeclarationOrder.end());
  CHECK(definitions.Publish(std::move(reverseDeclarationOrder), 102, &error));
  const auto* alphaDefinition = definitions.Find(L"sample.alpha:fighter");
  const auto* betaDefinition = definitions.Find(L"sample.beta:fighter");
  CHECK(alphaDefinition && betaDefinition && alphaDefinition != betaDefinition);
  CHECK(alphaDefinition->descriptor.definitionId == 102);
  CHECK(betaDefinition->descriptor.definitionId == 103);
  Ready(definitions, 102);
  Ready(definitions, 103);

  cc2::api::ui::ResourceRegistry resources;
  std::vector<cc2::api::ui::ResourceDescriptor> images;
  const cc2::api::vehicle::DefinitionRecord* vehicleDefinitions[] = {
      alphaDefinition, betaDefinition};
  for (std::size_t i = 0; i < std::size(vehicleDefinitions); ++i) {
    cc2::api::ui::ResourceDescriptor image;
    image.owner = vehicleDefinitions[i]->descriptor.owner;
    image.logicalId = L"vehicles/fighter/large";
    image.sourcePath = (i == 0 ? alpha : beta).wstring() + L"\\content\\icons\\fighter.png";
    image.virtualPath = i == 0 ? "textures/cc2x_runtime/surface_alpha.txtr"
                               : "textures/cc2x_runtime/surface_beta.txtr";
    image.type = cc2::api::ui::ResourceType::TxtrSurface;
    image.generatedPath = i == 0 ? L"cache/alpha.txtr" : L"cache/beta.txtr";
    image.regionName = i == 0 ? "cx000005" : "cx000006";
    image.regionToken = i == 0 ? 0x40000005u : 0x40000006u;
    images.push_back(std::move(image));
  }
  CHECK(resources.Publish(std::move(images), &error));
  for (const auto* definition : vehicleDefinitions) {
    CHECK(resources.Mark(definition->descriptor.owner, L"vehicles/fighter/large",
        cc2::api::ui::EngineRequested));
    CHECK(resources.Mark(definition->descriptor.owner, L"vehicles/fighter/large",
        cc2::api::ui::EngineReady));
  }
  const auto alphaUi = cc2::api::vehicle::ResolveUiRegionTokens(
      alphaDefinition->descriptor, resources);
  const auto betaUi = cc2::api::vehicle::ResolveUiRegionTokens(
      betaDefinition->descriptor, resources);
  CHECK(alphaUi.icon64 == 0x40000005u && betaUi.icon64 == 0x40000006u);
  CHECK(alphaUi.icon64 != betaUi.icon64);

  if (argc == 3) {
    const auto alphaFixture = fs::u8path(argv[1]).wstring();
    const auto betaFixture = fs::u8path(argv[2]).wstring();
    const auto fixtureDiscovery = cc2::host::DiscoverActiveBridgeMods(
        {betaFixture, alphaFixture});
    CHECK(fixtureDiscovery.mods.size() == 2 && fixtureDiscovery.diagnostics.empty());
    const auto fixtureCatalog = cc2::host::CollectEarlyDeclarations(fixtureDiscovery);
    CHECK(fixtureCatalog.diagnostics.empty() && fixtureCatalog.vehicles.size() == 2);
    CHECK(fixtureCatalog.vehicles[0].identity == L"cc2x.identity.alpha.fighter");
    CHECK(fixtureCatalog.vehicles[1].identity == L"cc2x.identity.beta.fighter");
    CHECK(fixtureCatalog.vehicles[0].resourceName != fixtureCatalog.vehicles[1].resourceName);
    CHECK(fixtureDiscovery.mods[0].uiImages.size() == 1 &&
        fixtureDiscovery.mods[1].uiImages.size() == 1);
    CHECK(fixtureDiscovery.mods[0].uiImages[0].identity == L"fighter" &&
        fixtureDiscovery.mods[1].uiImages[0].identity == L"fighter");
  }

  // A repeated basename is different: the engine's definition-name table is
  // global, so the early catalog must reject that collision instead of aliasing.
  const auto collision = MakeMod(root, L"collision", "sample.collision",
      "Colliding fighter", "alpha_fighter.xml");
  const auto rejected = cc2::host::CollectEarlyDeclarations(
      cc2::host::DiscoverActiveBridgeMods({alpha.wstring(), collision.wstring()}));
  CHECK(rejected.vehicles.size() == 1 && rejected.diagnostics.size() == 1);

  std::error_code ec;
  fs::remove_all(root, ec);
  CHECK(!ec);
  std::cout << "PASS identical local vehicle/UI IDs remain owner-scoped; engine resource-name collision is rejected\n";
  return 0;
}
