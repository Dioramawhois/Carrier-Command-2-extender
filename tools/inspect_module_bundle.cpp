// SPDX-License-Identifier: GPL-3.0-or-later
// Developer read-only inventory: never executes a mod Lua script or DLL.
#include "runtime/mod_loader.h"
#include <iostream>
#include <string>

int wmain(int argc, wchar_t** argv) {
  if (argc != 2) {
    std::cerr << "Expected one package directory\n";
    return 2;
  }
  const auto result = cc2::host::DiscoverActiveBridgeMods({argv[1]});
  if (!result.diagnostics.empty()) {
    for (const auto& diagnostic : result.diagnostics) std::wcerr << diagnostic << L"\n";
    return 1;
  }
  for (const auto& mod : result.mods) {
    // The SDK guarantees bounded ASCII owner IDs; keep private paths out of stdout.
    for (const auto character : mod.id) std::cout << static_cast<char>(character);
    std::cout << '\n';
  }
  return 0;
}
