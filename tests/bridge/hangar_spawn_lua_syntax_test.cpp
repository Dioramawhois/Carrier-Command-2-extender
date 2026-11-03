extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
#include <cstdio>

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  lua_State* state = luaL_newstate();
  if (!state) return 3;
  int status = LUA_OK;
  for (int i=1;i<argc&&status==LUA_OK;++i) {
    status=luaL_loadfilex(state,argv[i],"t");
    if(status!=LUA_OK) {
      const char* message=lua_tostring(state,-1);
      std::fprintf(stderr,"Lua syntax error (%s): %s\n",argv[i],message?message:"unknown");
    } else {
      lua_pop(state,1);
      std::printf("PASS Lua syntax: %s\n",argv[i]);
    }
  }
  lua_close(state);
  return status == LUA_OK ? 0 : 1;
}
