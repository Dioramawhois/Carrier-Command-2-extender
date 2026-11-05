// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
struct lua_State;
namespace cc2::runtime {class Host;void RegisterModuleExtensions(lua_State*,Host*,const std::string& owner);}
