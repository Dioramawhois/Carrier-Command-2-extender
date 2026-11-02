#pragma once

#include "api/ui/resource_registry.h"
#include "runtime/mod_loader.h"

#include <cstddef>
#include <string>
#include <vector>

namespace cc2::api::ui {

struct AtlasBuildResult {
  std::vector<ResourceDescriptor> resources;
  std::wstring cacheDirectory;
  std::string cacheKey;
  std::size_t regions = 0;
  std::size_t generatedSmallIcons = 0;
  bool cacheHit = false;
};

// One process-startup batch. It never writes to the source package or Workshop
// directory; all generated TXTR/ATLA files go to the CC2X-owned cache root.
bool PrepareAtlas(const host::ModDiscovery& mods, const std::wstring& vanillaAtlasPath,
                  const std::wstring& cacheRoot, const char* gameBuild,
                  AtlasBuildResult* output, std::string* error);

}  // namespace cc2::api::ui
