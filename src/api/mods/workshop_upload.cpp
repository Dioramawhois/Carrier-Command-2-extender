#include "workshop_upload.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

namespace cc2::api::mods {
namespace {
constexpr char kSupportedBuild[] = "1.5.18-win32-r2";
constexpr std::uint32_t kCopyEntryCallVa = 0x0064542f;
constexpr std::uint8_t kExpectedCall[] = {0xe8, 0x5c, 0x01, 0x00, 0x00};
constexpr std::uint8_t kExpectedPreparationCall[] = {0xe8, 0x6c, 0x62, 0xdd, 0xff};
constexpr std::uint8_t kExpectedValidator[] = {0xb9, 0xa0, 0x05, 0x00, 0x00};
constexpr std::uint8_t kAcceptExtension[] = {0xeb, 0x43, 0x90, 0x90, 0x90};
volatile LONG g_state = 0; // 0=not attempted, 1=installed, -1=failed/unsupported
const cc2x_api* g_api = nullptr;
std::uintptr_t g_moduleBase = 0;
SRWLOCK g_tempRootLock = SRWLOCK_INIT;
std::wstring g_tempRoot;

struct NativePath {
  const char* data;
  std::uint32_t length;
};

using NativeFileCopy = bool(__stdcall*)(const NativePath*, const NativePath*, const NativePath*);
using NativePreparation = int(__thiscall*)(void*);
NativeFileCopy g_originalFileCopy = nullptr;
NativePreparation g_originalPreparation = nullptr;

void Log(int level, const char* event, const char* reason, const char* detail = "") {
  if (g_api && g_api->log)
    g_api->log("native_bridge.workshop_upload", level,
      "event=%s build=%s va=0x%08lx reason=%s detail=%s",
      event, g_api->game_build ? g_api->game_build : "unknown",
      static_cast<unsigned long>(kCopyEntryCallVa), reason, detail);
}

bool ReadPath(const NativePath* path, std::string* out) {
  if (!path || !out) return false;
  NativePath view{};
  SIZE_T read = 0;
  if (!ReadProcessMemory(GetCurrentProcess(), path, &view, sizeof(view), &read) ||
      read != sizeof(view) || !view.data || view.length == 0 || view.length > 32767)
    return false;
  out->resize(view.length);
  if (!ReadProcessMemory(GetCurrentProcess(), view.data, out->data(), view.length, &read) ||
      read != view.length) return false;
  if (out->find('\0') != std::string::npos) return false;
  for (char& ch : *out) if (ch == '/') ch = '\\';
  return true;
}

std::wstring WidePath(const std::string& path) {
  if (path.empty() || path.size() > 32767) return {};
  const int length = MultiByteToWideChar(CP_ACP, 0, path.data(),
      static_cast<int>(path.size()), nullptr, 0);
  if (length <= 0) return {};
  std::wstring wide(length, L'\0');
  if (MultiByteToWideChar(CP_ACP, 0, path.data(), static_cast<int>(path.size()),
      wide.data(), length) != length) return {};
  return wide;
}

std::wstring Canonical(const std::wstring& path) {
  const DWORD size = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
  if (!size || size > 32768) return {};
  std::wstring full(size, L'\0');
  const DWORD written = GetFullPathNameW(path.c_str(), size, full.data(), nullptr);
  if (!written || written >= size) return {};
  full.resize(written);
  for (auto& ch : full) if (ch == L'/') ch = L'\\';
  return full;
}

bool InsideUploadTemp(const std::wstring& destination) {
  AcquireSRWLockShared(&g_tempRootLock);
  const auto root = g_tempRoot;
  ReleaseSRWLockShared(&g_tempRootLock);
  if (root.empty()) return false;
  auto canonicalRoot = Canonical(root);
  const auto canonicalTarget = Canonical(destination);
  if (canonicalRoot.empty() || canonicalTarget.empty()) return false;
  while (canonicalRoot.size() > 3 && canonicalRoot.back() == L'\\')
    canonicalRoot.pop_back();
  canonicalRoot += L'\\';
  return canonicalTarget.size() > canonicalRoot.size() &&
      CompareStringOrdinal(canonicalTarget.c_str(), static_cast<int>(canonicalRoot.size()),
          canonicalRoot.c_str(), static_cast<int>(canonicalRoot.size()), TRUE) == CSTR_EQUAL;
}

std::string Lower(std::string value) {
  for (char& ch : value) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  }
  return value;
}

bool IsInsideContent(const std::string& path) {
  const auto lower = Lower(path);
  return lower.find("\\content\\") != std::string::npos &&
      lower.find("\\temp\\content\\") == std::string::npos;
}

bool EnsureDirectoryTree(const std::string& directory) {
  if (directory.empty()) return false;
  std::string path = directory;
  for (std::size_t i = 3; i < path.size(); ++i) {
    if (path[i] != '\\') continue;
    const auto prefix = path.substr(0, i);
    if (prefix.size() <= 3) continue;
    if (!CreateDirectoryA(prefix.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
      return false;
  }
  if (!CreateDirectoryA(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
    return false;
  const DWORD attributes = GetFileAttributesA(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool EnsureParentDirectory(const std::string& filePath) {
  const auto separator = filePath.find_last_of('\\');
  if (separator == std::string::npos) return false;
  return EnsureDirectoryTree(filePath.substr(0, separator));
}

std::string ContentRoot(const std::string& path) {
  const auto lower = Lower(path);
  const auto at = lower.find("\\content\\");
  if (at == std::string::npos) return {};
  return path.substr(0, at + 8); // includes the "content" segment
}

std::string PackageRoot(const std::string& contentRoot) {
  if (contentRoot.size() < 8) return {};
  return contentRoot.substr(0, contentRoot.size() - 8);
}

bool IsReservedPackageEntry(const char* name) {
  return _stricmp(name, ".") == 0 || _stricmp(name, "..") == 0 ||
      _stricmp(name, "content") == 0 || _stricmp(name, "temp") == 0 ||
      _stricmp(name, "mod.xml") == 0 || _stricmp(name, "thumbnail.png") == 0;
}

bool CopyTree(const std::string& source, const std::string& destination, unsigned depth) {
  if (depth > 64) return false;
  WIN32_FIND_DATAA entry{};
  const std::string pattern = source + "\\*";
  HANDLE find = FindFirstFileA(pattern.c_str(), &entry);
  if (find == INVALID_HANDLE_VALUE) return false;
  bool ok = true;
  do {
    if (std::strcmp(entry.cFileName, ".") == 0 || std::strcmp(entry.cFileName, "..") == 0)
      continue;
    const std::string from = source + "\\" + entry.cFileName;
    const std::string to = destination + "\\" + entry.cFileName;
    if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) { ok = false; break; }
    if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      if (!EnsureDirectoryTree(to) || !CopyTree(from, to, depth + 1)) { ok = false; break; }
    } else {
      if (!EnsureParentDirectory(to) || !CopyFileA(from.c_str(), to.c_str(), FALSE)) { ok = false; break; }
    }
  } while (FindNextFileA(find, &entry));
  const DWORD lastError = GetLastError();
  FindClose(find);
  return ok && lastError == ERROR_NO_MORE_FILES;
}

bool CopyPackageRootSidecars(const std::string& sourceContentRoot,
                             const std::string& destinationContentRoot) {
  const std::string sourceRoot = sourceContentRoot;
  const std::string destinationRoot = destinationContentRoot;
  if (sourceRoot.empty() || destinationRoot.empty()) return false;
  WIN32_FIND_DATAA entry{};
  const std::string pattern = sourceRoot + "\\*";
  HANDLE find = FindFirstFileA(pattern.c_str(), &entry);
  if (find == INVALID_HANDLE_VALUE) return false;
  bool ok = true;
  do {
    if (IsReservedPackageEntry(entry.cFileName)) continue;
    const std::string source = sourceRoot + "\\" + entry.cFileName;
    const std::string destination = destinationRoot + "\\" + entry.cFileName;
    if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) { ok = false; break; }
    if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      if (!EnsureDirectoryTree(destination) || !CopyTree(source, destination, 0)) { ok = false; break; }
    } else if (!EnsureParentDirectory(destination) ||
               !CopyFileA(source.c_str(), destination.c_str(), FALSE)) {
      ok = false;
      break;
    }
  } while (FindNextFileA(find, &entry));
  const DWORD lastError = GetLastError();
  FindClose(find);
  return ok && lastError == ERROR_NO_MORE_FILES;
}

BOOL __stdcall WorkshopCopyEntry(const NativePath* source, const NativePath* destination,
                                 const NativePath* relative) {
  if (!g_originalFileCopy) return FALSE;
  // Keep the stock worker first: its successful copies and all surrounding
  // Workshop transaction checks remain authoritative. The fallback applies
  // only to a rejected file and only within this upload's own temp directory.
  if (g_originalFileCopy(source, destination, relative)) return TRUE;
  std::string rawSource, rawDestination;
  if (!ReadPath(source, &rawSource) || !ReadPath(destination, &rawDestination))
    return FALSE;
  const auto sourcePath = WidePath(rawSource);
  const auto destinationPath = WidePath(rawDestination);
  if (sourcePath.empty() || destinationPath.empty() || !InsideUploadTemp(destinationPath)) {
    Log(CC2X_LOG_WARN, "WORKSHOP_CONTENT_PATH_REJECTED", "outside_upload_temp");
    return FALSE;
  }
  std::error_code error;
  std::filesystem::create_directories(std::filesystem::path(destinationPath).parent_path(), error);
  if (error || !CopyFileW(sourcePath.c_str(), destinationPath.c_str(), FALSE)) {
    Log(CC2X_LOG_WARN, "WORKSHOP_CONTENT_COPY_FAILED", "fallback_copy_failed", rawSource.c_str());
    return FALSE;
  }
  Log(CC2X_LOG_INFO, "WORKSHOP_CONTENT_FALLBACK_COPIED", "stock_rejected_file", rawSource.c_str());
  return TRUE;
}

int __fastcall CapturePreparation(void* stringObject, void*) {
  if (!g_originalPreparation) return 0;
  std::string raw;
  const auto root = ReadPath(static_cast<const NativePath*>(stringObject), &raw)
      ? WidePath(raw) : std::wstring{};
  AcquireSRWLockExclusive(&g_tempRootLock);
  g_tempRoot = root;
  ReleaseSRWLockExclusive(&g_tempRootLock);
  return g_originalPreparation(stringObject);
}

bool MakeRelativeCall(std::uint32_t va, const void* target, std::uint8_t bytes[5]) {
  if (!target) return false;
  const auto source = static_cast<std::uint32_t>(
      g_moduleBase + (va - 0x00400000u));
  const auto destination = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target));
  const auto displacement = destination - (source + 5u);
  bytes[0] = 0xe8;
  std::memcpy(bytes + 1, &displacement, sizeof(displacement));
  return true;
}
} // namespace

bool InstallWorkshopUploadSupport(const cc2x_api* api) {
  if (InterlockedCompareExchange(&g_state, 0, 0) == 1) return true;
  if (!api || api->api_version < 5 ||
      api->size < offsetof(cc2x_api, patch_bytes) + sizeof(api->patch_bytes) ||
      !api->patch_begin || !api->patch_bytes || !api->patch_commit || !api->patch_abort ||
      !api->module_base || !api->va) {
    InterlockedExchange(&g_state, -1);
    return false;
  }
  if (!api->game_build || std::strcmp(api->game_build, kSupportedBuild) != 0) {
    InterlockedExchange(&g_state, -1);
    if (api->log) api->log("native_bridge.workshop_upload", CC2X_LOG_WARN,
      "event=WORKSHOP_UPLOAD_SUPPORT_UNAVAILABLE build=%s reason=unsupported_build",
      api->game_build ? api->game_build : "unknown");
    return false;
  }

  g_api = api;
  g_moduleBase = static_cast<std::uintptr_t>(api->module_base);
  g_originalFileCopy = reinterpret_cast<NativeFileCopy>(api->va(0x00645590u));
  g_originalPreparation = reinterpret_cast<NativePreparation>(api->va(0x0041ADD0u));
  std::uint8_t replacementCopy[5]{}, replacementPreparation[5]{};
  if (!MakeRelativeCall(kCopyEntryCallVa, reinterpret_cast<const void*>(&WorkshopCopyEntry), replacementCopy) ||
      !MakeRelativeCall(0x00644B5Fu, reinterpret_cast<const void*>(&CapturePreparation), replacementPreparation)) {
    InterlockedExchange(&g_state, -1);
    return false;
  }

  api->patch_begin("native_bridge.workshop_upload");
  if (!api->patch_bytes(0x00644A02u, sizeof(kExpectedValidator),
          kExpectedValidator, kAcceptExtension)) {
    api->patch_abort();
    InterlockedExchange(&g_state, -1);
    Log(CC2X_LOG_WARN, "WORKSHOP_UPLOAD_SUPPORT_UNAVAILABLE", "validator_signature_mismatch");
    return false;
  }
  if (!api->patch_bytes(kCopyEntryCallVa, sizeof(kExpectedCall), kExpectedCall, replacementCopy)) {
    api->patch_abort();
    InterlockedExchange(&g_state, -1);
    Log(CC2X_LOG_WARN, "WORKSHOP_UPLOAD_SUPPORT_UNAVAILABLE", "content_copy_callsite_mismatch");
    return false;
  }
  if (!api->patch_bytes(0x00644B5Fu, sizeof(kExpectedPreparationCall),
          kExpectedPreparationCall, replacementPreparation)) {
    api->patch_abort();
    InterlockedExchange(&g_state, -1);
    Log(CC2X_LOG_WARN, "WORKSHOP_UPLOAD_SUPPORT_UNAVAILABLE", "preparation_callsite_mismatch");
    return false;
  }
  if (!api->patch_commit()) {
    api->patch_abort();
    InterlockedExchange(&g_state, -1);
    Log(CC2X_LOG_WARN, "WORKSHOP_UPLOAD_SUPPORT_UNAVAILABLE", "patch_commit_failed");
    return false;
  }
  InterlockedExchange(&g_state, 1);
  Log(CC2X_LOG_INFO, "WORKSHOP_UPLOAD_SUPPORT_INSTALLED", "stock_copy_then_guarded_fallback");
  return true;
}

} // namespace cc2::api::mods
