#include "api/carrier/pe_relocation_guard.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
template<class T> void Put(std::vector<BYTE>& image,std::size_t offset,const T& value) {
  std::memcpy(image.data()+offset,&value,sizeof(value));
}
}

int main() {
  constexpr std::uint32_t kImageBase=0x00400000;
  constexpr std::size_t kImageSize=0x5000;
  constexpr std::uint32_t kTargetRva=0x10f8;
  constexpr std::uint32_t kRelocatedFieldRva=0x1100;
  constexpr std::uint32_t kPreferredValue=0x00c7d29d;
  std::vector<BYTE> image(kImageSize,0);

  IMAGE_DOS_HEADER dos{}; dos.e_magic=IMAGE_DOS_SIGNATURE; dos.e_lfanew=0x80;
  Put(image,0,dos);
  const DWORD signature=IMAGE_NT_SIGNATURE;
  Put(image,0x80,signature);
  IMAGE_FILE_HEADER file{}; file.Machine=IMAGE_FILE_MACHINE_I386;
  file.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER32);
  Put(image,0x84,file);
  IMAGE_OPTIONAL_HEADER32 optional{}; optional.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC;
  optional.ImageBase=kImageBase; optional.SizeOfImage=static_cast<DWORD>(kImageSize);
  optional.NumberOfRvaAndSizes=IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
  // The live loader may discard .reloc after applying fixups; the guard must
  // work from profile-supplied operand offsets without reading that section.
  optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress=0;
  optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size=0;
  Put(image,0x84+sizeof(file),optional);

  IMAGE_BASE_RELOCATION block{}; block.VirtualAddress=0x1000; block.SizeOfBlock=12;
  Put(image,0x3000,block);
  const WORD relocations[]{static_cast<WORD>((IMAGE_REL_BASED_HIGHLOW<<12)|0x100),
                           static_cast<WORD>(IMAGE_REL_BASED_ABSOLUTE<<12)};
  std::memcpy(image.data()+0x3000+sizeof(block),relocations,sizeof(relocations));

  std::array<BYTE,16> expected{0x55,0x8b,0xec,0x6a,0xff,0x68};
  std::memcpy(expected.data()+8,&kPreferredValue,sizeof(kPreferredValue));
  std::memcpy(image.data()+kTargetRva,expected.data(),expected.size());
  const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image.data())-kImageBase);
  // Model the observed CC2 loader behavior: the mapped PE header's ImageBase
  // is rewritten to the actual mapping base even though relocation operands
  // still need the original, profile-pinned preferred base for normalization.
  optional.ImageBase=static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(image.data()));
  Put(image,0x84+sizeof(file),optional);
  const auto loadedValue=kPreferredValue+delta;
  std::memcpy(image.data()+kRelocatedFieldRva,&loadedValue,sizeof(loadedValue));
  constexpr cc2::runtime::pe_guard::RelocationField relocationFields[]{{8,kPreferredValue}};
  cc2::runtime::pe_guard::MatchDiagnostics diagnostics{};

  if (!cc2::runtime::pe_guard::MatchLoadedBytes(image.data(),image.size(),kTargetRva,
                                                expected.data(),expected.size(),
                                                relocationFields,std::size(relocationFields),kImageBase,
                                                &diagnostics)) {
    std::fprintf(stderr,"ASLR-adjusted relocation did not match\n"); return 1;
  }
  if (diagnostics.relocationDelta!=delta ||
      diagnostics.mappedHeaderImageBase!=optional.ImageBase ||
      diagnostics.profilePreferredImageBase!=kImageBase) {
    std::fprintf(stderr,"relocation diagnostics did not retain profile and mapped bases\n"); return 6;
  }
  image[kTargetRva] ^= 0x01;
  if (cc2::runtime::pe_guard::MatchLoadedBytes(image.data(),image.size(),kTargetRva,
                                               expected.data(),expected.size(),
                                               relocationFields,std::size(relocationFields),kImageBase)) {
    std::fprintf(stderr,"non-relocated instruction mismatch was accepted\n"); return 2;
  }
  image[kTargetRva] ^= 0x01;
  std::memcpy(image.data()+kRelocatedFieldRva,&kPreferredValue,sizeof(kPreferredValue));
  if (cc2::runtime::pe_guard::MatchLoadedBytes(image.data(),image.size(),kTargetRva,
                                               expected.data(),expected.size(),
                                               relocationFields,std::size(relocationFields),kImageBase)) {
    std::fprintf(stderr,"stale preferred-base operand was accepted after rebase\n"); return 3;
  }
  constexpr std::uint32_t kPartialRva=0x1200;
  constexpr std::uint32_t kPartialPreferredValue=0x00e1db44;
  std::array<BYTE,16> partialExpected{};
  std::memcpy(partialExpected.data()+13,&kPartialPreferredValue,3);
  std::memcpy(image.data()+kPartialRva,partialExpected.data(),partialExpected.size());
  const auto partialLoadedValue=kPartialPreferredValue+delta;
  std::memcpy(image.data()+kPartialRva+13,&partialLoadedValue,sizeof(partialLoadedValue));
  constexpr cc2::runtime::pe_guard::RelocationField partialFields[]{{13,kPartialPreferredValue}};
  if (!cc2::runtime::pe_guard::MatchLoadedBytes(image.data(),image.size(),kPartialRva,
                                                 partialExpected.data(),partialExpected.size(),
                                                 partialFields,std::size(partialFields),kImageBase)) {
    std::fprintf(stderr,"partially covered relocated operand was not normalized\n"); return 4;
  }
  if (cc2::runtime::pe_guard::MatchLoadedBytes(image.data(),image.size(),kPartialRva,
                                                partialExpected.data(),partialExpected.size(),
                                                nullptr,0,kImageBase)) {
    std::fprintf(stderr,"unlisted partial relocation was accepted\n"); return 5;
  }
  std::puts("PASS PE32 base-relocation-aware signature guard");
  return 0;
}
