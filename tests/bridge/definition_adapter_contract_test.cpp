#include "api/vehicle/definition_adapter_contract.h"

#include <cstdio>
#include <cstdint>
#include <cstring>

int main() {
  using namespace cc2::api::vehicle::definition_adapter;
  constexpr std::uint32_t preferredReturn = 0x005A2EA1;
  constexpr std::uint32_t relocatedBase = 0x008A0000;
  constexpr std::uint32_t runtimeReturn = relocatedBase + (preferredReturn - 0x00400000);
  if (!ShouldExpandNameMap(true, runtimeReturn, runtimeReturn, kStockDefinitionCount) ||
      ShouldExpandNameMap(false, runtimeReturn, runtimeReturn, kStockDefinitionCount) ||
      ShouldExpandNameMap(true, runtimeReturn - 1, runtimeReturn, kStockDefinitionCount) ||
      ShouldExpandNameMap(true, runtimeReturn, runtimeReturn, kStockDefinitionCount + 1) ||
      ShouldExpandNameMap(true, runtimeReturn, 0, kStockDefinitionCount)) {
    std::fprintf(stderr, "name-map expansion guard contract failed\n");
    return 1;
  }
  if (!ShouldRegisterCustomDefinition(true, kCustomDefinitionLimit,
          kInitialDefinitionArrayCount) ||
      ShouldRegisterCustomDefinition(false, kCustomDefinitionLimit,
          kInitialDefinitionArrayCount) ||
      ShouldRegisterCustomDefinition(true, kCustomDefinitionLimit - 1,
          kInitialDefinitionArrayCount) ||
      ShouldRegisterCustomDefinition(true, kCustomDefinitionLimit,
          kCustomDefinitionLimit) ||
      ShouldRegisterCustomDefinition(true, kCustomDefinitionLimit,
          kStockDefinitionCount)) {
    std::fprintf(stderr, "custom definition registration guard contract failed\n");
    return 2;
  }
  constexpr auto f16Offset = RecordOffsetForDefinition(kF16DefinitionId);
  constexpr auto f15Offset = RecordOffsetForDefinition(kF15DefinitionId);
  if (!IsCustomDefinitionParseSuccess(true, f16Offset, 1) ||
      !IsCustomDefinitionParseSuccess(true, f15Offset, 0x101) ||
      ParsedDefinitionId(f16Offset) != kF16DefinitionId ||
      ParsedDefinitionId(f15Offset) != kF15DefinitionId ||
      ParsedDefinitionId(RecordOffsetForDefinition(kCustomDefinitionLimit)) != 0 ||
      IsCustomDefinitionParseSuccess(true, kF16DefinitionId, 1) ||
      IsCustomDefinitionParseSuccess(false, f16Offset, 1) ||
      IsCustomDefinitionParseSuccess(true, f16Offset, 0) ||
      IsCustomDefinitionParseSuccess(true, f16Offset, 0x100)) {
    std::fprintf(stderr, "custom XML parse-success register contract failed\n");
    return 3;
  }
  const auto allReady = kF16ReadyBit | kF15ReadyBit;
  const auto* f16 = FindCustomAircraft(kF16DefinitionId);
  const auto* f15 = FindCustomAircraft(kF15DefinitionId);
  if (!f16 || !f15 || FindCustomAircraft(kCustomDefinitionLimit) != nullptr ||
      kCustomAircraftDefinitionCount != 2 || f16->native_class_type != 8 ||
      f15->native_class_type != 10 || std::strcmp(f16->display_name, "F-16") != 0 ||
      std::strcmp(f15->display_name, "F-15") != 0 ||
      !IsAirDefinitionClass(f16->native_class_type) ||
      !IsAirDefinitionClass(f15->native_class_type) || IsAirDefinitionClass(2)) {
    std::fprintf(stderr, "custom aircraft selector metadata registry failed\n");
    return 4;
  }
  if (!IsDefinitionReady(kF16ReadyBit, kF16DefinitionId) ||
      IsDefinitionReady(kF16ReadyBit, kF15DefinitionId) ||
      IsDefinitionReady(allReady, 99) ||
      NativeClassType(true, allReady, kF16DefinitionId) != kF16NativeClassType ||
      NativeClassType(true, allReady, kF15DefinitionId) != kF15NativeClassType ||
      NativeClassType(true, kF16ReadyBit, kF15DefinitionId) != kF15DefinitionId ||
      NativeClassType(true, 0, kF16DefinitionId) != kF16DefinitionId ||
      NativeClassType(false, allReady, kF16DefinitionId) != kF16DefinitionId ||
      NativeClassType(true, allReady, kF16NativeClassType) != kF16NativeClassType ||
      NativeClassType(true, allReady, kF16DefinitionId - 1) != kF16DefinitionId - 1) {
    std::fprintf(stderr, "native class alias policy failed\n");
    return 5;
  }
  std::puts("Custom type prototype policy guards passed");
  return 0;
}
