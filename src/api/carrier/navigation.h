// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "cc2x_navigation.h"
#include <cstdint>
#include <array>
#include <map>
#include <string>
namespace cc2::api::carrier {
class Motion;
class Navigation {
 public:
 explicit Navigation(Motion* motion):motion_(motion){}
 bool Available() const;
 bool Install();
 bool Read(std::uint32_t id,cc2x_navigation_state* out);
 bool Configure(const std::string& owner,std::uint64_t generation,std::uint32_t id,const cc2x_navigation_options& options);
 void Session();
 void Tick();
 void Drop(const std::string& owner,std::uint32_t id=0);
 void Drop(std::uint32_t id);
 bool Blocked(std::uint32_t id) const;
 bool Arrived(std::uint32_t id) const;
 bool AllowsOwner(const std::string& owner,std::uint32_t id) const;
 void Follower(void* self,const std::uint32_t args[10]);
 struct Policy {
  std::string owner;
  cc2x_navigation_options options{};
  cc2x_navigation_state state{};
  bool dirty=true;
  bool goal_admitted=false;
  std::uint64_t admitted_tick=0;
  std::uint64_t last_tick=0;
  void* carrier=nullptr;
  std::array<std::uint64_t,16> geometry{};
 };
 private:
 Motion* motion_;
 std::map<std::uint32_t,Policy> policies_;
 bool installed_=false,faulted_=false;
 void Invalidate(std::uint32_t id,const Policy& policy);
};
}
