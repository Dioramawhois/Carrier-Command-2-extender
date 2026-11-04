#pragma once
#include <string>
#include <string_view>
#include <vector>
namespace cc2::host {
enum class HookMode {Before,After,Replace,Around};
enum class ModSource {Local,Workshop};
enum class ModActivation {StockActive,WorkshopActive};
struct HookDescriptor {std::wstring target;HookMode mode=HookMode::After;std::wstring handler;};
struct UiImageDescriptor {
  std::wstring identity, small, large, loadout;
  // Stock e_game_object_type symbol for weapon/equipment icon replacement.
  std::wstring engineType;
  bool weapon=false;
};
struct ModDescriptor {
  std::wstring id,name,version,rootPath,packagePath,entryPath;
  std::wstring rulesEntry; // optional pure-Lua construction-time decision entry
  std::vector<std::wstring> bridgeScripts;
  std::vector<HookDescriptor> hooks;
  std::vector<UiImageDescriptor> uiImages;
  unsigned schema=1;
  ModActivation activation=ModActivation::StockActive;
  ModSource source=ModSource::Local;
};
struct ModDiscovery {std::vector<ModDescriptor> mods;std::vector<std::wstring> diagnostics;};
// Explicit screen-VM extensions are independent of Bridge gameplay modules.
// Sources are copied once from enabled packages; no file is executed here.
struct ScreenExtensionDescriptor {std::string screen,sourcePath,source;};
struct ScreenExtensionDiscovery {
  std::vector<ScreenExtensionDescriptor> extensions;
  std::vector<std::wstring> diagnostics;
};
ScreenExtensionDiscovery DiscoverActiveScreenExtensions(const std::vector<std::wstring>& activePackages);
bool ScreenExtensionMatches(const ScreenExtensionDescriptor& extension,std::string_view screen);
ModDiscovery DiscoverActiveBridgeMods(const std::vector<std::wstring>& activePackages);
bool ResolveModAssetPath(const ModDescriptor& mod,const std::wstring& relative,
                         const wchar_t* extension,std::wstring* absolute);
bool ValidateUiImageDeclaration(const ModDescriptor& mod,const UiImageDescriptor& image,std::wstring* error);
bool WriteBridgeBootstrap(const ModDescriptor& mod,const std::wstring& sdkPath,const std::wstring& runtimeRoot,std::wstring*bootstrapPath,std::wstring*error);
}
