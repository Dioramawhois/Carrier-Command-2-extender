#pragma once
#include "cc2x.h"
#include <cstdint>
#include <limits>

namespace cc2::research::custom_registration_probe {
struct FactoryObservation {
  std::uint32_t returnAddress=0;
  std::uint32_t manager=0;
  std::uint32_t objectId=0;
  std::uint32_t definitionContext=0;
  std::uint32_t transform=0;
  std::uint32_t type=0;
  std::uint32_t owner=0;
  std::uint32_t generation=0;
  std::uint32_t server=0;
  // 0 = the legacy core factory entry; 1 = the current-build Hangar wrapper.
  // The wrapper probe observes arguments only and does not infer the inner
  // factory's object ID or definition-container layout.
  std::uint32_t boundary=0;
};

inline FactoryObservation DecodeFactoryEntry(const cc2x_cpu* cpu,const std::uint32_t* stack,
                                               std::uint32_t generation,std::uint32_t server) {
  FactoryObservation out{};
  if(!cpu||!stack)return out;
  out.returnAddress=stack[0];
  out.manager=cpu->ecx;
  out.objectId=stack[1];
  out.definitionContext=stack[2];
  out.transform=stack[3];
  out.type=stack[4];
  out.owner=stack[5];
  out.generation=generation;
  out.server=server;
  return out;
}

inline FactoryObservation DecodeFactoryWrapperEntry(const cc2x_cpu* cpu,const std::uint32_t* stack,
                                                     std::uint32_t generation,std::uint32_t server) {
  FactoryObservation out{};
  if(!cpu||!stack)return out;
  out.returnAddress=stack[0];
  out.manager=cpu->ecx;
  // Proven by the current Hangar backend's typed call to the exact-profile
  // wrapper: this, definition-container argument, transform, type, owner.
  out.definitionContext=stack[1];
  out.transform=stack[2];
  out.type=stack[3];
  out.owner=stack[4];
  out.generation=generation;
  out.server=server;
  out.boundary=1;
  return out;
}

inline bool ComputeDefinitionEntryCandidate(std::uint32_t base,std::uint32_t type,std::uint32_t* address) {
  // This helper addresses only confirmed stock definitions. The current
  // EXE's name map has 0x66 (102) slots: IDs 0..101. ID 102 is a research
  // candidate only; do not compute or dereference a presumed custom slot.
  constexpr std::uint32_t kStockTypeCount=102;
  constexpr std::uint32_t kDefinitionStride=0x220;
  if(!address||!base||type>=kStockTypeCount)return false;
  const std::uint64_t candidate=static_cast<std::uint64_t>(base)+
      static_cast<std::uint64_t>(type)*kDefinitionStride;
  if(candidate>std::numeric_limits<std::uint32_t>::max())return false;
  *address=static_cast<std::uint32_t>(candidate);
  return true;
}
}
