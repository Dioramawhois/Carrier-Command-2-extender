#include "api/ui/second_atlas_contract.h"

// This contract test must execute its assertions in Release as well as Debug.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace cc2::api::ui::second_atlas;

static void PutU32(std::vector<std::uint8_t>& data, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8)
    data.push_back(static_cast<std::uint8_t>(value >> shift));
}

static void PutName(std::vector<std::uint8_t>& data, const char* value) {
  const auto length = std::strlen(value);
  data.push_back(static_cast<std::uint8_t>(length));
  data.push_back(0);
  data.insert(data.end(), value, value + length);
}

static void PutFloat(std::vector<std::uint8_t>& data, float value) {
  std::uint8_t raw[sizeof(value)]{};
  std::memcpy(raw, &value, sizeof(value));
  data.insert(data.end(), raw, raw + sizeof(value));
}

static std::vector<std::uint8_t> MakeAtlas() {
  std::vector<std::uint8_t> data{'A', 'T', 'L', 'A'};
  PutU32(data, 1); // version
  PutU32(data, 1); // surfaces
  PutName(data, "icons.txtr");
  PutU32(data, 2); // regions
  const auto putRegion = [&data](const char* name, float x0, float x1, float y0, float y1) {
    PutName(data, name);
    PutU32(data, 0);
    for (const auto value : {x0, x1, y0, y1, 0.1f, 0.9f, 0.2f, 0.8f}) PutFloat(data, value);
  };
  putRegion("icon_chassis_16_wing_small", -8, 8, -8, 8);
  putRegion("icon_chassis_16_wing_large", -10, 10, -9, 9);
  return data;
}

int main() {
  const auto first = static_cast<Region>(7);
  const auto second = static_cast<Region>(512);
  std::uint32_t decoded = 0;
  assert(DecodeCustomRegionToken(EncodedIndex(first), &decoded) && decoded == 7);
  assert(DecodeCustomRegionToken(EncodedIndex(second), &decoded) && decoded == 512);
  assert(!DecodeCustomRegionToken(0, &decoded));
  assert(!DecodeCustomRegionToken(kCustomRegionTag, &decoded));
  assert(!DecodeCustomRegionToken(0xc0000001u, &decoded));
  assert(!DecodeCustomRegionToken(EncodedIndex(first), nullptr));
  assert(EncodedIndex(second) < 0x7fffffffu);
  assert(CircularSlot(0, 8, 0) == 0);
  assert(CircularSlot(7, 8, 0) == 7);
  assert(CircularSlot(7, 8, 1) == 0);
  assert(CircularSlot(2, 8, 3) == 5);
  // Engine's ring head is monotonic and wraps by one subtraction, not `%`.
  assert(CircularSlot(10, 8, 0) == 2);
  assert(CircularSlot(10, 8, 2) == 4);

  const auto atlas = MakeAtlas();
  float bounds[4]{};
  assert(FindAtlasBoundsV1(atlas.data(), atlas.size(),
      "icon_chassis_16_wing_small", bounds));
  assert(bounds[0] == -8 && bounds[1] == 8 && bounds[2] == -8 && bounds[3] == 8);
  assert((bounds[0] + bounds[1]) * 0.5f == 0);
  assert((bounds[2] + bounds[3]) * 0.5f == 0);
  assert(FindAtlasBoundsV1(atlas.data(), atlas.size(),
      "icon_chassis_16_wing_large", bounds));
  assert(bounds[0] == -10 && bounds[1] == 10 && bounds[2] == -9 && bounds[3] == 9);
  assert((bounds[0] + bounds[1]) * 0.5f == 0);
  assert((bounds[2] + bounds[3]) * 0.5f == 0);
  assert(!FindAtlasBoundsV1(atlas.data(), atlas.size(), "missing", bounds));
  // This helper reads the requested record, not the entire atlas. Truncating
  // the second record leaves the first readable; the second must be rejected.
  assert(FindAtlasBoundsV1(atlas.data(), atlas.size() - 1,
      "icon_chassis_16_wing_small", bounds));
  for (std::size_t truncated = 0; truncated < atlas.size(); ++truncated) {
    assert(!FindAtlasBoundsV1(atlas.data(), truncated,
        "icon_chassis_16_wing_large", bounds));
  }
  auto invalidAtlas = atlas;
  invalidAtlas[4] = 2; // unsupported version
  assert(!FindAtlasBoundsV1(invalidAtlas.data(), invalidAtlas.size(),
      "icon_chassis_16_wing_small", bounds));

  LookupContextStack lookups;
  lookups.Push(first);
  lookups.Push(static_cast<Region>(0));
  lookups.Push(second);
  assert(lookups.Pop() == second);
  assert(lookups.Pop() == static_cast<Region>(0));
  assert(lookups.Pop() == first);
  assert(lookups.Pop() == static_cast<Region>(0));
  for (std::size_t i = 0; i < LookupContextStack::kCapacity + 1; ++i) {
    lookups.Push(first);
  }
  assert(lookups.Pop() == static_cast<Region>(0));
  assert(lookups.Pop() == first);
  return 0;
}
