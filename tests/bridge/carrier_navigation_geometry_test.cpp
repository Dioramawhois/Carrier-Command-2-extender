// SPDX-License-Identifier: GPL-3.0-or-later
#include "api/carrier/navigation_geometry.h"
#include <cassert>
#include <cmath>
#include <limits>
using namespace cc2::api::carrier::navigation_geometry;
int main(){
 const Vertex shallow[]{{0,-5,0},{100,-5,0},{0,-5,100}};
 const Vertex land[]{{0,-5,0},{100,1,0},{0,-5,100}};
 const Vertex deep[]{{0,-200,0},{100,-200,0},{0,-200,100}};
 assert(!Wet(shallow,0,100)); // Reproduces stock deep-water refusal.
 assert(Wet(shallow,0,0)&&!Wet(shallow,0,6));
 assert(Wet(deep,0,100)&&!Wet(land,0,0));
 assert(!Wet(shallow,10,0)); // Account for the island mesh's world Y origin.
 assert(Contains(shallow,25,25)&&!Contains(shallow,90,90));
 assert(Contains(shallow,0,0));
 Vertex invalid[3]{shallow[0],shallow[1],shallow[2]};
 invalid[1].y=std::numeric_limits<float>::quiet_NaN();
 assert(!Wet(invalid,0,0));
 invalid[1].y=-0.f;assert(!Wet(invalid,0,0));
 assert(OrderedKey(0)==OrderedKey(0x80000000u));
 const Vertex collapsed[]{{0,-5,0},{0,-5,0},{0,-5,0}};
 assert(!Contains(collapsed,0,0));
 assert(AndFlags(0xffffffffu,true)==((0xffffffffu&~0x8c5u)));
 assert(AndFlags(0xffffffffu,false)==((0xffffffffu&~0x8c5u)|0x44u));
}
