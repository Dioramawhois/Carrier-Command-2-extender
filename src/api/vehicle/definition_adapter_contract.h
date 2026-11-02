#pragma once
#include <cstdint>
namespace cc2::api::vehicle::definition_adapter {
constexpr std::uint32_t kStockDefinitionCount = 102;
constexpr std::uint32_t kInitialDefinitionArrayCount = 0;
constexpr std::uint32_t kDefinitionRecordStride = 0x220;
constexpr std::uint32_t kFirstCustomDefinitionId = 102;
inline bool ShouldExpandNameMap(bool enabled, std::uint32_t address,
                               std::uint32_t expected, std::uint32_t requested) {
  return enabled && expected && address == expected && requested == kStockDefinitionCount;
}
}
