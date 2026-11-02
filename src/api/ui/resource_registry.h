#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cc2::api::ui {

enum class ResourceType : std::uint32_t {
  PngSource = 1,
  TxtrSurface = 2,
  AtlaIndex = 3,
  Mesh = 4,
  DefinitionXml = 5,
};

enum ResourceState : std::uint32_t {
  SourceValidated = 1u,
  OutputGenerated = 2u,
  EngineRequested = 4u,
  EngineReady = 8u,
};

struct ResourceDescriptor {
  std::wstring owner;
  std::wstring logicalId;
  std::wstring engineType;
  std::wstring sourcePath;
  std::string virtualPath;
  ResourceType type = ResourceType::PngSource;
  std::string sourceSha256;
  std::wstring generatedPath;
  std::string regionName;
  std::uint32_t regionToken = 0;
};

struct ResourceRecord {
  ResourceDescriptor descriptor;
  std::atomic<std::uint32_t> state{0};
};

// Startup-published, process-lifetime map. Resource identity is always the
// pair (owner, logicalId); virtual engine paths are separately unique.
// Engine handles are intentionally absent until their safe lifetime is known.
class ResourceRegistry {
 public:
  bool Publish(std::vector<ResourceDescriptor> resources, std::string* error);
  const ResourceRecord* Find(const std::wstring& owner,
                             const std::wstring& logicalId) const;
  const ResourceRecord* FindByVirtualPath(const std::string& virtualPath) const;
  const ResourceRecord* FindByRegionName(const std::string& regionName) const;
  const ResourceRecord* FindByRegionToken(std::uint32_t regionToken) const;
  const ResourceRecord* At(std::size_t index) const;
  std::size_t Count() const {
    return published_.load(std::memory_order_acquire) ? count_ : 0;
  }
  bool Published() const { return published_.load(std::memory_order_acquire); }
  bool Mark(const std::wstring& owner, const std::wstring& logicalId,
            ResourceState state);

 private:
  std::unique_ptr<ResourceRecord[]> records_;
  std::size_t count_ = 0;
  std::atomic<bool> published_{false};
};

ResourceRegistry& Registry();

}  // namespace cc2::api::ui
