#include "api/ui/resource_registry.h"

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

ResourceDescriptor Surface(const wchar_t* owner, const char* virtualPath,
                           const char* region, std::uint32_t token) {
  ResourceDescriptor value;
  value.owner = owner;
  value.logicalId = L"vehicle/f16/small";
  value.sourcePath = std::wstring(L"C:\\mods\\") + owner + L"\\content\\icons\\f16.png";
  value.virtualPath = virtualPath;
  value.type = ResourceType::TxtrSurface;
  value.sourceSha256 = std::string(64, 'a');
  value.generatedPath = std::wstring(L"C:\\cache\\") + owner + L".txtr";
  value.regionName = region;
  value.regionToken = token;
  return value;
}

}  // namespace

int main() {
 try {
  ResourceRegistry registry;
  std::string error;
  CHECK(registry.Publish({
      Surface(L"mod.alpha", "textures/cc2x/alpha/f16_small.txtr", "cc2x_r00000001", 0x40000001u),
      Surface(L"mod.beta", "textures/cc2x/beta/f16_small.txtr", "cc2x_r00000002", 0x40000002u),
  }, &error));
  CHECK(registry.Count() == 2);
  CHECK(registry.Find(L"MOD.ALPHA", L"VEHICLE/F16/SMALL") != nullptr);
  CHECK(registry.Find(L"mod.gamma", L"vehicle/f16/small") == nullptr);
  CHECK(registry.FindByVirtualPath("textures/cc2x/beta/f16_small.txtr")
      ->descriptor.owner == L"mod.beta");
  CHECK(registry.FindByRegionName("cc2x_r00000002")
      ->descriptor.owner == L"mod.beta");
  CHECK(!registry.Mark(L"mod.beta", L"vehicle/f16/small", EngineReady));
  CHECK(registry.Mark(L"mod.beta", L"vehicle/f16/small", OutputGenerated));
  CHECK(registry.Mark(L"mod.beta", L"vehicle/f16/small", EngineRequested));
  CHECK(registry.Mark(L"mod.beta", L"vehicle/f16/small", EngineReady));
  CHECK(!registry.Publish({}, &error));

  ResourceRegistry duplicate;
  auto first = Surface(L"mod.alpha", "textures/cc2x/alpha/f16_small.txtr", "cc2x_r00000001", 0x40000001u);
  auto second = Surface(L"mod.alpha", "textures/cc2x/alpha/f16_small_2.txtr", "cc2x_r00000002", 0x40000002u);
  CHECK(!duplicate.Publish({first, second}, &error));

  ResourceRegistry unsafe;
  auto traversal = Surface(L"mod.alpha", "textures/cc2x/../other/f16.txtr", "cc2x_r00000001", 0x40000001u);
  CHECK(!unsafe.Publish({traversal}, &error));
  return 0;
 } catch (const std::exception& error) {
   std::fprintf(stderr, "%s\n", error.what());
   return 1;
 }
}
