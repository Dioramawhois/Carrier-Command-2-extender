#pragma once

#include "cc2x.h"
#include <cstdint>

namespace cc2::api::vehicle::definition_adapter {

bool Install(const cc2x_api* api);
bool Enabled();
bool DefinitionReady(std::uint32_t id);
std::uint32_t CompatibilityType(std::uint32_t type);
void BeginDefinitionLoad();
void OnNameMapResize(cc2x_cpu* cpu);
void Flush(const cc2x_api* api);

}  // namespace cc2::api::vehicle::definition_adapter
