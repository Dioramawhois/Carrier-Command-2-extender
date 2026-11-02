#pragma once

#include "runtime/early_declaration.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cc2::api::vehicle {

enum DefinitionState : std::uint32_t {
  NameRegistered=1u, ArrayInitialized=2u, XmlParsed=4u, Ready=8u
};

struct DefinitionRecord {
  host::VehicleDeclaration descriptor;
  std::string resourceName, displayName;
  std::atomic<std::uint32_t> state{0};
};

// One process-lifetime owner of ID allocation, metadata and readiness.
// Publication completes before the engine definition loader's first entry.
class DefinitionRegistry {
 public:
  bool Publish(std::vector<host::VehicleDeclaration> declarations,
               std::uint32_t firstId,std::string* error);
  bool MatchesCatalog(std::vector<host::VehicleDeclaration> declarations,
                      std::string* error) const;
  const DefinitionRecord* Find(std::uint32_t id) const;
  const DefinitionRecord* Find(const std::wstring& identity) const;
  std::size_t Count() const {return published_.load(std::memory_order_acquire)?count_:0;}
  std::uint32_t FirstId() const {return published_.load(std::memory_order_acquire)?firstId_:0;}
  std::uint32_t Limit() const {return firstId_+static_cast<std::uint32_t>(count_);}
  void BeginLoad();
  bool Mark(std::uint32_t id,DefinitionState state);
  bool ReadyFor(std::uint32_t id) const;
  std::uint32_t CompatibilityType(std::uint32_t id) const;
 private:
  std::unique_ptr<DefinitionRecord[]> records_;
  std::size_t count_=0;
  std::uint32_t firstId_=0;
  std::atomic<bool> published_{false};
};

DefinitionRegistry& Registry();

}  // namespace cc2::api::vehicle
