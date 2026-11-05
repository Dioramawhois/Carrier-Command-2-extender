// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace cc2::api::vehicle {
// Shared admission rules; collection and engine invocation remain separate.
inline bool AttachmentRequestAllowed(std::uint32_t slot,std::int32_t definition,
    std::uint32_t slots,int dock,std::uint32_t parent,std::uint32_t waypoints) {
  return slots>0&&slots<=64&&slot<slots&&definition>=-1&&definition<102&&
      dock==4&&parent==0&&waypoints==0;
}
}
