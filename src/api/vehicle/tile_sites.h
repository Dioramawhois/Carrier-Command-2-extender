// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
namespace cc2::api::world {
struct TileSite {std::array<double,12> transform{};std::uint32_t flags_low=0,flags_high=0;};
inline bool TileSiteSelector(std::uint32_t kind,std::uint32_t definition){return kind<64&&definition<64;}
}
