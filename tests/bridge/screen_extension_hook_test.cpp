// SPDX-License-Identifier: GPL-3.0-or-later
// Exercises the production hook using owned data and callable x86 helper fixtures.
#include "api/vehicle/definition_registry.h"
#include "api/vehicle/ui_library_injection.h"
#include "cc2x.h"
#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace {
struct GameString { char* data; std::uint32_t size, capacity; };
struct GameScript { GameString name, source; };
struct GameScriptVector { GameScript* data; std::uint32_t capacity, first, size; };
static_assert(sizeof(GameScriptVector) == 16 && sizeof(GameScript) == 24);

cc2x_mid_fn hook = nullptr;
std::vector<std::wstring> activePackages;
std::vector<void*> thunkAllocations;
void* appendThunk = nullptr;
void* reserveThunk = nullptr;
void* freeThunk = nullptr;
int patchCount = 0;
bool rejectHelper = false;

GameScript* __fastcall Append(GameScriptVector* vector, void*) {
  if (vector->size == vector->capacity) {
    const auto capacity = std::max<std::uint32_t>(4, vector->capacity * 2);
    auto* data = static_cast<GameScript*>(std::calloc(capacity, sizeof(GameScript)));
    if (!data) return nullptr;
    for (std::uint32_t i = 0; i < vector->size; ++i)
      data[i] = vector->data[(vector->first + i) % vector->capacity];
    std::free(vector->data);
    vector->data = data;
    vector->capacity = capacity;
    vector->first = 0;
  }
  auto* item = vector->data + (vector->first + vector->size) % vector->capacity;
  std::memset(item, 0, sizeof(*item));
  ++vector->size;
  return item;
}
void __fastcall Reserve(GameString* string, void*, std::uint32_t length) {
  auto* data = static_cast<char*>(std::realloc(string->data, length));
  if (data) { string->data = data; string->capacity = length; }
}
void __cdecl Free(void* pointer) { std::free(pointer); }

// Production validates the original helper entry bytes before calling it.
// Each owned thunk executes that exact prologue, reverses its stack/register
// effects, then tail-jumps to a fixture helper with the same calling convention.
void* Thunk(std::initializer_list<unsigned char> prefix,
            std::initializer_list<unsigned char> restore, std::uintptr_t target) {
  std::vector<unsigned char> code(prefix);
  code.insert(code.end(), restore.begin(), restore.end());
  code.push_back(0xB8); // mov eax, target; jmp eax
  const auto address = static_cast<std::uint32_t>(target);
  const auto* bytes = reinterpret_cast<const unsigned char*>(&address);
  code.insert(code.end(), bytes, bytes + sizeof(address));
  code.push_back(0xFF); code.push_back(0xE0);
  void* memory = VirtualAlloc(nullptr, code.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!memory) return nullptr;
  std::memcpy(memory, code.data(), code.size());
  DWORD previous = 0;
  if (!VirtualProtect(memory, code.size(), PAGE_EXECUTE_READ, &previous) ||
      !FlushInstructionCache(GetCurrentProcess(), memory, code.size())) {
    VirtualFree(memory, 0, MEM_RELEASE); return nullptr;
  }
  thunkAllocations.push_back(memory);
  return memory;
}
std::uintptr_t Va(std::uint32_t address) {
  static const std::uint8_t mismatchedHelper[] = {0x90, 0x90, 0x90, 0x90, 0x90};
  if (address == 0x00858740 && rejectHelper)
    return reinterpret_cast<std::uintptr_t>(mismatchedHelper);
  if (address == 0x00858740) return reinterpret_cast<std::uintptr_t>(appendThunk);
  if (address == 0x00412B30) return reinterpret_cast<std::uintptr_t>(reserveThunk);
  if (address == 0x00C4C722) return reinterpret_cast<std::uintptr_t>(freeThunk);
  return 0;
}
void Begin(const char*) { patchCount = 0; hook = nullptr; }
int Mid(std::uint32_t address, std::uint32_t size, const std::uint8_t* expected,
        cc2x_mid_fn callback, void*) {
  constexpr std::uint8_t bytes[] = {0x8D, 0x5F, 0x74, 0x8D, 0x45, 0xC4};
  if (address != 0x005B555B || size != sizeof(bytes) ||
      std::memcmp(bytes, expected, sizeof(bytes))) return 0;
  hook = callback; ++patchCount; return 1;
}
int Commit() { return 1; }
void Abort() { hook = nullptr; }
void Log(const char*, int, const char*, ...) {}
int ActiveCount() { return static_cast<int>(activePackages.size()); }
int ActivePath(int index, wchar_t* output, std::uint32_t capacity) {
  if (index < 0 || static_cast<std::size_t>(index) >= activePackages.size() ||
      capacity <= activePackages[index].size()) return 0;
  std::wcscpy(output, activePackages[index].c_str()); return 1;
}
void Write(const std::filesystem::path& path, const std::string& contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << contents;
}
void Set(GameString* string, const std::string& value) {
  string->data = static_cast<char*>(std::malloc(value.size() + 1));
  std::memcpy(string->data, value.c_str(), value.size() + 1);
  string->size = static_cast<std::uint32_t>(value.size());
  string->capacity = string->size + 1;
}
GameScript& At(GameScriptVector& vector, std::uint32_t index) {
  return vector.data[(vector.first + index) % vector.capacity];
}
GameScriptVector Scripts(const char* screen) {
  GameScriptVector vector{};
  vector.capacity = 3; vector.first = 2; vector.size = 2;
  vector.data = static_cast<GameScript*>(std::calloc(vector.capacity, sizeof(GameScript)));
  Set(&At(vector, 0).name, "scripts/library_fixture.lua");
  Set(&At(vector, 0).source, "fixture_library = true\n");
  Set(&At(vector, 1).name, screen);
  Set(&At(vector, 1).source, "fixture_sequence = 'stock'\nfunction begin() end\n");
  return vector;
}
void Release(GameScriptVector& vector) {
  for (std::uint32_t i = 0; i < vector.size; ++i) {
    auto& script = At(vector, i);
    std::free(script.name.data); std::free(script.source.data);
  }
  std::free(vector.data); vector = {};
}
void Invoke(GameScriptVector& vector) {
  cc2x_cpu cpu{};
  cpu.ebp = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&vector) + 0x3C);
  hook(&cpu, nullptr);
}
bool ExecuteOwnedChunks(GameScriptVector& vector) {
  lua_State* state = luaL_newstate();
  if (!state) return false;
  bool valid = true;
  for (std::uint32_t i = 0; valid && i < vector.size; ++i) {
    auto& script = At(vector, i);
    valid = luaL_loadbufferx(state, script.source.data, script.source.size,
        script.name.data, "t") == LUA_OK && lua_pcall(state, 0, 0, 0) == LUA_OK;
  }
  lua_getglobal(state, "fixture_sequence");
  const char* sequence = lua_tostring(state, -1);
  valid = valid && sequence && std::strcmp(sequence, "stock:first:second") == 0;
  lua_close(state); return valid;
}
} // namespace

#define CHECK(condition) do { if (!(condition)) { std::cerr << "FAIL " << __LINE__ << ": " << #condition << '\n'; return 1; } } while (0)
int main() {
  using namespace cc2::api::vehicle;
  CHECK(Registry().Count() == 0);
  appendThunk = Thunk({0x55, 0x8B, 0xEC, 0x6A, 0xFF},
      {0x83, 0xC4, 0x04, 0x5D}, reinterpret_cast<std::uintptr_t>(&Append));
  reserveThunk = Thunk({0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x53, 0x8B, 0xD9, 0x57},
      {0x5F, 0x5B, 0x5D}, reinterpret_cast<std::uintptr_t>(&Reserve));
  freeThunk = Thunk({0x8B, 0xFF, 0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x85, 0xC0},
      {0x5D}, reinterpret_cast<std::uintptr_t>(&Free));
  CHECK(appendThunk && reserveThunk && freeThunk);

  wchar_t temp[MAX_PATH]{};
  CHECK(GetTempPathW(MAX_PATH, temp));
  const auto root = std::filesystem::path(temp) /
      (L"cc2x_screen_hook_" + std::to_wstring(GetCurrentProcessId()));
  for (const auto* name : {L"first", L"second"}) {
    const auto package = root / name;
    Write(package / "mod.xml", "<data/>");
    Write(package / "content/cc2x/screen_extensions.json",
        R"({"schema":1,"extensions":[{"screen":"scripts/screen_menu_main.lua","source":"cc2x/ui/owned.lua"}]})");
    Write(package / "content/cc2x/ui/owned.lua", name == std::wstring(L"first") ?
        "fixture_sequence = fixture_sequence .. ':first'\n" :
        "fixture_sequence = fixture_sequence .. ':second'\n");
    activePackages.push_back(package.wstring());
  }
  cc2x_api api{};
  api.api_version = 8; api.size = sizeof(api); api.module_base = 0x00400000;
  api.game_build = "1.5.18-win32-r2"; api.va = Va; api.log = Log;
  api.patch_begin = Begin; api.patch_mid = Mid; api.patch_commit = Commit; api.patch_abort = Abort;
  api.active_mod_count = ActiveCount; api.active_mod_path = ActivePath;
  api.game_build = "unknown";
  CHECK(!InstallStockUiLibraryInjection(&api) && !hook);
  api.game_build = "1.5.18-win32-r2";
  CHECK(InstallStockUiLibraryInjection(&api) && patchCount == 1 && hook);

  rejectHelper = true;
  auto mismatched = Scripts("scripts/screen_menu_main.lua");
  Invoke(mismatched);
  CHECK(mismatched.size == 2); Release(mismatched);
  rejectHelper = false;
  auto unrelated = Scripts("scripts/screen_menu_options.lua");
  Invoke(unrelated);
  CHECK(unrelated.size == 2); Release(unrelated);
  auto suffix = Scripts("other/scripts/screen_menu_main.lua");
  Invoke(suffix);
  CHECK(suffix.size == 2); Release(suffix);
  auto main = Scripts("scripts/screen_menu_main.lua");
  Invoke(main);
  CHECK(Registry().Count() == 0 && main.size == 4);
  CHECK(main.capacity == 6 && main.first == 0); // Wrapped ring grew during the second append.
  CHECK(std::strcmp(At(main, 0).name.data, "scripts/library_fixture.lua") == 0);
  CHECK(std::strcmp(At(main, 1).name.data, "scripts/screen_menu_main.lua") == 0);
  CHECK(std::strcmp(At(main, 2).source.data, "fixture_sequence = fixture_sequence .. ':first'\n") == 0);
  CHECK(std::strcmp(At(main, 3).source.data, "fixture_sequence = fixture_sequence .. ':second'\n") == 0);
  CHECK(ExecuteOwnedChunks(main)); Release(main);

  // Files are copied for the process snapshot; no per-frame file execution/read.
  std::filesystem::remove_all(root);
  auto cached = Scripts("scripts/screen_menu_main.lua");
  Invoke(cached);
  CHECK(cached.size == 4 && ExecuteOwnedChunks(cached)); Release(cached);
  for (auto* memory : thunkAllocations) VirtualFree(memory, 0, MEM_RELEASE);
  std::cout << "PASS production screen hook, zero vehicles, exact target, ring growth, copied activation and execution order\n";
}
