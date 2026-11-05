// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "cc2x_carrier.h"
#include <Windows.h>
#include <cstdint>
#include <deque>
#include <mutex>
#include <map>
#include <string>
namespace cc2::api::carrier {
struct RecoveryProfile {
 const char* name;std::uint32_t timestamp,h1,h2,h2next,h5,h5next,h6,h6next,vtable,dock;
};
class Recovery {
 public:
 using DockInvoker=void(*)(std::uintptr_t,void*,void*,std::uint32_t);
 explicit Recovery(DockInvoker invoker=nullptr):invoke_(invoker){}
 void Configure(const cc2x_api* api);
 void Session(void* server,std::uint64_t generation);
 void Tick(void* server,std::uint64_t generation,std::uint64_t tick);
 bool Available(std::uint32_t capability);
 std::uint64_t Request(const std::string& owner,std::uint64_t generation,std::uint32_t carrier,std::uint32_t vehicle);
 bool Result(const std::string& owner,std::uint64_t request,cc2x_dock_result* out);
 bool Arm(const std::string& owner,std::uint64_t generation,std::uint32_t carrier,std::uint32_t vehicle,const cc2x_recovery_options& options);
 bool Disarm(const std::string& owner,std::uint32_t vehicle);
 bool Status(const std::string& owner,std::uint32_t vehicle,cc2x_recovery_status* out);
 void DropOwner(const std::string& owner);
 void Hook(unsigned site,cc2x_cpu* cpu);
 private:
 bool Context() const;
 bool Profile(bool hooks) const;
 bool Install();
 bool Pair(std::uint32_t carrier,std::uint32_t vehicle,void** ship,void** aircraft) const;
 bool Parked(void* ship,void* aircraft) const;
 bool FreeBay(const unsigned char* ship) const;
 bool LiveWindow();
 std::uint32_t Execute(const cc2x_dock_result& result);
 void ProcessHook(unsigned site,cc2x_cpu* cpu);
 void Stop(std::uint32_t reason);
 struct RequestRecord {std::string owner;cc2x_dock_result result;std::uint64_t tick;};
 std::recursive_mutex mutex_;
 const cc2x_api* api_=nullptr;const RecoveryProfile* profile_=nullptr;
 DockInvoker invoke_=nullptr;
 void* server_=nullptr;DWORD thread_=0;
 std::uint64_t generation_=0,tick_=0,next_=0,deadline_=0;
 std::deque<RequestRecord> requests_;
 std::map<std::uint32_t,std::uint32_t> used_; // one window per continuous recall, across owner reloads
 std::string owner_;
 cc2x_recovery_status status_{sizeof(status_)};
 std::uint32_t grantsLeft_=0,jumpsLeft_=0,rotorLeft_=0,fake_[3]{};
 void* manager_=nullptr;bool ours_=false,rotor_=false,installed_=false,faulted_=false;
};
}
