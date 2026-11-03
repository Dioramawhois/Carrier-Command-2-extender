#include "mod_loader.h"

#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <map>
#include <set>
#include <string_view>
#include <cwctype>
#include <cwchar>

namespace cc2::host {
namespace {

struct JsonValue {
  enum class Kind { String, Object, Array, Other } kind = Kind::Other;
  std::wstring string;
  std::wstring primitive;
  std::vector<std::pair<std::wstring, JsonValue>> object;
  std::vector<JsonValue> array;
};

class JsonParser {
 public:
  explicit JsonParser(std::wstring input) : input_(std::move(input)) {}
  bool Parse(JsonValue* value, std::wstring* error) {
    Skip();
    if (!Value(value, 0)) return Fail(error, L"invalid JSON value");
    Skip();
    if (position_ != input_.size()) return Fail(error, L"trailing JSON data");
    return true;
  }

 private:
  bool Fail(std::wstring* error, const wchar_t* why) {
    if (error) *error = why;
    return false;
  }
  void Skip() {
    while (position_ < input_.size() && (input_[position_] == L' ' || input_[position_] == L'\t' ||
           input_[position_] == L'\r' || input_[position_] == L'\n')) ++position_;
  }
  bool Hex4(unsigned* value) {
    if (position_ + 4 > input_.size()) return false;
    unsigned result = 0;
    for (int i = 0; i < 4; ++i) {
      const wchar_t c = input_[position_++];
      result <<= 4;
      if (c >= L'0' && c <= L'9') result |= c - L'0';
      else if (c >= L'a' && c <= L'f') result |= c - L'a' + 10;
      else if (c >= L'A' && c <= L'F') result |= c - L'A' + 10;
      else return false;
    }
    *value = result;
    return true;
  }
  bool String(std::wstring* out) {
    if (position_ >= input_.size() || input_[position_++] != L'"') return false;
    while (position_ < input_.size()) {
      wchar_t c = input_[position_++];
      if (c == L'"') return true;
      if (c < 0x20) return false;
      if (c != L'\\') { out->push_back(c); continue; }
      if (position_ == input_.size()) return false;
      c = input_[position_++];
      switch (c) {
        case L'"': case L'\\': case L'/': out->push_back(c); break;
        case L'b': out->push_back(L'\b'); break;
        case L'f': out->push_back(L'\f'); break;
        case L'n': out->push_back(L'\n'); break;
        case L'r': out->push_back(L'\r'); break;
        case L't': out->push_back(L'\t'); break;
        case L'u': {
          unsigned cp = 0;
          if (!Hex4(&cp)) return false;
          if (cp >= 0xd800 && cp <= 0xdbff) {
            if (position_ + 2 > input_.size() || input_[position_] != L'\\' || input_[position_ + 1] != L'u') return false;
            position_ += 2;
            unsigned low = 0;
            if (!Hex4(&low) || low < 0xdc00 || low > 0xdfff) return false;
            cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
          } else if (cp >= 0xdc00 && cp <= 0xdfff) return false;
          if (cp <= 0xffff) out->push_back(static_cast<wchar_t>(cp));
          else {
            cp -= 0x10000;
            out->push_back(static_cast<wchar_t>(0xd800 + (cp >> 10)));
            out->push_back(static_cast<wchar_t>(0xdc00 + (cp & 0x3ff)));
          }
          break;
        }
        default: return false;
      }
    }
    return false;
  }
  bool Value(JsonValue* out, unsigned depth) {
    if (depth > 32 || position_ >= input_.size()) return false;
    Skip();
    if (position_ >= input_.size()) return false;
    if (input_[position_] == L'"') {
      out->kind = JsonValue::Kind::String;
      return String(&out->string);
    }
    if (input_[position_] == L'{') {
      out->kind = JsonValue::Kind::Object;
      ++position_; Skip();
      if (position_ < input_.size() && input_[position_] == L'}') { ++position_; return true; }
      for (;;) {
        Skip(); std::wstring key;
        if (!String(&key)) return false;
        Skip(); if (position_ >= input_.size() || input_[position_++] != L':') return false;
        JsonValue child;
        if (!Value(&child, depth + 1)) return false;
        out->object.emplace_back(std::move(key), std::move(child));
        Skip(); if (position_ >= input_.size()) return false;
        const wchar_t separator = input_[position_++];
        if (separator == L'}') return true;
        if (separator != L',') return false;
      }
    }
    if (input_[position_] == L'[') {
      out->kind = JsonValue::Kind::Array;
      ++position_; Skip();
      if (position_ < input_.size() && input_[position_] == L']') { ++position_; return true; }
      for (;;) {
        JsonValue child;
        if (!Value(&child, depth + 1)) return false;
        out->array.push_back(std::move(child));
        Skip(); if (position_ >= input_.size()) return false;
        const wchar_t separator = input_[position_++];
        if (separator == L']') return true;
        if (separator != L',') return false;
      }
    }
    const auto start = position_;
    if (input_.compare(position_, 4, L"true") == 0) position_ += 4;
    else if (input_.compare(position_, 5, L"false") == 0) position_ += 5;
    else if (input_.compare(position_, 4, L"null") == 0) position_ += 4;
    else {
      if (input_[position_] == L'-') ++position_;
      if (position_ >= input_.size()) return false;
      if (input_[position_] == L'0') ++position_;
      else {
        if (input_[position_] < L'1' || input_[position_] > L'9') return false;
        while (position_ < input_.size() && input_[position_] >= L'0' && input_[position_] <= L'9') ++position_;
      }
      if (position_ < input_.size() && input_[position_] == L'.') {
        ++position_; const auto fraction = position_;
        while (position_ < input_.size() && input_[position_] >= L'0' && input_[position_] <= L'9') ++position_;
        if (fraction == position_) return false;
      }
      if (position_ < input_.size() && (input_[position_] == L'e' || input_[position_] == L'E')) {
        ++position_;
        if (position_ < input_.size() && (input_[position_] == L'+' || input_[position_] == L'-')) ++position_;
        const auto exponent = position_;
        while (position_ < input_.size() && input_[position_] >= L'0' && input_[position_] <= L'9') ++position_;
        if (exponent == position_) return false;
      }
    }
    if (position_ == start) return false;
    out->kind = JsonValue::Kind::Other;
    out->primitive = input_.substr(start, position_ - start);
    return true;
  }
  std::wstring input_;
  std::size_t position_ = 0;
};

std::wstring Utf8ToWide(const std::string& input) {
  if (input.empty()) return {};
  const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0);
  if (!count) return {};
  std::wstring output(count, L'\0');
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), output.data(), count)) return {};
  return output;
}

bool ReadUtf8File(const std::wstring& path, std::size_t maxBytes, std::wstring* text, std::wstring* error) {
  std::ifstream file(std::filesystem::path(path), std::ios::binary|std::ios::ate);
  if (!file) { if (error) *error = L"file could not be opened"; return false; }
  if(file.tellg()<0 || file.tellg()>static_cast<std::streamoff>(maxBytes)) { if(error)*error=L"file exceeds size limit";return false; }
  file.seekg(0);
  // Keep the read bounded even if another writer grows the file after tellg().
  std::string bytes(maxBytes + 1, '\0');
  file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  bytes.resize(static_cast<std::size_t>(file.gcount()));
  if (bytes.size() > maxBytes) { if (error) *error = L"file exceeds size limit"; return false; }
  if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xef &&
      static_cast<unsigned char>(bytes[1]) == 0xbb && static_cast<unsigned char>(bytes[2]) == 0xbf) bytes.erase(0, 3);
  *text = Utf8ToWide(bytes);
  if (!bytes.empty() && text->empty()) { if (error) *error = L"file is not valid UTF-8"; return false; }
  return true;
}

bool ReadManifest(const std::wstring& path, JsonValue* root, std::wstring* error) {
  std::wstring text;
  if (!ReadUtf8File(path, 64 * 1024, &text, error)) return false;
  return JsonParser(std::move(text)).Parse(root, error);
}

const JsonValue* Field(const JsonValue& object, const wchar_t* name) {
  if (object.kind != JsonValue::Kind::Object) return nullptr;
  const JsonValue* found = nullptr;
  for (const auto& item : object.object) {
    if (item.first == name) {
      if (found) return nullptr;
      found = &item.second;
    }
  }
  return found;
}

bool SafeId(const std::wstring& id) {
  if (id.empty() || id.size() > 64) return false;
  bool component = false;
  for (wchar_t c : id)
    if (c == L'.') { if (!component) return false; component = false; }
    else if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
             (c >= L'0' && c <= L'9') || c == L'_' || c == L'-') component = true;
    else return false;
  return component;
}

bool SafeEntry(const std::wstring& entry) {
  if (entry.empty() || entry.size() > 240 || entry[0] == L'/' || entry[0] == L'\\' ||
      entry.find(L':') != std::wstring::npos || entry.find(L'\\') != std::wstring::npos ||
      entry.size() < 5 || entry.substr(entry.size() - 4) != L".lua") return false;
  std::size_t start = 0;
  while (start <= entry.size()) {
    const auto end = entry.find(L'/', start);
    const auto part = entry.substr(start, end == std::wstring::npos ? end : end - start);
    if (part.empty() || part == L"." || part == L"..") return false;
    if (end == std::wstring::npos) break;
    start = end + 1;
  }
  return true;
}

bool SafeLuaName(const std::wstring& value) {
  if (value.empty() || value.size() > 160) return false;
  bool needsComponent = true;
  for (wchar_t c : value) {
    if (c == L'.') { if (needsComponent) return false; needsComponent = true; continue; }
    if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'_' || c == L'-')) return false;
    needsComponent = false;
  }
  return !needsComponent;
}

bool IsReparsePath(const std::wstring& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

bool ValidateEntryPath(const std::wstring& root, const std::wstring& entry) {
  std::wstring current = root;
  std::size_t start = 0;
  while (start < entry.size()) {
    const auto end = entry.find(L'/', start);
    current += L"\\" + entry.substr(start, end == std::wstring::npos ? end : end - start);
    if (IsReparsePath(current)) return false;
    if (end == std::wstring::npos) break;
    start = end + 1;
  }
  const DWORD attrs = GetFileAttributesW(current.c_str());
  return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool SafeImagePath(const std::wstring& path) {
  if (path.empty() || path.size() > 240 || path.front() == L'/' ||
      path.front() == L'\\' || path.find(L':') != std::wstring::npos ||
      path.find(L'\\') != std::wstring::npos) return false;
  std::size_t start = 0;
  while (start < path.size()) {
    const auto end = path.find(L'/', start);
    const auto part = path.substr(start, end == std::wstring::npos ? end : end - start);
    if (part.empty() || part == L"." || part == L"..") return false;
    if (end == std::wstring::npos) break;
    start = end + 1;
  }
  return path.size() >= 5 && _wcsicmp(path.c_str() + path.size() - 4, L".png") == 0;
}

bool UniqueJsonKeys(const JsonValue& value) {
  if (value.kind == JsonValue::Kind::Array) {
    for (const auto& child : value.array) if (!UniqueJsonKeys(child)) return false;
  } else if (value.kind == JsonValue::Kind::Object) {
    std::set<std::wstring> keys;
    for (const auto& [key, child] : value.object)
      if (!keys.insert(key).second || !UniqueJsonKeys(child)) return false;
  }
  return true;
}

bool ParseUiImages(const JsonValue* ui, const std::wstring& root,
                   std::vector<UiImageDescriptor>* output, std::wstring* error) {
  if (!ui) return true;
  if (ui->kind != JsonValue::Kind::Object) {
    *error = L"ui must be an object"; return false;
  }
  for (const auto& category : ui->object) {
    const bool weapon = category.first == L"weapons";
    if (!weapon && category.first != L"vehicles") {
      *error = L"unknown ui category: " + category.first; return false;
    }
    if (category.second.kind != JsonValue::Kind::Object) {
      *error = L"ui category must be an object"; return false;
    }
    for (const auto& item : category.second.object) {
      if (!SafeId(item.first) || item.second.kind != JsonValue::Kind::Object) {
        *error = L"invalid ui image identity or descriptor"; return false;
      }
      UiImageDescriptor image;
      image.identity = item.first;
      image.weapon = weapon;
      for (const auto& field : item.second.object) {
        std::wstring* target = field.first == L"small" ? &image.small :
            field.first == L"large" ? &image.large :
            field.first == L"loadout" && !weapon ? &image.loadout : nullptr;
        if (weapon && field.first == L"type" &&
            field.second.kind == JsonValue::Kind::String &&
            SafeId(field.second.string)) {
          auto engineType = field.second.string;
          std::transform(engineType.begin(), engineType.end(), engineType.begin(),
                         [](wchar_t c) { return std::towlower(c); });
          if (engineType.rfind(L"attachment_", 0) == 0) {
            image.engineType = std::move(engineType);
            continue;
          }
        }
        if (!target || field.second.kind != JsonValue::Kind::String ||
            !SafeImagePath(field.second.string) ||
            !ValidateEntryPath(root, field.second.string)) {
          *error = L"invalid or unsafe ui image: " + item.first + L"." + field.first;
          return false;
        }
        *target = field.second.string;
      }
      if (image.large.empty()) {
        *error = L"ui image requires large PNG: " + item.first; return false;
      }
      if (weapon && image.engineType.empty()) {
        *error = L"weapon ui image requires attachment type: " + item.first;
        return false;
      }
      output->push_back(std::move(image));
    }
  }
  return true;
}

std::wstring CanonicalPath(std::wstring path) {
  std::replace(path.begin(), path.end(), L'/', L'\\');
  DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
  if (!required) return {};
  std::wstring full(required, L'\0');
  const DWORD written = GetFullPathNameW(path.c_str(), required, full.data(), nullptr);
  if (!written || written >= required) return {};
  full.resize(written);
  while (full.size() > 3 && (full.back() == L'\\' || full.back() == L'/')) full.pop_back();
  return full;
}


bool ParseHook(const JsonValue& json, HookDescriptor* hook);
void ParsePackage(const std::wstring& package,ModSource source,std::vector<ModDescriptor>*candidates,std::vector<std::wstring>*diagnostics) {
  const auto label=package.substr(package.find_last_of(L"\\/")+1);
  if(IsReparsePath(package)||!ValidateEntryPath(package,L"mod.xml")) {
    diagnostics->push_back(label+L": STOCK_MOD_XML_MISSING_OR_UNSAFE");return;
  }
    const std::wstring contentRoot = package + L"\\content";
    std::wstring manifestRoot = contentRoot;
    std::wstring manifestPath = contentRoot + L"\\bridge.json";
    const std::wstring nativeRoot = package + L"\\content\\native_bridge";
    if (GetFileAttributesW(manifestPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
      manifestRoot = nativeRoot;
      manifestPath = nativeRoot + L"\\bridge.json";
      if (GetFileAttributesW(manifestPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        manifestRoot = package;
        manifestPath = package + L"\\bridge.json";
        if (GetFileAttributesW(manifestPath.c_str()) == INVALID_FILE_ATTRIBUTES) return;
      }
    }
    std::wstring relativeManifest = manifestPath.substr(package.size() + 1);
    std::replace(relativeManifest.begin(), relativeManifest.end(), L'\\', L'/');
    if (!ValidateEntryPath(package, relativeManifest)) {
      diagnostics->push_back(label + L": MANIFEST_FAIL: unsafe manifest path"); return;
    }
    JsonValue json; std::wstring error;
    if (!ReadManifest(manifestPath, &json, &error) || json.kind != JsonValue::Kind::Object ||
        !UniqueJsonKeys(json)) {
      if (error.empty()) error = L"root must be an object with unique keys";
      diagnostics->push_back(label + std::wstring(L": MANIFEST_FAIL: ") + error);
      return;
    }
    const auto* schema = Field(json, L"schema");
    const auto* id = Field(json, L"id");
    const auto* name = Field(json, L"name");
    const auto* version = Field(json, L"version");
    const auto* runtime = Field(json, L"runtime");
    const auto* entry = Field(json, L"entry");
    const auto* scripts = Field(json, L"scripts");
    const auto* hooks = Field(json, L"hooks");
    const auto* ui = Field(json, L"ui");
    const bool legacy = schema && schema->kind == JsonValue::Kind::Other && schema->primitive == L"1";
    const bool modern = !schema || (schema->kind == JsonValue::Kind::Other && schema->primitive == L"2");
    // Older packages sometimes copied bridge.json into content/ without
    // rebasing the original package-relative "content/bridge/main.lua" entry.
    // Preserve that exact legacy spelling; modern manifests stay content-relative.
    const std::wstring scriptRoot = legacy && manifestRoot == contentRoot && entry &&
        entry->kind == JsonValue::Kind::String && entry->string.rfind(L"content/",0)==0
        ? package : manifestRoot;
    if ((!legacy && !modern) || !id || id->kind != JsonValue::Kind::String ||
        (name && name->kind != JsonValue::Kind::String) ||
        (version && version->kind != JsonValue::Kind::String) ||
        (runtime && runtime->kind != JsonValue::Kind::String) ||
        (entry && entry->kind != JsonValue::Kind::String) ||
        (hooks && hooks->kind != JsonValue::Kind::Array) ||
        (scripts && scripts->kind != JsonValue::Kind::Array) ||
        (legacy && (!name || !version || !runtime || !entry || !hooks))) {
      diagnostics->push_back(label + std::wstring(L": MANIFEST_FAIL: invalid schema or field types"));
      return;
    }
    if (!SafeId(id->string) || (name && (name->string.empty() || name->string.size() > 128)) ||
        (version && (version->string.empty() || version->string.size() > 64)) ||
        (runtime && runtime->string != L"bridge") ||
        (entry && (!SafeEntry(entry->string) || !ValidateEntryPath(scriptRoot, entry->string)))) {
      diagnostics->push_back(label + std::wstring(L": MANIFEST_FAIL: invalid id/name/version/runtime/entry or unsafe entry path"));
      return;
    }

    ModDescriptor mod;
    mod.schema = legacy ? 1u : 2u;
    mod.id = id->string; mod.name = name ? name->string : id->string;
    mod.version = version ? version->string : L"unspecified";
    std::transform(mod.id.begin(),mod.id.end(),mod.id.begin(),towlower);
    mod.packagePath = package; mod.rootPath = scriptRoot; mod.source = source;
    if (entry) mod.bridgeScripts.push_back(entry->string);
    if (scripts) {
      if (scripts->kind != JsonValue::Kind::Array || scripts->array.size() > 256) {
        diagnostics->push_back(label + std::wstring(L": MANIFEST_FAIL: scripts must be an array with at most 256 paths"));
        return;
      }
      bool badScript = false;
      for (const auto& value : scripts->array) {
        if (value.kind != JsonValue::Kind::String || !SafeEntry(value.string) ||
            !ValidateEntryPath(scriptRoot, value.string)) {
          diagnostics->push_back(label + std::wstring(L": MANIFEST_FAIL: invalid or unsafe script declaration"));
          badScript = true;
          break;
        }
        if (std::any_of(mod.bridgeScripts.begin(), mod.bridgeScripts.end(), [&](const auto& prior) {
              return _wcsicmp(prior.c_str(), value.string.c_str()) == 0;
            })) {
          diagnostics->push_back(label + std::wstring(L": MANIFEST_FAIL: duplicate Bridge script declaration"));
          badScript = true;
          break;
        }
        mod.bridgeScripts.push_back(value.string);
      }
      if (badScript) return;
    }
    if (mod.bridgeScripts.empty()) {
      diagnostics->push_back(label + std::wstring(L": MANIFEST_FAIL: at least one script or entry is required"));
      return;
    }
    mod.entryPath = scriptRoot + L"\\";
    for (wchar_t c : mod.bridgeScripts.front()) mod.entryPath.push_back(c == L'/' ? L'\\' : c);
    if (!ParseUiImages(ui, manifestRoot, &mod.uiImages, &error)) {
      diagnostics->push_back(label + std::wstring(L": MANIFEST_FAIL: ") + error);
      return;
    }
    if (hooks && hooks->array.size() > 256) {
      diagnostics->push_back(label + std::wstring(L": MANIFEST_FAIL: hook count exceeds 256"));
      return;
    }
    std::set<std::wstring> hookKeys;
    std::size_t replaceCount = 0;
    if (hooks) for (const auto& value : hooks->array) {
      HookDescriptor hook;
      if (!ParseHook(value, &hook)) {
        mod.id.clear();
        diagnostics->push_back(label + std::wstring(L": MANIFEST_FAIL: malformed hook descriptor"));
        break;
      }
      const auto modeName = hook.mode == HookMode::Before ? L"before" :
          hook.mode == HookMode::After ? L"after" :
          hook.mode == HookMode::Replace ? L"replace" : L"around";
      const std::wstring key = hook.target + L"\x1f" + modeName + L"\x1f" + hook.handler;
      if (!hookKeys.insert(key).second) {
        mod.id.clear();
        diagnostics->push_back(label + std::wstring(L": MANIFEST_FAIL: duplicate hook callback descriptor"));
        break;
      }
      if (hook.target == L"bridge.update" && hook.mode == HookMode::Replace && ++replaceCount > 1) {
        mod.id.clear();
        diagnostics->push_back(label + std::wstring(L": MANIFEST_FAIL: multiple bridge.update replace handlers"));
        break;
      }
      mod.hooks.push_back(std::move(hook));
    }
    if (mod.id.empty()) return;

    mod.activation=source==ModSource::Workshop?ModActivation::WorkshopActive:ModActivation::StockActive;
    for (const auto& script : mod.bridgeScripts) {
      std::wstring full = scriptRoot + L"\\" + script;
      std::replace(full.begin(),full.end(),L'/',L'\\');
      std::wstring relative=CanonicalPath(full).substr(package.size()+1);
      std::transform(relative.begin(),relative.end(),relative.begin(),towlower);
      if(relative.rfind(L"content\\scripts\\",0)==0) {
        diagnostics->push_back(mod.id+L": STOCK_SCRIPT_BLOCKED: use separate Native Bridge files");return;
      }
    }
    if(IsReparsePath(manifestRoot) || IsReparsePath(scriptRoot)) {
      diagnostics->push_back(mod.id+L": unsafe manifest or script root");return;}
    candidates->push_back(std::move(mod));
}

std::wstring LuaQuote(const std::wstring& value) {
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  std::string utf8(bytes, '\0');
  if (bytes) WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), utf8.data(), bytes, nullptr, nullptr);
  std::wstring out = L"\"";
  for (unsigned char c : utf8) {
    if (c == '"' || c == '\\') { out.push_back(L'\\'); out.push_back(static_cast<wchar_t>(c)); }
    else if (c == '\n') out += L"\\n";
    else if (c == '\r') out += L"\\r";
    else if (c == '\t') out += L"\\t";
    else if (c < 0x20 || c >= 0x7f) { wchar_t escaped[5]{}; swprintf_s(escaped, L"\\%03u", static_cast<unsigned>(c)); out += escaped; }
    else out.push_back(static_cast<wchar_t>(c));
  }
  out.push_back(L'"');
  return out;
}

bool EnsureDirectory(const std::wstring& path) {
  if (CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) return true;
  const auto slash = path.find_last_of(L"\\/");
  return slash != std::wstring::npos && EnsureDirectory(path.substr(0, slash)) &&
      (CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS);
}

bool ParseHook(const JsonValue& json, HookDescriptor* hook) {
  const auto* target = Field(json, L"target");
  if (!target) target = Field(json, L"event");
  const auto* mode = Field(json, L"mode");
  const auto* handler = Field(json, L"handler");
  if (!target || !handler || target->kind != JsonValue::Kind::String ||
      (mode && mode->kind != JsonValue::Kind::String) || handler->kind != JsonValue::Kind::String ||
      target->string!=L"bridge.update" || !SafeLuaName(handler->string)) return false;
  hook->target = target->string;
  hook->handler = handler->string;
  if (!mode || mode->string == L"after") hook->mode = HookMode::After;
  else if (mode->string == L"before") hook->mode = HookMode::Before;
  else if (mode->string == L"replace") hook->mode = HookMode::Replace;
  else if (mode->string == L"around") hook->mode = HookMode::Around;
  else return false;
  return true;
}

}  // namespace

ModDiscovery DiscoverActiveBridgeMods(const std::vector<std::wstring>& activePackages) {
  ModDiscovery result;
  bool explicitOrder = false;
  std::vector<std::wstring> seen;
  for (const auto& package : activePackages) {
    const auto canonical=CanonicalPath(package);
    if(canonical.empty())continue;
    if (std::any_of(seen.begin(),seen.end(),[&](const auto& p){return _wcsicmp(p.c_str(),canonical.c_str())==0;})) continue;
    seen.push_back(canonical);
    std::wstring lower=canonical;
    std::transform(lower.begin(),lower.end(),lower.begin(),towlower);
    const auto source = lower.find(L"\\workshop\\content\\")!=std::wstring::npos ? ModSource::Workshop : ModSource::Local;
    const std::wstring indexRelative = L"content/cc2x-modules/index.json";
    const auto indexPath = canonical + L"\\content\\cc2x-modules\\index.json";
    if (GetFileAttributesW(indexPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
      ParsePackage(canonical,source,&result.mods,&result.diagnostics);
      continue;
    }
    explicitOrder = true;
    JsonValue index; std::wstring error;
    std::vector<ModDescriptor> bundled;
    bool valid = !IsReparsePath(canonical) && ValidateEntryPath(canonical,L"mod.xml") &&
        ValidateEntryPath(canonical,indexRelative) && ReadManifest(indexPath,&index,&error) &&
        index.kind == JsonValue::Kind::Object && UniqueJsonKeys(index) && index.object.size() == 2;
    const auto* schema = Field(index,L"schema");
    const auto* packages = Field(index,L"packages");
    valid = valid && schema && schema->kind == JsonValue::Kind::Other && schema->primitive == L"1" &&
        packages && packages->kind == JsonValue::Kind::Array && packages->array.size() <= 256;
    std::set<std::wstring> keys;
    if (valid) for (const auto& entry : packages->array) {
      if (entry.kind != JsonValue::Kind::String || !SafeId(entry.string)) { valid = false; break; }
      std::wstring key = entry.string;
      std::transform(key.begin(),key.end(),key.begin(),towlower);
      if (!keys.insert(key).second) { valid = false; break; }
      const auto child = canonical + L"\\content\\cc2x-modules\\" + entry.string;
      if (IsReparsePath(child) || GetFileAttributesW((child + L"\\content\\cc2x-modules\\index.json").c_str()) != INVALID_FILE_ATTRIBUTES) {
        valid = false; break;
      }
      const auto previous = bundled.size();
      ParsePackage(child,source,&bundled,&result.diagnostics);
      if (bundled.size() != previous + 1) { valid = false; break; }
    }
    if (!valid) {
      result.diagnostics.push_back(canonical + L": MODULE_BUNDLE_FAIL: invalid, unsafe or incomplete module index");
      continue;
    }
    for (auto& mod : bundled) result.mods.push_back(std::move(mod));
  }
  if (!explicitOrder) std::sort(result.mods.begin(),result.mods.end(),[](const auto&a,const auto&b){return a.id<b.id;});
  std::map<std::wstring,std::size_t> counts;
  for (const auto& mod : result.mods) ++counts[mod.id];
  std::vector<ModDescriptor> unique;
  for (auto& mod : result.mods) {
    if (counts[mod.id] == 1) unique.push_back(std::move(mod));
    else if (counts[mod.id] > 1) {
      result.diagnostics.push_back(mod.id+L": DUPLICATE_ACTIVE_ID: all colliding packages skipped");
      counts[mod.id] = 0;
    }
  }
  result.mods=std::move(unique);return result;
}

bool ResolveModAssetPath(const ModDescriptor& mod,const std::wstring& relative,
                         const wchar_t* extension,std::wstring* absolute) {
  if (!absolute || !extension || relative.empty() || relative.size() > 240 ||
      relative.front() == L'/' || relative.front() == L'\\' ||
      relative.find(L':') != std::wstring::npos ||
      relative.find(L'\\') != std::wstring::npos) return false;
  const std::size_t suffix = std::wcslen(extension);
  if (relative.size() <= suffix ||
      _wcsicmp(relative.c_str() + relative.size() - suffix, extension) != 0) return false;
  std::wstring current = mod.rootPath;
  std::size_t start = 0;
  while (start < relative.size()) {
    const auto end = relative.find(L'/', start);
    const auto part = relative.substr(start, end == std::wstring::npos ? end : end - start);
    if (part.empty() || part == L"." || part == L"..") return false;
    current += L"\\" + part;
    if (IsReparsePath(current)) return false;
    if (end == std::wstring::npos) break;
    start = end + 1;
  }
  const DWORD attrs = GetFileAttributesW(current.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) return false;
  *absolute = std::move(current);
  return true;
}

bool WriteBridgeBootstrap(const ModDescriptor& mod, const std::wstring& sdkPath,
                          const std::wstring& runtimeRoot, std::wstring* bootstrapPath,
                          std::wstring* error) {
  const auto directory = runtimeRoot + L"\\" + mod.id;
  if (!EnsureDirectory(directory)) { if (error) *error = L"cannot create runtime directory"; return false; }
  const auto path = directory + L"\\bootstrap.lua";
  std::wstring hookTable = L"local hooks={\n";
  std::size_t hookIndex = 0;
  for (const auto& hook : mod.hooks) {
    const wchar_t* mode = hook.mode == HookMode::Before ? L"before" :
        hook.mode == HookMode::After ? L"after" :
        hook.mode == HookMode::Replace ? L"replace" : L"around";
    hookTable += L"  {target=" + LuaQuote(hook.target) + L",mode=" + LuaQuote(mode) +
        L",handler=" + LuaQuote(hook.handler) + L",index=" + std::to_wstring(hookIndex++) + L"},\n";
  }
  hookTable += L"}\n";
  const std::wstring source =
      L"local root=" + LuaQuote(mod.rootPath) + L"\n" +
      L"local entry=" + LuaQuote(mod.entryPath) + L"\n" +
      L"local sdkPath=" + LuaQuote(sdkPath) + L"\n" +
      L"local forbidden=" + LuaQuote(_wcsicmp(mod.rootPath.c_str(),mod.packagePath.c_str())==0 ? L"content/scripts/" :
          (_wcsicmp(mod.rootPath.c_str(),(mod.packagePath+L"\\content").c_str())==0 ? L"scripts/" : L"")) + L"\n" +
      hookTable +
      L"local cache, loading = {}, {}\n" +
      L"local function safe_name(n) return type(n)=='string' and #n>0 and #n<=160 and not n:match('(^%.)') and not n:match('%.%.') and n:match('^[%w_%.%-]+$') end\n" +
      L"function require(name)\n" +
      L"  if not safe_name(name) then error('invalid module name',2) end\n" +
      L"  if cache[name]~=nil then return cache[name] end\n" +
      L"  if loading[name] then error('cyclic require: '..name,2) end\n" +
      L"  loading[name]=true\n" +
      L"  local rel=name:gsub('%.','/')\n" +
      L"  if #forbidden>0 and rel:lower():sub(1,#forbidden)==forbidden then loading[name]=nil; error('stock Lua resource cannot be loaded by Bridge',2) end\n" +
      L"  local chunk,err=loadfile(root..'/'..rel..'.lua')\n" +
      L"  if not chunk then chunk,err=loadfile(root..'/'..rel..'/init.lua') end\n" +
      L"  if not chunk then loading[name]=nil; error(err,2) end\n" +
      L"  local ok,value=pcall(chunk)\n" +
      L"  loading[name]=nil\n" +
      L"  if not ok then error(value,2) end\n" +
      L"  if value==nil then value=true end\n" +
      L"  cache[name]=value\n" +
      L"  __cc2_host_require_count=(__cc2_host_require_count or 0)+1\n" +
      L"  return value\n" +
      L"end\n" +
      L"local sdk=dofile(sdkPath)\n" +
      L"if type(sdk)~='table' or type(sdk.carrier)~='table' or type(sdk.animation)~='table' then error('invalid cc2 SDK') end\n" +
      L"if type(sdk.carrier.throttle)~='function' or type(sdk.animation.routes)~='function' then error('SDK API shape mismatch') end\n" +
      L"cc2=sdk; __cc2_host_sdk_pass=true\n" +
      L"cache.cc2=sdk\n" +
      L"local mod=dofile(entry)\n" +
      L"if type(mod)~='table' then error('entry must return a lifecycle table') end\n" +
      L"__cc2_host_entry_pass=true\n" +
      L"__cc2_host_require_pass=(__cc2_host_require_count or 0)>0\n" +
      L"local function report_handler(index,ok,err) if type(cc2_bridge_host_handler_event)=='function' then local token=index+(ok and 0 or 2147483648); pcall(cc2_bridge_host_handler_event,token,err) end end\n" +
      L"local function invoke_handler(index,fn,...) local ok,a,b,c=pcall(fn,...); report_handler(index,ok,not ok and tostring(a) or nil); if ok then return a,b,c end; return nil end\n" +
      L"if mod.on_load~=nil then if type(mod.on_load)~='function' then error('on_load must be a function') end; local ok,err=pcall(mod.on_load); report_handler(#hooks+1,ok); if not ok then error(err,2) end; __cc2_host_on_load_pass=true else __cc2_host_on_load_pass=false end\n" +
      L"if mod.on_update==nil then mod.on_update=function() end elseif type(mod.on_update)~='function' then error('on_update must be a function') end\n" +
      L"if mod.on_unload~=nil and type(mod.on_unload)~='function' then error('on_unload must be a function') end\n" +
      L"local function resolve_handler(name) local value=mod; for part in name:gmatch('[^%.]+') do if type(value)~='table' then return nil end; value=value[part] end; if type(value)~='function' then return nil end; return value end\n" +
      L"for _,hook in ipairs(hooks) do if not resolve_handler(hook.handler) then error('hook handler is not a function: '..hook.handler,2) end end\n" +
      L"__cc2_host_hooks_pass=true\n" +
      L"local function run_hooks(target,mode,...) for _,hook in ipairs(hooks) do if hook.target==target and hook.mode==mode then local fn=resolve_handler(hook.handler); if fn then invoke_handler(hook.index,fn,...) end end end end\n" +
      L"local function update_base(dt) return invoke_handler(#hooks,mod.on_update,dt) end\n" +
      L"local function run_around(list,index,dt) if index>#list then return update_base(dt) end; local hook=list[index]; local fn=resolve_handler(hook.handler); if not fn then return run_around(list,index+1,dt) end; return invoke_handler(hook.index,fn,function(nextDt) return run_around(list,index+1,nextDt==nil and dt or nextDt) end,dt) end\n" +
      L"function __cc2_host_update(dt) run_hooks('bridge.update','before',dt); local replace,around={},{}; for _,hook in ipairs(hooks) do if hook.target=='bridge.update' then if hook.mode=='replace' then replace[#replace+1]=hook elseif hook.mode=='around' then around[#around+1]=hook end end end; if #replace==1 then local fn=resolve_handler(replace[1].handler); if fn then invoke_handler(replace[1].index,fn,dt) end else if #around>0 then run_around(around,1,dt) else update_base(dt) end end; run_hooks('bridge.update','after',dt) end\n" +
      L"function __cc2_host_unload() if mod.on_unload then local ok,err=pcall(mod.on_unload); report_handler(#hooks+2,ok); if not ok then error(err,2) end end end\n";
  std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
  if (!file) { if (error) *error = L"cannot write bootstrap"; return false; }
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, source.data(), static_cast<int>(source.size()), nullptr, 0, nullptr, nullptr);
  std::string utf8(bytes, '\0');
  if (!bytes || !WideCharToMultiByte(CP_UTF8, 0, source.data(), static_cast<int>(source.size()), utf8.data(), bytes, nullptr, nullptr)) {
    if (error) *error = L"bootstrap UTF-8 conversion failed"; return false;
  }
  file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
  if (!file) { if (error) *error = L"bootstrap write failed"; return false; }
  if (bootstrapPath) *bootstrapPath = path;
  return true;
}

}  // namespace cc2::host
