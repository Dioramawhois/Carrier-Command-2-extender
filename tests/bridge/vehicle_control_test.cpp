// SPDX-License-Identifier: GPL-3.0-or-later
#include "api/vehicle/control.h"
#include <Windows.h>
#include <cstring>
#include <cmath>
#include <iostream>
#include <limits>
namespace {unsigned char code[8]={0xf3,0x0f,0x11,0x87,0x14,0x03,0,0};bool wing=true;
std::uintptr_t Va(std::uint32_t address){return address==0x71dcbf?reinterpret_cast<std::uintptr_t>(code):0;}
std::int64_t GetI(const void*,int field,int){return field==CC2X_VEHICLE_IS_WING&&wing;}}
#define CHECK(x) do{if(!(x)){std::cerr<<"line "<<__LINE__<<": " #x "\n";return 1;}}while(0)
int main(){
  cc2x_api a{};a.game_build="1.5.18-win32-r2";a.va=Va;a.geti=GetI;
  alignas(16) unsigned char vehicle[0x400]{};auto* throttle=reinterpret_cast<float*>(vehicle+0x314);*throttle=0.8f;
  float result=0;using cc2::api::vehicle::ReadThrottle;using cc2::api::vehicle::WriteThrottle;
  CHECK(ReadThrottle(&a,vehicle,&result)&&result==0.8f);CHECK(*throttle==0.8f); // read is neutral
  CHECK(WriteThrottle(&a,vehicle,0.6)&&*throttle==0.6f);
  CHECK(!WriteThrottle(&a,vehicle,-0.1));CHECK(!WriteThrottle(&a,vehicle,1.01));
  CHECK(!WriteThrottle(&a,vehicle,std::nan("")));CHECK(!WriteThrottle(&a,vehicle,std::numeric_limits<double>::infinity()));
  wing=false;CHECK(!ReadThrottle(&a,vehicle,&result));CHECK(!WriteThrottle(&a,vehicle,1));wing=true;
  code[0]=0;CHECK(!WriteThrottle(&a,vehicle,1));code[0]=0xf3;
  a.game_build="unknown";CHECK(!WriteThrottle(&a,vehicle,1));a.game_build="1.5.18-win32-r2";
  CHECK(!WriteThrottle(&a,nullptr,1));CHECK(!ReadThrottle(&a,vehicle,nullptr));
  auto* readonly=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);std::memcpy(readonly,vehicle,sizeof(vehicle));
  DWORD old=0;CHECK(VirtualProtect(readonly,4096,PAGE_READONLY,&old));
  CHECK(ReadThrottle(&a,readonly,&result));CHECK(!WriteThrottle(&a,readonly,1));VirtualFree(readonly,0,MEM_RELEASE);
  CHECK(*throttle==0.6f);std::cout<<"PASS generic wing throttle: explicit writes, profile/signature, type, ranges, page protection\n";
}
