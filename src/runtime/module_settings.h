// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Windows.h>
#include <map>
#include <mutex>
#include <string>
namespace cc2::runtime {
// One bounded typed INI per module. No caller-supplied path or Lua file handle.
class ModuleSettings {
 public:
 ModuleSettings(std::wstring root,std::string owner):root_(std::move(root)),owner_(std::move(owner)){}
 bool Reload(std::string* reason);
 bool Number(const std::string& key,double* out);
 bool Boolean(const std::string& key,bool* out);
 bool SetNumber(const std::string& key,double value,std::string* reason);
 bool SetBoolean(const std::string& key,bool value,std::string* reason);
 static bool SafeName(const std::string& name);
 private:
 struct Value {double number=0;bool boolean=false,isBoolean=false;};
 using Values=std::map<std::string,Value>;
 bool Read(Values* values,std::string* reason);
 bool Write(const Values& values,std::string* reason);
 bool Set(const std::string& key,Value value,std::string* reason);
 std::recursive_mutex mutex_;
 std::wstring root_;std::string owner_;Values values_;
};
}
