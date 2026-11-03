// SPDX-License-Identifier: GPL-3.0-or-later
// Engine layout research: Yolo / totolerigolo118, carrier-command-2-mods
// a40d1572eabd6d9710e6d64aae431824ffc98790, modes v3.1. See NOTICE.md.
#include "control.h"
#include <Windows.h>
#include <cmath>
#include <cstring>
#include <cstdint>
namespace cc2::api::vehicle {
namespace {
bool Region(const void* p,std::size_t n,bool write) {
  MEMORY_BASIC_INFORMATION m{};
  if(!p||!n||!VirtualQuery(p,&m,sizeof(m))||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
  const auto begin=reinterpret_cast<std::uintptr_t>(p),base=reinterpret_cast<std::uintptr_t>(m.BaseAddress);
  if(begin<base||begin-base>m.RegionSize||n>m.RegionSize-(begin-base))return false;
  const DWORD access=m.Protect&0xff;
  if(write)return access==PAGE_READWRITE||access==PAGE_WRITECOPY||access==PAGE_EXECUTE_READWRITE||access==PAGE_EXECUTE_WRITECOPY;
  return access==PAGE_READONLY||access==PAGE_READWRITE||access==PAGE_WRITECOPY||access==PAGE_EXECUTE_READ||access==PAGE_EXECUTE_READWRITE||access==PAGE_EXECUTE_WRITECOPY;
}
bool Profile(const cc2x_api* a) {
  if(!a||!a->game_build||!a->va||!a->geti)return false;
  std::uint32_t site=0;
  if(std::strcmp(a->game_build,"1.5.18-win32-r2")==0)site=0x71dcbf;
  else if(std::strcmp(a->game_build,"1.5.18-win32")==0)site=0x71dbbf;
  else return false;
  const auto* code=reinterpret_cast<const void*>(a->va(site));
  constexpr unsigned char expected[]{0xf3,0x0f,0x11,0x87,0x14,0x03,0,0};
  return Region(code,sizeof(expected),false)&&std::memcmp(code,expected,sizeof(expected))==0;
}
}
bool ReadThrottle(const cc2x_api* a,const void* vehicle,float* result) {
  if(!result||!Profile(a)||!Region(vehicle,0x318,false)||!a->geti(vehicle,CC2X_VEHICLE_IS_WING,0))return false;
  float value=0;std::memcpy(&value,static_cast<const unsigned char*>(vehicle)+0x314,sizeof(value));
  if(!std::isfinite(value)||value<0||value>1.0001f)return false;
  *result=value;return true;
}
bool WriteThrottle(const cc2x_api* a,void* vehicle,double value) {
  if(!std::isfinite(value)||value<0||value>1)return false;
  float current=0;if(!ReadThrottle(a,vehicle,&current)||!Region(static_cast<unsigned char*>(vehicle)+0x314,sizeof(float),true))return false;
  const float output=static_cast<float>(value);std::memcpy(static_cast<unsigned char*>(vehicle)+0x314,&output,sizeof(output));return true;
}
}
