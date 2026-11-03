#include "atlas_factory.h"

#include "api/ui/second_atlas_contract.h"

#include <Windows.h>
#include <bcrypt.h>
#include <wincodec.h>

#ifdef small
#undef small
#endif
#ifdef large
#undef large
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace cc2::api::ui {
namespace {

constexpr char kFactoryVersion[] = "cc2x-atlas-factory-v3";
constexpr char kAtlasVirtualPath[] = "textures/cc2x_runtime/index.atlas";
constexpr std::uint32_t kRegionTag = 0x40000000u;
constexpr std::uint32_t kFirstDynamicRegion = 5;
constexpr std::uint32_t kMaxRegions = 16384;

struct ImagePlan {
  std::wstring owner;
  std::wstring identity;
  std::wstring engineType;
  std::wstring role;
  std::wstring sourcePath;
  std::string sourceRelative;
  std::string logicalId;
  std::string virtualPath;
  std::string regionName;
  std::string sha256;
  std::uint32_t token = 0;
  std::uint32_t expectedSize = 0;
  bool generateSmall = false;
  float bounds[4]{};
};

struct Bitmap {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> rgba;
};

template <typename T>
struct ComOwner {
  T* value = nullptr;
  ~ComOwner() { if (value) value->Release(); }
  T** put() { if (value) { value->Release(); value = nullptr; } return &value; }
  T* operator->() const { return value; }
};

std::wstring Lower(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
    return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + 32) : c;
  });
  return value;
}

std::string Utf8(const std::wstring& value) {
  const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (!length) return {};
  std::string result(static_cast<std::size_t>(length), '\0');
  if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), result.data(), length, nullptr, nullptr)) return {};
  return result;
}

std::string Hex(const std::uint8_t* bytes, std::size_t length) {
  constexpr char digits[] = "0123456789abcdef";
  std::string output(length * 2, '0');
  for (std::size_t i = 0; i < length; ++i) {
    output[i * 2] = digits[bytes[i] >> 4];
    output[i * 2 + 1] = digits[bytes[i] & 15];
  }
  return output;
}

bool HashBytes(const std::uint8_t* bytes, std::size_t length, std::string* output) {
  if (!output || (!bytes && length)) return false;
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::array<std::uint8_t, 32> digest{};
  bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
            BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
  std::size_t offset = 0;
  while (ok && offset < length) {
    const auto chunk = static_cast<ULONG>(std::min<std::size_t>(length - offset, 1u << 20));
    ok = BCryptHashData(hash, const_cast<PUCHAR>(bytes + offset), chunk, 0) >= 0;
    offset += chunk;
  }
  if (ok) ok = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
  if (hash) BCryptDestroyHash(hash);
  if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  if (ok) *output = Hex(digest.data(), digest.size());
  return ok;
}

bool ReadBytes(const std::wstring& path, std::vector<std::uint8_t>* bytes,
               std::size_t maxBytes = 32u * 1024u * 1024u) {
  if (!bytes) return false;
  std::ifstream file(std::filesystem::path(path), std::ios::binary | std::ios::ate);
  if (!file) return false;
  const auto end = file.tellg();
  if (end <= 0 || static_cast<std::uint64_t>(end) > maxBytes) return false;
  bytes->resize(static_cast<std::size_t>(end));
  file.seekg(0);
  file.read(reinterpret_cast<char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
  return static_cast<bool>(file);
}

bool HashFile(const std::wstring& path, std::string* digest) {
  std::vector<std::uint8_t> bytes;
  return ReadBytes(path, &bytes) && HashBytes(bytes.data(), bytes.size(), digest);
}

bool ReadPng(const std::wstring& path, Bitmap* output, std::string* error) {
  if (!output) return false;
  ComOwner<IWICImagingFactory> factory;
  ComOwner<IWICBitmapDecoder> decoder;
  ComOwner<IWICBitmapFrameDecode> frame;
  ComOwner<IWICFormatConverter> converter;
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS(factory.put()));
  if (SUCCEEDED(hr)) hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr,
      GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.put());
  if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, frame.put());
  if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(converter.put());
  if (SUCCEEDED(hr)) hr = converter->Initialize(frame.value, GUID_WICPixelFormat32bppRGBA,
      WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
  UINT width = 0, height = 0;
  if (SUCCEEDED(hr)) hr = converter->GetSize(&width, &height);
  if (FAILED(hr) || !width || !height || width > 4096 || height > 4096) {
    if (error) *error = "png_decode_or_dimensions_invalid";
    return false;
  }
  const std::uint64_t bytes = static_cast<std::uint64_t>(width) * height * 4u;
  if (bytes > 64u * 1024u * 1024u) {
    if (error) *error = "png_decoded_size_limit_exceeded";
    return false;
  }
  output->width = width;
  output->height = height;
  output->rgba.resize(static_cast<std::size_t>(bytes));
  hr = converter->CopyPixels(nullptr, width * 4u, static_cast<UINT>(bytes), output->rgba.data());
  if (FAILED(hr)) {
    if (error) *error = "png_pixel_decode_failed";
    return false;
  }
  return true;
}

bool MakeSmallFromLarge(const Bitmap& source, Bitmap* output) {
  if (!output || source.width != 64 || source.height != 64) return false;
  std::uint32_t x0 = 64, y0 = 64, x1 = 0, y1 = 0;
  bool found = false;
  for (std::uint32_t y = 0; y < 64; ++y) for (std::uint32_t x = 0; x < 64; ++x) {
    if (source.rgba[(y * 64 + x) * 4 + 3] == 0) continue;
    found = true;
    x0 = std::min(x0, x); y0 = std::min(y0, y);
    x1 = std::max(x1, x + 1); y1 = std::max(y1, y + 1);
  }
  if (!found) return false;
  const std::uint32_t sw = x1 - x0, sh = y1 - y0;
  const float scale = 14.0f / static_cast<float>(std::max(sw, sh));
  const auto dw = std::max(1u, static_cast<std::uint32_t>(sw * scale + 0.5f));
  const auto dh = std::max(1u, static_cast<std::uint32_t>(sh * scale + 0.5f));
  // Odd-width silhouettes cannot be centered on the 16px grid without a
  // fractional pixel. Keep the measured left-side placement: the follow-up
  // game test found the alternate (right-tied) placement one pixel too far.
  const auto left = (16u - dw) / 2u, top = (16u - dh) / 2u;
  output->width = output->height = 16;
  output->rgba.assign(16u * 16u * 4u, 0);
  for (std::uint32_t y = 0; y < dh; ++y) {
    const auto sy = y0 + std::min(sh - 1, ((2 * y + 1) * sh) / (2 * dh));
    for (std::uint32_t x = 0; x < dw; ++x) {
      const auto sx = x0 + std::min(sw - 1, ((2 * x + 1) * sw) / (2 * dw));
      const auto src = (sy * 64 + sx) * 4;
      const auto dst = ((top + y) * 16 + left + x) * 4;
      std::memcpy(output->rgba.data() + dst, source.rgba.data() + src, 4);
    }
  }
  return true;
}

bool WriteFile(const std::wstring& path, const std::vector<std::uint8_t>& bytes) {
  std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
  if (!file) return false;
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  file.flush();
  return static_cast<bool>(file);
}

std::vector<std::uint8_t> EncodeTxtr(const Bitmap& bitmap) {
  if ((bitmap.width != 16 && bitmap.width != 64) || bitmap.width != bitmap.height ||
      bitmap.rgba.size() != static_cast<std::size_t>(bitmap.width) * bitmap.height * 4u)
    return {};
  // Header bytes match compiler-produced single-mip RGBA TXTRs in the
  // checked-in fixture; width, height, and payload block count are variable.
  std::vector<std::uint8_t> result(23u + bitmap.rgba.size(), 0);
  const std::uint8_t header[23] = {
    'T','X','T','R', 2,0,0,0, 2, 0,0, 0,0, 0, 1,1, 0,0,0,0, 0,0,0
  };
  std::memcpy(result.data(), header, sizeof(header));
  result[9] = static_cast<std::uint8_t>(bitmap.width);
  result[11] = static_cast<std::uint8_t>(bitmap.height);
  const auto blocks = static_cast<std::uint16_t>(bitmap.rgba.size() / 256u);
  result[20] = static_cast<std::uint8_t>(blocks & 0xffu);
  result[21] = static_cast<std::uint8_t>(blocks >> 8);
  std::size_t out = 23;
  for (std::uint32_t y = bitmap.height; y-- > 0;) {
    const auto row = static_cast<std::size_t>(y) * bitmap.width * 4u;
    std::memcpy(result.data() + out, bitmap.rgba.data() + row,
        static_cast<std::size_t>(bitmap.width) * 4u);
    out += static_cast<std::size_t>(bitmap.width) * 4u;
  }
  return result;
}

void AppendU32(std::vector<std::uint8_t>* out, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) out->push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}

void AppendF32(std::vector<std::uint8_t>* out, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  AppendU32(out, bits);
}

bool AppendString(std::vector<std::uint8_t>* out, const std::string& value) {
  if (value.empty() || value.size() > 255 || value.find('\0') != std::string::npos) return false;
  out->push_back(static_cast<std::uint8_t>(value.size()));
  out->push_back(0);
  out->insert(out->end(), value.begin(), value.end());
  return true;
}

std::vector<std::uint8_t> EncodeAtlas(const std::vector<ImagePlan>& plans) {
  std::vector<std::uint8_t> out{'A','T','L','A'};
  AppendU32(&out, 1);
  AppendU32(&out, static_cast<std::uint32_t>(plans.size()));
  for (const auto& plan : plans) {
    if (!AppendString(&out, plan.virtualPath)) return {};
  }
  AppendU32(&out, static_cast<std::uint32_t>(plans.size()));
  for (std::size_t i = 0; i < plans.size(); ++i) {
    const auto& plan = plans[i];
    if (!AppendString(&out, plan.regionName)) return {};
    AppendU32(&out, static_cast<std::uint32_t>(i));
    for (float value : plan.bounds) AppendF32(&out, value);
    for (float value : std::array<float,4>{0.0f, 1.0f, 0.0f, 1.0f}) AppendF32(&out, value);
  }
  return out;
}

bool MakeImagePlan(const host::ModDiscovery& mods, const float smallBounds[4],
                   const float largeBounds[4], std::vector<ImagePlan>* plans,
                   std::string* error) {
  if (!plans) return false;
  for (const auto& mod : mods.mods) for (const auto& image : mod.uiImages) {
    auto add = [&](const std::wstring& role, const std::wstring& relative,
                   std::uint32_t expectedSize, bool generatedSmall) -> bool {
      if (relative.empty()) { if (error) *error = "required_ui_image_path_missing"; return false; }
      std::wstring sourcePath;
      if (!host::ResolveModAssetPath(mod, relative, L".png", &sourcePath)) {
        if (error) *error = "ui_image_path_no_longer_safe";
        return false;
      }
      ImagePlan plan;
      plan.owner = mod.id;
      plan.identity = image.identity;
      plan.engineType = image.engineType;
      plan.role = role;
      plan.sourcePath = std::move(sourcePath);
      plan.sourceRelative = Utf8(relative);
      std::replace(plan.sourceRelative.begin(), plan.sourceRelative.end(), '\\', '/');
      plan.logicalId = std::string(image.weapon ? "weapons/" : "vehicles/") +
          Utf8(image.identity) + "/" + Utf8(role);
      plan.expectedSize = expectedSize;
      plan.generateSmall = generatedSmall;
      std::memcpy(plan.bounds, expectedSize == 16 ? smallBounds : largeBounds,
          sizeof(plan.bounds));
      plans->push_back(std::move(plan));
      return true;
    };
    if (image.weapon) {
      if (!add(L"small", image.small.empty() ? image.large : image.small,
               16, image.small.empty()) ||
          !add(L"large", image.large, 64, false)) return false;
    } else {
      if (!add(L"small", image.small.empty() ? image.large : image.small, 16, image.small.empty()) ||
          !add(L"large", image.large, 64, false) ||
          !add(L"loadout", image.loadout.empty() ? image.large : image.loadout, 64, false)) return false;
    }
  }
  std::sort(plans->begin(), plans->end(), [](const ImagePlan& a, const ImagePlan& b) {
    const auto ao = Lower(a.owner), bo = Lower(b.owner);
    if (ao != bo) return ao < bo;
    const auto ai = Lower(a.identity), bi = Lower(b.identity);
    if (ai != bi) return ai < bi;
    return a.role < b.role;
  });
  if (plans->size() > kMaxRegions) { if (error) *error = "atlas_region_limit_exceeded"; return false; }
  for (std::size_t i = 0; i < plans->size(); ++i) {
    auto& plan = (*plans)[i];
    const auto regionId = static_cast<std::uint32_t>(i + kFirstDynamicRegion);
    plan.token = kRegionTag | regionId;
    char region[32]{};
    sprintf_s(region, "cx%06X", static_cast<unsigned>(regionId));
    plan.regionName = region;
    char virtualPath[96]{};
    sprintf_s(virtualPath, "textures/cc2x_runtime/surface_%08X.txtr", static_cast<unsigned>(i + 1));
    plan.virtualPath = virtualPath;
    if (!HashFile(plan.sourcePath, &plan.sha256)) {
      if (error) *error = "ui_image_hash_failed";
      return false;
    }
  }
  return true;
}

bool ReadVanillaBounds(const std::wstring& path, float small[4], float large[4],
                       std::string* error) {
  std::vector<std::uint8_t> bytes;
  if (!ReadBytes(path, &bytes) || bytes.size() < 16 ||
      !second_atlas::FindAtlasBoundsV1(bytes.data(), bytes.size(), "icon_chassis_16_wing_small", small) ||
      !second_atlas::FindAtlasBoundsV1(bytes.data(), bytes.size(), "icon_chassis_wing_small", large)) {
    if (error) *error = "vanilla_atlas_bounds_unavailable";
    return false;
  }
  return true;
}

bool SafeCreateDirectory(const std::wstring& path) {
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  return !ec && std::filesystem::is_directory(path, ec) && !ec;
}

std::string BuildKey(std::vector<ImagePlan>* plans, const char* gameBuild) {
  std::ostringstream material;
  material << kFactoryVersion << '\n' << (gameBuild ? gameBuild : "unknown") << '\n';
  for (auto& plan : *plans) {
    const auto owner = Utf8(plan.owner), identity = Utf8(plan.identity), role = Utf8(plan.role);
    if (owner.empty() || identity.empty() || plan.sourceRelative.empty()) return {};
    material << owner << '\t' << identity << '\t' << role << '\t' << plan.sourceRelative << '\t'
             << plan.sha256 << '\t' << plan.expectedSize << '\t'
             << (plan.generateSmall ? "alpha-bbox-nearest-14-center-left-tie-v3" : "preserve-rgba-v1") << '\n';
  }
  const auto value = material.str();
  std::string digest;
  if (!HashBytes(reinterpret_cast<const std::uint8_t*>(value.data()), value.size(), &digest)) return {};
  return digest;
}

std::wstring Join(const std::wstring& a, const std::wstring& b) {
  return (std::filesystem::path(a) / std::filesystem::path(b)).wstring();
}

std::map<std::string, std::string> ReadCacheManifest(const std::wstring& path) {
  std::ifstream file(std::filesystem::path(path), std::ios::binary);
  std::map<std::string, std::string> values;
  std::string line;
  while (std::getline(file, line)) {
    const auto split = line.find('=');
    if (split == std::string::npos || !values.emplace(line.substr(0, split), line.substr(split + 1)).second)
      return {};
  }
  return values;
}

bool ValidateCache(const std::wstring& directory, const std::string& key,
                   std::size_t surfaceCount) {
  const auto values = ReadCacheManifest(Join(directory, L"cache.meta"));
  if (values.empty() || values.find("version") == values.end() ||
      values.at("version") != kFactoryVersion || values.find("key") == values.end() ||
      values.at("key") != key) return false;
  const std::size_t expectedCount = surfaceCount + 1;
  if (values.size() != expectedCount + 2) return false;
  for (std::size_t i = 0; i < surfaceCount; ++i) {
    char name[64]{}; sprintf_s(name, "surface_%08X.txtr",
        static_cast<unsigned>(i + kFirstDynamicRegion));
    const auto it = values.find(name);
    std::string hash;
    if (it == values.end() || !HashFile(Join(directory, std::wstring(name, name + std::strlen(name))), &hash) ||
        hash != it->second) return false;
  }
  std::string atlasHash;
  return values.find("index.atlas") != values.end() &&
      HashFile(Join(directory, L"index.atlas"), &atlasHash) &&
      atlasHash == values.at("index.atlas");
}

std::wstring HexId(std::uint32_t id) {
  wchar_t buffer[16]{}; swprintf_s(buffer, L"%08X", id); return buffer;
}

bool BuildSurfaces(const std::vector<ImagePlan>& plans, const std::wstring& directory,
                   std::vector<std::pair<std::string, std::string>>* outputs,
                   std::size_t* generatedSmall, std::string* error) {
  if (generatedSmall) *generatedSmall = 0;
  for (const auto& plan : plans) {
    Bitmap source, converted;
    if (!ReadPng(plan.sourcePath, &source, error)) return false;
    if (plan.generateSmall) {
      if (source.width != 64 || source.height != 64 || !MakeSmallFromLarge(source, &converted)) {
        if (error) *error = "small_fallback_requires_nonempty_64x64_large_icon";
        return false;
      }
      if (generatedSmall) ++*generatedSmall;
    } else {
      if (source.width != plan.expectedSize || source.height != plan.expectedSize) {
        if (error) *error = "ui_png_dimensions_do_not_match_declared_role";
        return false;
      }
      converted = std::move(source);
    }
    bool hasVisiblePixel = false;
    for (std::size_t alpha = 3; alpha < converted.rgba.size(); alpha += 4)
      if (converted.rgba[alpha] != 0) { hasVisiblePixel = true; break; }
    if (!hasVisiblePixel) { if (error) *error = "ui_png_is_fully_transparent"; return false; }
    auto txtr = EncodeTxtr(converted);
    if (txtr.empty()) { if (error) *error = "txtr_encoding_failed"; return false; }
    const std::wstring filename = L"surface_" + HexId(plan.token & ~kRegionTag) + L".txtr";
    const auto path = Join(directory, filename);
    if (!WriteFile(path, txtr)) { if (error) *error = "txtr_write_failed"; return false; }
    std::string hash;
    if (!HashBytes(txtr.data(), txtr.size(), &hash)) { if (error) *error = "txtr_hash_failed"; return false; }
    outputs->push_back({Utf8(filename), std::move(hash)});
  }
  return true;
}

void CleanupStaleCache(const std::wstring& cacheRoot, const std::wstring& keep) {
  std::error_code ec;
  const std::filesystem::path root(cacheRoot);
  const auto canonicalRoot = std::filesystem::weakly_canonical(root, ec);
  if (ec) return;
  for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_directory(ec) || ec) { ec.clear(); continue; }
    const auto name = it->path().filename().wstring();
    if (it->path().wstring() == keep ||
        (name.rfind(L"atlas-", 0) != 0 && name.rfind(L".stage-", 0) != 0)) continue;
    const auto candidate = std::filesystem::weakly_canonical(it->path(), ec);
    if (ec) { ec.clear(); continue; }
    if (candidate.parent_path() == canonicalRoot) {
      std::filesystem::remove_all(candidate, ec);
      ec.clear();
    }
  }
}

bool PublishResources(const std::vector<ImagePlan>& plans, const std::wstring& cache,
                      const std::string& atlasHash, AtlasBuildResult* output,
                      std::string* error) {
  for (const auto& plan : plans) {
    ResourceDescriptor resource;
    resource.owner = plan.owner;
    resource.logicalId.assign(plan.logicalId.begin(), plan.logicalId.end());
    resource.engineType = plan.engineType;
    resource.sourcePath = plan.sourcePath;
    resource.virtualPath = plan.virtualPath;
    resource.type = ResourceType::TxtrSurface;
    resource.sourceSha256 = plan.sha256;
    resource.generatedPath = Join(cache, L"surface_" + HexId(plan.token & ~kRegionTag) + L".txtr");
    resource.regionName = plan.regionName;
    resource.regionToken = plan.token;
    output->resources.push_back(std::move(resource));
  }
  ResourceDescriptor atlas;
  atlas.owner = L"__cc2x_framework__";
  atlas.logicalId = L"atlas/index";
  atlas.sourcePath = Join(cache, L"index.atlas");
  atlas.virtualPath = kAtlasVirtualPath;
  atlas.type = ResourceType::AtlaIndex;
  atlas.sourceSha256 = atlasHash;
  atlas.generatedPath = atlas.sourcePath;
  output->resources.push_back(std::move(atlas));
  (void)error;
  return true;
}

}  // namespace

bool PrepareAtlas(const host::ModDiscovery& mods, const std::wstring& vanillaAtlasPath,
                  const std::wstring& cacheRoot, const char* gameBuild,
                  AtlasBuildResult* output, std::string* error) {
  if (!output || !error) return false;
  *output = {};
  error->clear();
  std::vector<ImagePlan> plans;
  const bool anyImages = std::any_of(mods.mods.begin(), mods.mods.end(),
      [](const host::ModDescriptor& mod) { return !mod.uiImages.empty(); });
  if (!anyImages) return true;
  if (!gameBuild || std::strcmp(gameBuild, "1.5.18-win32-r2") != 0) {
    *error = "atlas_factory_game_profile_unsupported";
    return false;
  }

  const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(com)) { *error = "wic_com_initialization_failed"; return false; }
  bool ok = false;
  do {
    float smallBounds[4]{}, largeBounds[4]{};
    if (!ReadVanillaBounds(vanillaAtlasPath, smallBounds, largeBounds, error) ||
        !MakeImagePlan(mods, smallBounds, largeBounds, &plans, error)) break;
    if (!SafeCreateDirectory(cacheRoot)) { *error = "cc2x_cache_root_unavailable"; break; }
    output->cacheKey = BuildKey(&plans, gameBuild);
    if (output->cacheKey.empty()) { *error = "atlas_cache_key_failed"; break; }
    output->cacheDirectory = Join(cacheRoot, L"atlas-" + std::wstring(output->cacheKey.begin(), output->cacheKey.end()));

    std::vector<std::pair<std::string, std::string>> generated;
    if (ValidateCache(output->cacheDirectory, output->cacheKey, plans.size())) {
      output->cacheHit = true;
      output->generatedSmallIcons = static_cast<std::size_t>(std::count_if(
          plans.begin(), plans.end(), [](const ImagePlan& plan) { return plan.generateSmall; }));
      for (std::size_t i = 0; i < plans.size(); ++i) {
        char name[64]{}; sprintf_s(name, "surface_%08X.txtr",
            static_cast<unsigned>(i + kFirstDynamicRegion));
        const auto values = ReadCacheManifest(Join(output->cacheDirectory, L"cache.meta"));
        generated.push_back({name, values.at(name)});
      }
    } else {
      const auto stageName = L".stage-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
          std::to_wstring(GetTickCount64());
      const auto stage = Join(cacheRoot, stageName);
      std::error_code removeError;
      std::filesystem::remove_all(stage, removeError);
      if (!SafeCreateDirectory(stage)) { *error = "atlas_stage_directory_unavailable"; break; }
      std::size_t generatedSmall = 0;
      if (!BuildSurfaces(plans, stage, &generated, &generatedSmall, error)) {
        std::filesystem::remove_all(stage, removeError); break;
      }
      output->generatedSmallIcons = generatedSmall;
      auto atlas = EncodeAtlas(plans);
      if (atlas.empty() || !WriteFile(Join(stage, L"index.atlas"), atlas)) {
        *error = "atlas_index_generation_failed";
        std::filesystem::remove_all(stage, removeError); break;
      }
      std::string atlasHash;
      if (!HashBytes(atlas.data(), atlas.size(), &atlasHash)) {
        *error = "atlas_index_hash_failed";
        std::filesystem::remove_all(stage, removeError); break;
      }
      std::ostringstream metadata;
      metadata << "version=" << kFactoryVersion << "\nkey=" << output->cacheKey << "\n";
      for (const auto& file : generated) metadata << file.first << '=' << file.second << '\n';
      metadata << "index.atlas=" << atlasHash << '\n';
      const auto metaText = metadata.str();
      std::vector<std::uint8_t> meta(metaText.begin(), metaText.end());
      if (!WriteFile(Join(stage, L"cache.meta"), meta)) {
        *error = "atlas_cache_metadata_write_failed";
        std::filesystem::remove_all(stage, removeError); break;
      }
      // A corrupt cache from a prior interrupted run belongs to this isolated
      // CC2X root. Remove only this exact content-addressed child after the
      // replacement has been fully generated and validated.
      if (std::filesystem::exists(output->cacheDirectory))
        std::filesystem::remove_all(output->cacheDirectory, removeError);
      if (removeError) {
        *error = "atlas_cache_corrupt_entry_cleanup_failed";
        std::filesystem::remove_all(stage, removeError); break;
      }
      if (!MoveFileExW(stage.c_str(), output->cacheDirectory.c_str(), MOVEFILE_WRITE_THROUGH)) {
        if (!ValidateCache(output->cacheDirectory, output->cacheKey, plans.size())) {
          *error = "atlas_cache_atomic_commit_failed";
          std::filesystem::remove_all(stage, removeError); break;
        }
        std::filesystem::remove_all(stage, removeError);
      }
    }
    std::string atlasHash;
    const auto meta = ReadCacheManifest(Join(output->cacheDirectory, L"cache.meta"));
    if (meta.find("index.atlas") == meta.end()) { *error = "atlas_cache_manifest_incomplete"; break; }
    atlasHash = meta.at("index.atlas");
    if (!PublishResources(plans, output->cacheDirectory, atlasHash, output, error)) break;
    output->regions = plans.size();
    CleanupStaleCache(cacheRoot, output->cacheDirectory);
    ok = true;
  } while (false);
  CoUninitialize();
  if (!ok) *output = {};
  return ok;
}

}  // namespace cc2::api::ui
