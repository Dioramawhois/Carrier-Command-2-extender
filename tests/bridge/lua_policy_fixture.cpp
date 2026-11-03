// SPDX-License-Identifier: GPL-3.0-or-later
// Executes reviewed project-owned policy regression fixtures with Lua's
// filesystem, OS, package, debug and native-library loaders absent.
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
bool Load(lua_State* L,const char* path,int results) {
  std::ifstream file(path,std::ios::binary|std::ios::ate);
  if(!file||file.tellg()<0||file.tellg()>1024*1024){std::cerr<<"fixture file unavailable or too large\n";return false;}
  std::string source(static_cast<std::size_t>(file.tellg()),'\0');file.seekg(0);file.read(source.data(),source.size());
  if(!file||luaL_loadbufferx(L,source.data(),source.size(),path,"t")!=LUA_OK||lua_pcall(L,0,results,0)!=LUA_OK){std::cerr<<lua_tostring(L,-1)<<"\n";return false;}
  return true;
}
int main(int argc,char** argv) {
  if(argc==1){std::cout<<"PASS Lua policy fixture runner built; no external policy executed\n";return 0;}
  if(argc!=3){std::cerr<<"expected owned policy and fixture paths\n";return 2;}
  auto* L=luaL_newstate();if(!L)return 2;
  luaL_requiref(L,"_G",luaopen_base,1);lua_pop(L,1);
  luaL_requiref(L,LUA_TABLIBNAME,luaopen_table,1);lua_pop(L,1);
  luaL_requiref(L,LUA_STRLIBNAME,luaopen_string,1);lua_pop(L,1);
  luaL_requiref(L,LUA_MATHLIBNAME,luaopen_math,1);lua_pop(L,1);
  for(const char* name:{"dofile","loadfile","load","collectgarbage"}){lua_pushnil(L);lua_setglobal(L,name);}
  if(!Load(L,argv[1],1)){lua_close(L);return 1;}lua_setglobal(L,"POLICY");
  const bool ok=Load(L,argv[2],0);lua_close(L);return ok?0:1;
}
