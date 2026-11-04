#include "runtime/mod_loader.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
void Write(const fs::path& path, const std::string& data) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << data;
}
std::string Descriptor(const std::string& screen = "scripts/screen_menu_main.lua",
                       const std::string& source = "cc2x/ui/owned.lua") {
  return "{\"schema\":1,\"extensions\":[{\"screen\":\"" + screen +
      "\",\"source\":\"" + source + "\"}]}";
}
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " << #x << "\n"; return 1; } } while (0)
int main() {
  wchar_t temp[MAX_PATH]{};
  CHECK(GetTempPathW(MAX_PATH, temp));
  const auto root = fs::path(temp) / (L"cc2x_screen_extensions_" + std::to_wstring(GetCurrentProcessId()));
  const auto active = root / L"active", inactive = root / L"inactive", other = root / L"other";
  const auto manifest = fs::path(L"content/cc2x/screen_extensions.json");
  const auto source = fs::path(L"content/cc2x/ui/owned.lua");
  for (const auto& package : {active, inactive, other}) {
    Write(package / L"mod.xml", "<data/>");
    Write(package / manifest, Descriptor());
    // This must compile without being executed; opening standard libraries is unnecessary.
    Write(package / source, "error('a syntax check must never execute this')\n");
  }
  auto discover = [&](const fs::path& package = {}) {
    return cc2::host::DiscoverActiveScreenExtensions({(package.empty() ? active : package).wstring()});
  };
  CHECK(cc2::host::DiscoverActiveScreenExtensions({}).extensions.empty());
  auto result = discover();
  CHECK(result.extensions.size() == 1 && result.diagnostics.empty());
  CHECK(result.extensions[0].screen == "scripts/screen_menu_main.lua");
  CHECK(result.extensions[0].source == "error('a syntax check must never execute this')\n");
  wchar_t shortPath[32768]{};
  CHECK(GetShortPathNameW(active.c_str(), shortPath, 32768));
  CHECK(discover(fs::path(shortPath)).extensions.size() == 1);
  CHECK(cc2::host::ScreenExtensionMatches(result.extensions[0], "scripts/screen_menu_main.lua"));
  CHECK(!cc2::host::ScreenExtensionMatches(result.extensions[0], "scripts/screen_menu_options.lua"));
  CHECK(!cc2::host::ScreenExtensionMatches(result.extensions[0], "screen_menu_main.lua"));
  CHECK(!cc2::host::ScreenExtensionMatches(result.extensions[0], "other/scripts/screen_menu_main.lua"));
  CHECK(!cc2::host::ScreenExtensionMatches(result.extensions[0], "Scripts/screen_menu_main.lua"));
  CHECK(!fs::exists(active / "bridge.json"));
  CHECK(cc2::host::DiscoverActiveScreenExtensions({active.wstring(), active.wstring()}).extensions.size() == 1);
  auto ordered = cc2::host::DiscoverActiveScreenExtensions({other.wstring(), active.wstring()});
  CHECK(ordered.extensions.size() == 2); // The inactive neighbor is never discovered.

  for (const auto& unsafe : {"../outside.lua", "cc2x/ui/../outside.lua", "cc2x\\\\ui/owned.lua",
                            "C:/outside.lua", "cc2x/ui/owned.lua:stream", "cc2x/ui/own\\u0000ed.lua"}) {
    Write(active / manifest, Descriptor("scripts/screen_menu_main.lua", unsafe));
    result = discover(); CHECK(result.extensions.empty() && !result.diagnostics.empty());
  }
  for (const auto& invalid : {
      "{\"schema\":1.0,\"extensions\":[]}",
      "{\"schema\":true,\"extensions\":[]}",
      "{\"schema\":1,\"schema\":1,\"extensions\":[]}",
      "{\"schema\":1,\"extensions\":[],\"unknown\":true}",
      "{\"schema\":1,\"extensions\":[{\"screen\":\"scripts/a.lua\",\"screen\":\"scripts/b.lua\",\"source\":\"cc2x/ui/owned.lua\"}]}",
      "{\"schema\":1,\"extensions\":[{\"screen\":\"scripts/a.lua\",\"source\":\"cc2x/ui/owned.lua\"},{\"screen\":\"scripts/a.lua\",\"source\":\"cc2x/ui/owned.lua\"}]}"}) {
    Write(active / manifest, invalid); CHECK(discover().extensions.empty());
  }
  Write(active / manifest, Descriptor());
  for (const auto& invalidSource : {std::string(), std::string("function broken("), std::string("\xff", 1),
                                   std::string("return 1\0--", 11), std::string(256 * 1024 + 1, ' ')}) {
    Write(active / source, invalidSource); CHECK(discover().extensions.empty());
  }
  Write(active / source, "--" + std::string(256 * 1024 - 2, ' '));
  CHECK(discover().extensions.size() == 1); // Inclusive source limit.
  Write(active / source, "return true\n");
  Write(active / manifest,
      "{\"schema\":1,\"extensions\":[{\"screen\":\"scripts/a.lua\",\"source\":\"cc2x/ui/owned.lua\"},"
      "{\"screen\":\"scripts/b.lua\",\"source\":\"cc2x/ui/missing.lua\"}]}");
  CHECK(discover().extensions.empty()); // An invalid second entry rejects the whole package.

  Write(active / manifest, Descriptor());
  std::vector<std::wstring> excess(257, active.wstring());
  CHECK(cc2::host::DiscoverActiveScreenExtensions(excess).extensions.empty());
  fs::remove(active / source);
  fs::create_directory(active / source);
  CHECK(discover().extensions.empty());
  fs::remove_all(root);
  std::cout << "PASS screen extension activation, boundaries, syntax without execution and atomic package validation\n";
  return 0;
}
