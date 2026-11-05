// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime/module_settings.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
namespace fs=std::filesystem;
int main(){
 using cc2::runtime::ModuleSettings;
 auto root=fs::temp_directory_path()/("cc2x_settings_"+std::to_string(GetCurrentProcessId()));fs::create_directories(root);
 const auto path=root/"settings-6272616b65.ini";
 auto write=[&](const char* text){std::ofstream(path,std::ios::binary)<<text;};
 ModuleSettings s(root.wstring(),"brake");double n=0;bool b=false;std::string why;
 assert(s.Reload(&why));assert(!s.Number("brake",&n));
 write("# user settings\n[brake]\nbrake=0.75\nhold_idle=true\nheight_m=40\n");
 assert(s.Reload(&why)&&s.Number("brake",&n)&&n==0.75&&s.Boolean("hold_idle",&b)&&b);
 write("brake=0.25\nbrake=0.8\n");assert(!s.Reload(&why));assert(s.Number("brake",&n)&&n==0.75); // transactional reload
 write("brake=nan\n");assert(!s.Reload(&why));write("brake=1e999\n");assert(!s.Reload(&why));
 write("[another]\nbrake=0.1\n");assert(!s.Reload(&why));
 write("x=\"not a number\"\n");assert(!s.Reload(&why));
 std::ofstream(path,std::ios::binary)<<std::string(65537,'x');assert(!s.Reload(&why));
 ModuleSettings unsafe(root.wstring(),"../outside");assert(!unsafe.Reload(&why));
 ModuleSettings reserved(root.wstring(),"CON");assert(!reserved.Reload(&why));
 write("brake=0.4\nhold_idle=0\n");assert(s.Reload(&why)&&s.Boolean("hold_idle",&b)&&!b);
 if(!s.SetNumber("brake",0.6,&why)||!s.SetBoolean("hold_idle",true,&why)){std::cerr<<why<<" Win32="<<GetLastError()<<'\n';return 1;}
 ModuleSettings reopened(root.wstring(),"brake");assert(reopened.Reload(&why)&&reopened.Number("brake",&n)&&n==0.6&&reopened.Boolean("hold_idle",&b)&&b);
 assert(!s.SetNumber("bad",std::numeric_limits<double>::infinity(),&why));
 // A locked destination must keep both the previous file and cached value.
 HANDLE locked=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);assert(locked!=INVALID_HANDLE_VALUE);
 assert(!s.SetNumber("brake",0.9,&why)&&s.Number("brake",&n)&&n==0.6);CloseHandle(locked);
 for(const auto& f:fs::directory_iterator(root))assert(f.path().extension()!=L".tmp");
 // A hard link must never let a module overwrite another file's contents.
 fs::create_hard_link(path,root/"another.ini");assert(!s.Reload(&why)&&!s.SetNumber("brake",0.9,&why));fs::remove(root/"another.ini");
 ModuleSettings upper(root.wstring(),"Brake");assert(upper.SetNumber("brake",0.9,&why));
 assert(s.Reload(&why)&&s.Number("brake",&n)&&n==0.6);fs::remove(root/"settings-4272616b65.ini");
 std::string keys;for(int i=0;i<128;++i)keys+="v"+std::to_string(i)+"=1\n";write(keys.c_str());
 assert(s.Reload(&why)&&!s.SetNumber("overflow",1,&why));
 // Symlink creation depends on Windows developer-mode/privilege. If available,
 // test both the file leaf and an ancestor without following either link.
 fs::remove(path);const auto destination=root/"safe.ini";std::ofstream(destination)<<"brake=0.2\n";
 if(CreateSymbolicLinkW(path.c_str(),destination.c_str(),2)){
 assert(!s.Reload(&why)&&!s.SetNumber("brake",0.9,&why));fs::remove(path);
 std::cout<<"PASS settings leaf symlink refusal\n";
 }
 const auto alias=root/"alias";
 if(CreateSymbolicLinkW(alias.c_str(),root.c_str(),3)){
 ModuleSettings linked(alias.wstring(),"brake");assert(!linked.Reload(&why));fs::remove(alias);
 std::cout<<"PASS settings ancestor symlink refusal\n";
 }
 fs::remove(destination);write("brake=0.6\n");
 fs::remove(path);assert(s.Reload(&why)&&!s.Number("brake",&n));
 // Only fixture-owned files in the exact temporary directory are removed.
 fs::remove(root);std::cout<<"PASS bounded typed settings, malformed input, transactional reload, safe names\n";
}
