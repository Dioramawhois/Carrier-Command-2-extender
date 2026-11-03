// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "cc2x.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace cc2::api::vehicle {
using ButtonEnqueue=void(__attribute__((thiscall)) *)(void*,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t);
struct ButtonState {std::string name;std::uint32_t index;bool on,enabled;unsigned char* state;const unsigned char* definition;};
struct ButtonReference {std::int32_t index;const unsigned char* state;const unsigned char* definition;};
struct ButtonContext {
  std::uint32_t vehicleId=0;std::vector<ButtonState> buttons;std::vector<void*> queues;
  int sourceIndex=-1;
  std::map<std::uint32_t,bool> pending;ButtonEnqueue enqueue=nullptr;bool rejected=false;
};
bool BeginButtons(void* vehicle,void* peers,ButtonEnqueue enqueue,ButtonContext* context);
bool BeginButtonQuery(void* vehicle,ButtonContext* context);
bool SelectButtonReference(const ButtonContext& context,int index,ButtonReference* result);
bool StageButtons(ButtonContext* context,const std::map<std::string,bool>& values);
bool CommitButtons(ButtonContext* context);
using ButtonsCallback=void(*)(ButtonContext*,void*);
using ButtonQueryCallback=int(*)(ButtonContext*,void*);
using ButtonsInterest=bool(*)(const char*,void*);
bool InstallButtons(const cc2x_api* api,ButtonsCallback callback,void* user,
                    ButtonQueryCallback query=nullptr,ButtonsInterest interest=nullptr);
bool ConfigureButtons(const cc2x_api* api,ButtonsCallback callback,void* user,
                      ButtonQueryCallback query=nullptr,ButtonsInterest interest=nullptr);
bool ButtonsSupported();
bool EnsureButtons();
bool ButtonsReady();
}
