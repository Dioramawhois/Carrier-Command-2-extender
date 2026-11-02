#pragma once

#include <Windows.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cc2::runtime::pe_guard {

struct RelocationField {
  std::uint8_t offset;
  std::uint32_t preferredValue;
};

struct MatchDiagnostics {
  const char* reason = "not_evaluated";
  std::uint32_t relocationDelta = 0;
  std::uint32_t mappedHeaderImageBase = 0;
  std::uint32_t profilePreferredImageBase = 0;
  BYTE normalized[64]{};
  bool normalizedAvailable = false;
};

inline bool ReadableRange(const void* address, std::size_t bytes) {
  if (!address || !bytes) return false;
  auto current = reinterpret_cast<std::uintptr_t>(address);
  std::size_t remaining = bytes;
  while (remaining) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(current), &info, sizeof(info)) ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
      return false;
    const DWORD protection = info.Protect & 0xff;
    if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
        protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READ &&
        protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY)
      return false;
    const auto region = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    const auto end = region + info.RegionSize;
    if (current < region || current >= end) return false;
    const auto chunk = static_cast<std::size_t>(std::min<std::uintptr_t>(end - current, remaining));
    current += chunk;
    remaining -= chunk;
  }
  return true;
}

// Compare an in-memory PE32 image range to bytes from the authenticated file
// image, normalizing only HIGHLOW relocation fields contained by that range.
// Unsupported/malformed relocations fail closed.
inline bool MatchLoadedBytes(const BYTE* image, std::size_t imageSize,
                             std::uint32_t targetRva, const BYTE* expected,
                             std::size_t expectedSize,
                             const RelocationField* relocationFields,
                             std::size_t relocationCount,
                             std::uint32_t preferredImageBase,
                             MatchDiagnostics* diagnostics = nullptr) {
  if (diagnostics) *diagnostics = MatchDiagnostics{};
  const auto fail = [diagnostics](const char* reason) {
    if (diagnostics) diagnostics->reason = reason;
    return false;
  };
  if (!image || !expected || expectedSize == 0 || expectedSize > 64 ||
      (relocationCount != 0 && !relocationFields) ||
      targetRva > imageSize || expectedSize > imageSize - targetRva ||
      preferredImageBase == 0 || imageSize < sizeof(IMAGE_DOS_HEADER) ||
      !ReadableRange(image,sizeof(IMAGE_DOS_HEADER)))
    return fail("invalid_image_or_range");

  IMAGE_DOS_HEADER dos{};
  std::memcpy(&dos, image, sizeof(dos));
  if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) return fail("invalid_dos_header");
  const auto ntOffset = static_cast<std::size_t>(dos.e_lfanew);
  if (ntOffset > imageSize || sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) > imageSize - ntOffset)
    return fail("nt_header_out_of_image");
  if (!ReadableRange(image + ntOffset, sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER)))
    return fail("nt_header_unreadable");

  DWORD signature = 0;
  IMAGE_FILE_HEADER file{};
  std::memcpy(&signature, image + ntOffset, sizeof(signature));
  std::memcpy(&file, image + ntOffset + sizeof(signature), sizeof(file));
  if (signature != IMAGE_NT_SIGNATURE || file.Machine != IMAGE_FILE_MACHINE_I386 ||
      file.SizeOfOptionalHeader < offsetof(IMAGE_OPTIONAL_HEADER32, DataDirectory) +
          (IMAGE_DIRECTORY_ENTRY_BASERELOC + 1) * sizeof(IMAGE_DATA_DIRECTORY) ||
      ntOffset + sizeof(signature) + sizeof(file) > imageSize ||
      file.SizeOfOptionalHeader > imageSize - (ntOffset + sizeof(signature) + sizeof(file)))
    return fail("invalid_pe32_file_header");
  if (!ReadableRange(image + ntOffset + sizeof(signature) + sizeof(file),sizeof(IMAGE_OPTIONAL_HEADER32)))
    return fail("optional_header_unreadable");

  IMAGE_OPTIONAL_HEADER32 optional{};
  std::memcpy(&optional, image + ntOffset + sizeof(signature) + sizeof(file),
              sizeof(optional));
  if (optional.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC || optional.SizeOfImage != imageSize ||
      optional.SizeOfImage == 0) return fail("pe32_image_size_mismatch");

  if (!ReadableRange(image + targetRva,expectedSize)) return fail("target_unreadable");

  BYTE adjusted[64]{};
  std::memcpy(adjusted, expected, expectedSize);
  // Do not trust OptionalHeader.ImageBase in the mapped image. Some CC2
  // startup paths rewrite that field to the actual load address, which would
  // incorrectly produce a zero delta even though absolute operands were
  // rebased. preferredImageBase comes from the SHA-256-pinned disk profile.
  const auto delta = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image) -
                                                 preferredImageBase);
  if (diagnostics) {
    diagnostics->relocationDelta = delta;
    diagnostics->mappedHeaderImageBase = optional.ImageBase;
    diagnostics->profilePreferredImageBase = preferredImageBase;
  }
  // The loader may discard the PE .reloc section after applying base
  // relocations. Therefore callers supply offsets extracted from the exact,
  // SHA-256-authenticated executable profile instead of reading that section
  // back from the live image. Unknown relocations fail closed as byte mismatch.
  for (std::size_t i = 0; i < relocationCount; ++i) {
    const auto fieldOffset = static_cast<std::size_t>(relocationFields[i].offset);
    if (fieldOffset >= expectedSize) return fail("relocation_offset_out_of_signature");
    for (std::size_t j = 0; j < i; ++j)
      if (relocationFields[j].offset == relocationFields[i].offset)
        return fail("duplicate_relocation_offset");
    DWORD originalBytes = relocationFields[i].preferredValue;
    if (std::memcmp(expected + fieldOffset, &originalBytes,
                    std::min(sizeof(originalBytes), expectedSize - fieldOffset)) != 0)
      return fail("relocation_profile_operand_mismatch");
    DWORD relocatedValue = relocationFields[i].preferredValue + delta;
    std::memcpy(adjusted + fieldOffset, &relocatedValue,
                std::min(sizeof(relocatedValue), expectedSize - fieldOffset));
  }

  if (diagnostics) {
    std::memcpy(diagnostics->normalized, adjusted, expectedSize);
    diagnostics->normalizedAvailable = true;
  }
  if (std::memcmp(image + targetRva, adjusted, expectedSize) != 0)
    return fail("loaded_bytes_mismatch");
  if (diagnostics) diagnostics->reason = "match";
  return true;
}

} // namespace cc2::runtime::pe_guard
