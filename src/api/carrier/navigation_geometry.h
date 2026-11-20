// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <cstdint>
namespace cc2::api::carrier::navigation_geometry {
struct Vertex {float x,y,z;};
inline std::uint32_t OrderedKey(std::uint32_t bits){
 if(!(bits&0x7fffffffu))bits=0; // IEEE comparisons treat signed zeros identically.
 return bits^((bits&0x80000000u)?0xffffffffu:0x80000000u);
}
inline bool Wet(const Vertex (&v)[3],double origin_y,double depth){
 if(!std::isfinite(origin_y)||!std::isfinite(depth)||depth<0)return false;
 for(const auto& p:v)if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||
     !(static_cast<double>(p.y)+origin_y < -depth))return false;
 return true;
}
inline bool Contains(const Vertex (&v)[3],double x,double z){
 if(!std::isfinite(x)||!std::isfinite(z))return false;
 const double a=(static_cast<double>(v[1].x)-v[0].x)*(v[2].z-v[0].z)-
                (static_cast<double>(v[1].z)-v[0].z)*(v[2].x-v[0].x);
 if(!std::isfinite(a)||std::abs(a)<1e-8)return false;
 const double u=((x-v[0].x)*(v[2].z-v[0].z)-(z-v[0].z)*(v[2].x-v[0].x))/a;
 const double w=((v[1].x-v[0].x)*(z-v[0].z)-(v[1].z-v[0].z)*(x-v[0].x))/a;
 return u>=-1e-7&&w>=-1e-7&&u+w<=1.0000001;
}
inline std::uint32_t AndFlags(std::uint32_t old,bool admitted){
 return (old&~0x8c5u)|(admitted?0u:0x44u);
}
}
