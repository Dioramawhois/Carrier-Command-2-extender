#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cc2::api::ui::second_atlas {

// Keep the token below INT32_MAX because the stock Lua binding may expose
// the native unsigned index through a signed Lua integer.
constexpr std::uint32_t kCustomRegionTag = 0x40000000u;
enum class Region : std::uint32_t {}; // opaque registry token

// The engine's atlas lookup overwrites its incoming stack argument before
// returning. Keep the requested region in a per-thread LIFO until the return
// hook can substitute the native index.
struct LookupContextStack {
  static constexpr std::size_t kCapacity = 16;
  Region regions[kCapacity]{};
  std::size_t depth = 0;
  std::size_t overflow = 0;

  void Push(Region region) {
    if (overflow != 0 || depth == kCapacity) {
      ++overflow;
      return;
    }
    regions[depth++] = region;
  }

  Region Pop() {
    if (overflow != 0) {
      --overflow;
      return static_cast<Region>(0);
    }
    if (depth == 0) return static_cast<Region>(0);
    return regions[--depth];
  }
};

constexpr bool DecodeCustomRegionToken(std::uint32_t value, std::uint32_t* out) {
  if (!out || (value & 0xC0000000u) != kCustomRegionTag) return false;
  const auto index = value & ~kCustomRegionTag;
  if (!index) return false;
  *out = index;
  return true;
}

// Read the four bounds from a serialized ATLA v1 atlas without depending on
// host alignment. The values are copied verbatim so custom regions inherit
// both the dimensions and the exact origin/centering of the stock icon.
inline bool FindAtlasBoundsV1(const std::uint8_t* data, std::size_t size,
                              const char* targetName, float outBounds[4]) {
  if (!data || !targetName || !outBounds || size < 16 ||
      data[0] != 'A' || data[1] != 'T' || data[2] != 'L' || data[3] != 'A') return false;
  auto readU32 = [data, size](std::size_t offset, std::uint32_t* out) {
    if (!out || offset > size || size - offset < 4) return false;
    *out = static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
    return true;
  };
  std::uint32_t version = 0, surfaceCount = 0;
  if (!readU32(4, &version) || version != 1 || !readU32(8, &surfaceCount)) return false;
  std::size_t cursor = 12;
  for (std::uint32_t i = 0; i < surfaceCount; ++i) {
    if (cursor > size || size - cursor < 2 || data[cursor + 1] != 0) return false;
    const auto length = static_cast<std::size_t>(data[cursor]);
    cursor += 2;
    if (length > size - cursor) return false;
    cursor += length;
  }
  std::uint32_t regionCount = 0;
  if (!readU32(cursor, &regionCount)) return false;
  cursor += 4;
  const auto targetLength = std::strlen(targetName);
  for (std::uint32_t i = 0; i < regionCount; ++i) {
    if (cursor > size || size - cursor < 2 || data[cursor + 1] != 0) return false;
    const auto nameLength = static_cast<std::size_t>(data[cursor]);
    cursor += 2;
    // name + surface index + 8 floats
    if (nameLength > size - cursor || size - cursor - nameLength < 36) return false;
    const bool match = nameLength == targetLength &&
        std::memcmp(data + cursor, targetName, targetLength) == 0;
    cursor += nameLength;
    std::uint32_t surfaceIndex = 0;
    if (!readU32(cursor, &surfaceIndex)) return false;
    (void)surfaceIndex; // the UI atlas parser validates this against its surfaces
    cursor += 4;
    if (match) {
      std::memcpy(outBounds, data + cursor, sizeof(float) * 4);
      return outBounds[1] > outBounds[0] && outBounds[3] > outBounds[2];
    }
    cursor += sizeof(float) * 8;
  }
  return false;
}

constexpr std::uint32_t EncodedIndex(Region region) {
  return kCustomRegionTag | static_cast<std::uint32_t>(region);
}

constexpr std::uint32_t CircularSlot(std::uint32_t head, std::uint32_t capacity,
                                     std::uint32_t offset) {
  const auto logical = head + offset;
  return logical < capacity ? logical : logical - capacity;
}

}  // namespace cc2::api::ui::second_atlas
