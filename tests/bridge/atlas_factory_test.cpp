#include "api/ui/atlas_factory.h"
#include "api/ui/second_atlas_contract.h"

#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cc2::api::ui;

void Check(bool condition, const char* expression, int line) {
  if (!condition) throw std::runtime_error(std::string("check failed at line ") +
      std::to_string(line) + ": " + expression);
}

#define CHECK(...) Check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__, __LINE__)

namespace {

std::wstring FromUtf8(const char* value) {
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
  CHECK(length > 0);
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  CHECK(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1,
      result.data(), length) == length);
  result.pop_back();
  return result;
}

std::wstring TempRoot() {
  wchar_t temp[MAX_PATH]{};
  const DWORD length = GetTempPathW(MAX_PATH, temp);
  CHECK(length > 0 && length < MAX_PATH);
  return (std::filesystem::path(temp) /
      (L"CC2XAtlasFactoryTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
       std::to_wstring(GetTickCount64()))).wstring();
}

std::vector<std::uint8_t> ReadFile(const std::wstring& path) {
  std::ifstream file(std::filesystem::path(path), std::ios::binary | std::ios::ate);
  CHECK(static_cast<bool>(file));
  const auto end = file.tellg();
  CHECK(end >= 0);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
  file.seekg(0);
  file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  CHECK(static_cast<bool>(file));
  return bytes;
}

bool SameVisibleTxtr(const std::vector<std::uint8_t>& actual,
                     const std::vector<std::uint8_t>& expected) {
  if (actual.size() != expected.size() || actual.size() < 23 ||
      !std::equal(actual.begin(), actual.begin() + 23, expected.begin())) return false;
  for (std::size_t i = 23; i + 3 < actual.size(); i += 4) {
    if (actual[i + 3] != expected[i + 3]) return false;
    if (actual[i + 3] && !std::equal(actual.begin() + i, actual.begin() + i + 3,
                                     expected.begin() + i)) return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
 try {
  CHECK(argc == 3);
  const auto gameExe = std::filesystem::path(FromUtf8(argv[1]));
  const auto sourceRoot = FromUtf8(argv[2]);
  const auto atlasPath = (gameExe.parent_path() / L"rom_0" / L"textures" /
      L"screen_ui" / L"index.atlas").wstring();
  const auto cacheRoot = TempRoot();

  cc2::host::ModDescriptor mod;
  mod.id = L"cc2x.atlas.test";
  mod.rootPath = sourceRoot;
  cc2::host::UiImageDescriptor image;
  image.identity = L"test_fighter";
  image.large = L"textures/cc2x_aircraft_icons/f16_layout.png";
  mod.uiImages.push_back(image);
  cc2::host::UiImageDescriptor weaponImage;
  weaponImage.identity = L"test_missile";
  weaponImage.engineType = L"attachment_hardpoint_missile_ir";
  weaponImage.large = L"textures/cc2x_aircraft_icons/f16_layout.png";
  weaponImage.weapon = true;
  mod.uiImages.push_back(weaponImage);
  cc2::host::ModDiscovery discovery;
  discovery.mods.push_back(std::move(mod));

  AtlasBuildResult first;
  std::string error;
  CHECK(PrepareAtlas(discovery, atlasPath, cacheRoot, "1.5.18-win32-r2",
      &first, &error));
  CHECK(first.regions == 5);
  CHECK(first.generatedSmallIcons == 2);
  CHECK(!first.cacheHit);
  CHECK(first.cacheKey.size() == 64);
  CHECK(first.resources.size() == 6);
  CHECK(first.resources.back().type == ResourceType::AtlaIndex);
  CHECK(first.resources.back().virtualPath == "textures/cc2x_runtime/index.atlas");
  for (const auto& resource : first.resources) {
    if (resource.type != ResourceType::TxtrSurface) continue;
    CHECK(std::filesystem::exists(resource.generatedPath));
    CHECK(resource.generatedPath.find(cacheRoot) == 0);
    CHECK(resource.sourcePath.find(sourceRoot) == 0);
    CHECK(resource.regionToken >= 0x40000001u);
    if (resource.logicalId == L"weapons/test_missile/large")
      CHECK(resource.engineType == L"attachment_hardpoint_missile_ir");
    if (resource.logicalId == L"weapons/test_missile/small")
      CHECK(resource.engineType == L"attachment_hardpoint_missile_ir");
    if (resource.logicalId == L"vehicles/test_fighter/large" ||
        resource.logicalId == L"vehicles/test_fighter/loadout") {
      const auto fixtureRoot = std::filesystem::path(sourceRoot).parent_path();
      const auto expectedTxtr = (fixtureRoot / L"content" / L"textures" /
          L"cc2x_aircraft_icons" / L"f16_layout.txtr").wstring();
      CHECK(SameVisibleTxtr(ReadFile(resource.generatedPath), ReadFile(expectedTxtr)));
    }
  }
  const auto generatedAtlas = ReadFile((std::filesystem::path(first.cacheDirectory) / L"index.atlas").wstring());
  float bounds[4]{};
  const auto smallRegion = std::find_if(first.resources.begin(), first.resources.end(),
      [](const ResourceDescriptor& item) { return item.type == ResourceType::TxtrSurface && item.logicalId == L"vehicles/test_fighter/small"; });
  const auto largeRegion = std::find_if(first.resources.begin(), first.resources.end(),
      [](const ResourceDescriptor& item) { return item.type == ResourceType::TxtrSurface && item.logicalId == L"vehicles/test_fighter/large"; });
  CHECK(smallRegion != first.resources.end() && largeRegion != first.resources.end());
  const auto smallTxtr = ReadFile(smallRegion->generatedPath);
  CHECK(smallTxtr.size() == 23u + 16u * 16u * 4u);
  CHECK(smallTxtr[9] == 16 && smallTxtr[11] == 16);
  const auto weaponSmallRegion = std::find_if(first.resources.begin(), first.resources.end(),
      [](const ResourceDescriptor& item) { return item.type == ResourceType::TxtrSurface && item.logicalId == L"weapons/test_missile/small"; });
  CHECK(weaponSmallRegion != first.resources.end());
  const auto weaponSmallTxtr = ReadFile(weaponSmallRegion->generatedPath);
  CHECK(weaponSmallTxtr.size() == 23u + 16u * 16u * 4u);
  CHECK(weaponSmallTxtr[9] == 16 && weaponSmallTxtr[11] == 16);
  std::uint32_t x0 = 16, y0 = 16, x1 = 0, y1 = 0;
  for (std::uint32_t y = 0; y < 16; ++y) for (std::uint32_t x = 0; x < 16; ++x) {
    const auto bottomUpY = 15u - y;
    const auto alpha = smallTxtr[23u + (bottomUpY * 16u + x) * 4u + 3u];
    if (!alpha) continue;
    x0 = std::min(x0, x); y0 = std::min(y0, y);
    x1 = std::max(x1, x + 1u); y1 = std::max(y1, y + 1u);
  }
  // The measured F-16 source alpha bounds (8,2)-(56,64) fit to 11x14.
  // The follow-up game comparison selected the left of the two equally near
  // integer placements for this odd-width raster.
  CHECK(x0 == 2 && x1 == 13 && y0 == 1 && y1 == 15);
  CHECK(second_atlas::FindAtlasBoundsV1(generatedAtlas.data(), generatedAtlas.size(),
      smallRegion->regionName.c_str(), bounds));
  CHECK(bounds[0] == -8.0f && bounds[1] == 8.0f && bounds[2] == -8.0f && bounds[3] == 8.0f);
  CHECK(second_atlas::FindAtlasBoundsV1(generatedAtlas.data(), generatedAtlas.size(),
      largeRegion->regionName.c_str(), bounds));
  CHECK(bounds[0] == -32.0f && bounds[1] == 32.0f && bounds[2] == -32.0f && bounds[3] == 32.0f);
  const auto stale = std::filesystem::path(cacheRoot) / L"atlas-stale-test";
  std::filesystem::create_directories(stale);
  std::ofstream(stale / L"old.txtr", std::ios::binary).put('x');

  AtlasBuildResult second;
  CHECK(PrepareAtlas(discovery, atlasPath, cacheRoot, "1.5.18-win32-r2",
      &second, &error));
  CHECK(second.cacheHit);
  CHECK(second.cacheKey == first.cacheKey);
  CHECK(second.regions == first.regions);
  CHECK(second.generatedSmallIcons == 2);
  CHECK(!std::filesystem::exists(stale));

  cc2::host::ModDiscovery empty;
  AtlasBuildResult noUi;
  CHECK(PrepareAtlas(empty, atlasPath, cacheRoot + L"_empty", "1.5.18-win32-r2",
      &noUi, &error));
  CHECK(noUi.resources.empty() && noUi.regions == 0);
  CHECK(!std::filesystem::exists(cacheRoot + L"_empty"));

  auto invalid = discovery;
  invalid.mods[0].uiImages[0].small = L"textures/cc2x_aircraft_icons/f16_layout.png";
  AtlasBuildResult rejected;
  CHECK(!PrepareAtlas(invalid, atlasPath, cacheRoot, "1.5.18-win32-r2",
      &rejected, &error));
  CHECK(error == "ui_png_dimensions_do_not_match_declared_role");

  std::error_code ec;
  std::filesystem::remove_all(cacheRoot, ec);
  CHECK(!ec);
  return 0;
 } catch (const std::exception& error) {
   std::fprintf(stderr, "%s\n", error.what());
   return 1;
 }
}
