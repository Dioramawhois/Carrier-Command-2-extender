#pragma once

#include <cstdint>

namespace cc2::api::vehicle::definition_adapter {

constexpr std::uint32_t kStockDefinitionCount = 102;
constexpr std::uint32_t kInitialDefinitionArrayCount = 0;
constexpr std::uint32_t kDefinitionRecordStride = 0x220;
constexpr std::uint32_t kFirstCustomDefinitionId = 102;
constexpr std::uint32_t kCustomDefinitionCount = 2;
constexpr std::uint32_t kCustomDefinitionLimit =
    kFirstCustomDefinitionId + kCustomDefinitionCount;
constexpr std::uint32_t kF16DefinitionId = 102;
constexpr std::uint32_t kF15DefinitionId = 103;
constexpr std::uint32_t kF16NativeClassType = 8;  // c_vehicle_*_wing_light
constexpr std::uint32_t kF15NativeClassType = 10; // c_vehicle_*_wing_heavy
constexpr std::uint32_t kF16ReadyBit = 1u << 0;
constexpr std::uint32_t kF15ReadyBit = 1u << 1;

// Single source for the opt-in prototype's definition metadata. The same
// records drive name registration, class aliasing, and the diagnostic
// stock-Lua selector query.
struct CustomAircraftDefinition {
  std::uint32_t id;
  const char* resource_name;
  const char* display_name;
  std::uint32_t native_class_type;
  std::uint32_t ready_bit;
};

constexpr CustomAircraftDefinition kCustomAircraftDefinitions[] = {
    {kF16DefinitionId, "cc2x_f16", "F-16", kF16NativeClassType, kF16ReadyBit},
    {kF15DefinitionId, "cc2x_f15", "F-15", kF15NativeClassType, kF15ReadyBit},
};
constexpr std::uint32_t kCustomAircraftDefinitionCount =
    static_cast<std::uint32_t>(sizeof(kCustomAircraftDefinitions) /
                               sizeof(kCustomAircraftDefinitions[0]));

inline const CustomAircraftDefinition* FindCustomAircraft(std::uint32_t id) {
  for (const auto& definition : kCustomAircraftDefinitions) {
    if (definition.id == id) return &definition;
  }
  return nullptr;
}

inline bool IsAirDefinitionClass(std::uint32_t type) {
  return type == kF16NativeClassType || type == kF15NativeClassType ||
      type == 12 || type == 14;
}

inline std::uint32_t ReadyBitForDefinition(std::uint32_t id) {
  const auto* definition = FindCustomAircraft(id);
  return definition ? definition->ready_bit : 0;
}

inline constexpr std::uint32_t RecordOffsetForDefinition(std::uint32_t id) {
  return id >= kFirstCustomDefinitionId && id < kCustomDefinitionLimit
      ? id * kDefinitionRecordStride : 0;
}

inline bool IsCustomDefinition(std::uint32_t id) {
  return ReadyBitForDefinition(id) != 0;
}
inline bool ShouldExpandNameMap(bool enabled, std::uint32_t returnAddress,
                                std::uint32_t expectedRuntimeReturnAddress,
                                std::uint32_t requestedCount) {
  return enabled && expectedRuntimeReturnAddress != 0 &&
      returnAddress == expectedRuntimeReturnAddress &&
      requestedCount == kStockDefinitionCount;
}

inline bool ShouldRegisterCustomDefinition(bool enabled, std::uint32_t nameMapCount,
                                           std::uint32_t definitionArrayCount) {
  return enabled && nameMapCount == kCustomDefinitionLimit &&
      definitionArrayCount == kInitialDefinitionArrayCount;
}

// FUN_005A5E80 scales the definition index in EDI by the record stride before
// the XML-document parse call. At the success hook EDI is therefore a byte
// offset, not the original definition ID.
inline std::uint32_t ParsedDefinitionId(std::uint32_t recordOffset) {
  for (std::uint32_t id = kFirstCustomDefinitionId;
       id < kCustomDefinitionLimit; ++id) {
    if (recordOffset == id * kDefinitionRecordStride) return id;
  }
  return 0;
}

inline bool IsCustomDefinitionParseSuccess(bool enabled,
                                           std::uint32_t recordOffset,
                                           std::uint32_t parserResult) {
  return enabled && ParsedDefinitionId(recordOffset) != 0 &&
      (parserResult & 0xFFu) != 0;
}

inline bool IsDefinitionReady(std::uint32_t readyMask, std::uint32_t id) {
  const auto bit = ReadyBitForDefinition(id);
  return bit != 0 && (readyMask & bit) != 0;
}

inline std::uint32_t NativeClassType(bool enabled, std::uint32_t readyMask,
                                     std::uint32_t type) {
  if (!enabled || !IsDefinitionReady(readyMask, type)) return type;
  const auto* definition = FindCustomAircraft(type);
  return definition ? definition->native_class_type : type;
}

}  // namespace cc2::api::vehicle::definition_adapter
