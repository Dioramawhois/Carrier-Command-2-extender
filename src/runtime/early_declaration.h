#pragma once

#include "mod_loader.h"
#include <cstdint>
#include <string>
#include <vector>

namespace cc2::host {

struct VehicleDeclaration {
  std::wstring owner, localId, identity, name, definitionPath, resourceName, ui;
  std::uint32_t nativeBaseType=0;
  // Optional deterministic ordering hint for preserving IDs during migrations.
  // Unranked declarations sort after ranked entries, then by identity.
  std::uint32_t order=0xffffffffu;
  std::uint32_t definitionId=0; // assigned by the process-lifetime registry
};

struct DeclarationCatalog {
  std::vector<VehicleDeclaration> vehicles;
  std::vector<std::wstring> diagnostics;
};

// Executes schema-2 entry scripts in a restricted, disposable Lua VM. The
// returned descriptors own all strings; no lua_State or Lua closure survives.
DeclarationCatalog CollectEarlyDeclarations(const ModDiscovery& discovery);

}  // namespace cc2::host
