#pragma once
#include "cc2x.h"
#include <cstdint>

namespace cc2::research::custom_registration_probe {
bool Install(const cc2x_api* api);
bool Enabled();
bool ArmOneFactoryCall();
void CancelArm();
void OnSession(void* server,bool started,std::uint64_t generation);
void Flush(const cc2x_api* api);
}
