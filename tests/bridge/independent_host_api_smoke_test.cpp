extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include <cstdio>
#include <cstring>

namespace {
bool Run(lua_State* state, const char* source) {
  if (luaL_loadstring(state, source) != LUA_OK) return false;
  if (lua_pcall(state, 0, 0, 0) != LUA_OK) {
    const char* message = lua_tostring(state, -1);
    std::fprintf(stderr, "Lua fixture setup failed: %s\n", message ? message : "unknown");
    return false;
  }
  return true;
}

bool HasLog(lua_State* state, const char* needle) {
  lua_getglobal(state, "__cc2x_smoke_logs");
  const int logs = lua_gettop(state);
  const auto count = lua_rawlen(state, logs);
  bool found = false;
  for (lua_Integer i = 1; i <= static_cast<lua_Integer>(count); ++i) {
    lua_rawgeti(state, logs, i);
    const char* line = lua_tostring(state, -1);
    if (line && std::strstr(line, needle)) found = true;
    lua_pop(state, 1);
  }
  lua_pop(state, 1);
  return found;
}

bool Call(lua_State* state, const char* method, int arguments) {
  lua_getglobal(state, "__cc2x_smoke_module");
  lua_getfield(state, -1, method);
  if (arguments) lua_pushnumber(state, 1.0 / 30.0);
  if (lua_pcall(state, arguments, 0, 0) != LUA_OK) {
    const char* message = lua_tostring(state, -1);
    std::fprintf(stderr, "%s failed: %s\n", method, message ? message : "unknown");
    lua_settop(state, 0);
    return false;
  }
  lua_settop(state, 0);
  return true;
}
}

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  lua_State* state = luaL_newstate();
  if (!state) return 3;
  luaL_openlibs(state);
  const char* mocks = R"lua(
    __cc2x_smoke_logs = {}
    function print(...) local p={} for i=1,select('#',...) do p[i]=tostring(select(i,...)) end; table.insert(__cc2x_smoke_logs,table.concat(p,' ')) end
    cc2={
      bridge={is_available=true,abi_version=function() return 1 end,build_id=function() return 'test-build' end,is_ready=function() return true end},
      world={generation=function() return 2 end,tick=function() return 17 end,
        vehicles=function() return {{id=1,definition=0}} end,
        tiles=function() return {{id=20}} end},
      vehicles={list=function() return {{identity='test.fighter',ready=true}} end}
    }
  )lua";
  bool ok = Run(state, mocks);
  if (ok) {
    const int status = luaL_loadfilex(state, argv[1], "t");
    if (status != LUA_OK) {
      const char* message = lua_tostring(state, -1);
      std::fprintf(stderr, "Lua syntax error (%s): %s\n", argv[1], message ? message : "unknown");
      ok = false;
    } else if (lua_pcall(state, 0, 1, 0) != LUA_OK || !lua_istable(state, -1)) {
      const char* message = lua_tostring(state, -1);
      std::fprintf(stderr, "Lua fixture load failed: %s\n", message ? message : "entry did not return a table");
      ok = false;
    } else {
      lua_setglobal(state, "__cc2x_smoke_module");
    }
  }
  if (ok) ok = Call(state, "on_load", 0);
  if (ok) ok = Call(state, "on_update", 1);
  for (int i = 1; ok && i < 300; ++i) ok = Call(state, "on_update", 1);
  if (ok) {
    ok = HasLog(state, "CC2X_INDEPENDENT_API_LOAD_PASS") &&
        HasLog(state, "CC2X_INDEPENDENT_API_PASS generation=2 tick=17 vehicles=1 tiles=1 definitions=1") &&
        HasLog(state, "CC2X_INDEPENDENT_API_UPDATE_PASS calls=1") &&
        HasLog(state, "CC2X_INDEPENDENT_API_UPDATE_PASS calls=300");
  }
  lua_close(state);
  if (!ok) return 1;
  std::puts("PASS independent Lua API smoke fixture: read-only SDK shape and repeated update markers");
  return 0;
}
