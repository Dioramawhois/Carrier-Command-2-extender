// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "cc2x_motion.h"
#include <Windows.h>
#include <map>
#include <string>
#include <vector>
namespace cc2::api::carrier {
class Motion {
 public:
 void Configure(const cc2x_api* api);
 void Session(void* server,std::uint64_t generation);
 void Tick(std::uint64_t tick);
 bool Available() const;
 bool Read(std::uint32_t id,cc2x_motion_state* out);
 bool Set(const std::string& owner,std::uint64_t generation,std::uint32_t id,bool active,double power);
 bool Contacts(std::uint32_t id,std::vector<cc2x_contact>* out);
 void ObserveControls(void* carrier,void* inputs);
 void BeforeMotion(void* carrier);
 void AfterMotion(void* carrier);
 void DropOwner(const std::string& owner);
 private:
 bool Context() const;
 bool Install();
 unsigned char* Carrier(std::uint32_t id) const;
 bool LocalTeam(std::uint32_t* team) const;
 void Stop(unsigned char* carrier);
 struct Lease {std::string owner;std::uint64_t tick;double power;};
 const cc2x_api* api_=nullptr;void* server_=nullptr;DWORD thread_=0;
 std::uint64_t generation_=0,tick_=0;
 std::map<std::uint32_t,Lease> leases_;
 std::map<std::uint32_t,bool> manual_;
 bool installed_=false;
};
}
