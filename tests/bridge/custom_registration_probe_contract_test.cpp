#include "reference/custom_registration_probe_contract.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc,char** argv) {
  using namespace cc2::research::custom_registration_probe;
  if(argc!=2){std::fprintf(stderr,"usage: CC2CustomRegistrationProbeContractTest <carrier_command.exe>\n");return 2;}
  std::ifstream input(argv[1],std::ios::binary);
  std::vector<unsigned char> file{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
  if(file.size()<sizeof(IMAGE_DOS_HEADER)){std::fprintf(stderr,"target image is unreadable\n");return 2;}
  IMAGE_DOS_HEADER dos{};std::memcpy(&dos,file.data(),sizeof(dos));
  if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||
     static_cast<std::size_t>(dos.e_lfanew)+sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER)>file.size()) {
    std::fprintf(stderr,"target image has invalid DOS/PE headers\n");return 2;
  }
  DWORD signature=0;std::memcpy(&signature,file.data()+dos.e_lfanew,sizeof(signature));
  IMAGE_FILE_HEADER fileHeader{};std::memcpy(&fileHeader,file.data()+dos.e_lfanew+sizeof(DWORD),sizeof(fileHeader));
  const auto optionalOffset=static_cast<std::size_t>(dos.e_lfanew)+sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER);
  if(signature!=IMAGE_NT_SIGNATURE||fileHeader.Machine!=IMAGE_FILE_MACHINE_I386||
     fileHeader.SizeOfOptionalHeader<sizeof(IMAGE_OPTIONAL_HEADER32)||optionalOffset+fileHeader.SizeOfOptionalHeader>file.size()) {
    std::fprintf(stderr,"target is not the expected PE32 x86 image\n");return 2;
  }
  IMAGE_OPTIONAL_HEADER32 optional{};std::memcpy(&optional,file.data()+optionalOffset,sizeof(optional));
  if(optional.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC){std::fprintf(stderr,"target optional header is not PE32\n");return 2;}
  if(fileHeader.TimeDateStamp==0x6ae228f3&&optional.SizeOfImage==0x00af7000) {
    constexpr DWORD wrapperRva=0x001EE840;
    constexpr unsigned char expectedWrapper[5]={0x55,0x8B,0xEC,0x6A,0xFF};
    std::size_t wrapperOffset=0;bool wrapperFound=false;
    auto sectionOffset=optionalOffset+fileHeader.SizeOfOptionalHeader;
    for(unsigned i=0;i<fileHeader.NumberOfSections;++i) {
      IMAGE_SECTION_HEADER section{};
      const auto at=sectionOffset+i*sizeof(section);
      if(at+sizeof(section)>file.size())break;
      std::memcpy(&section,file.data()+at,sizeof(section));
      const std::uint64_t rvaEnd=static_cast<std::uint64_t>(section.VirtualAddress)+section.SizeOfRawData;
      if(wrapperRva>=section.VirtualAddress&&static_cast<std::uint64_t>(wrapperRva)+sizeof(expectedWrapper)<=rvaEnd) {
        wrapperOffset=section.PointerToRawData+(wrapperRva-section.VirtualAddress);
        wrapperFound=wrapperOffset+sizeof(expectedWrapper)<=file.size();break;
      }
    }
    if(!wrapperFound||std::memcmp(file.data()+wrapperOffset,expectedWrapper,sizeof(expectedWrapper))!=0) {
      std::fprintf(stderr,"current-build Hangar wrapper guard mismatch at RVA 0x001EE840\n");return 2;
    }
    const auto matchesRva=[&](DWORD rva,const unsigned char* expected,std::size_t length) {
      auto sectionAt=optionalOffset+fileHeader.SizeOfOptionalHeader;
      for(unsigned i=0;i<fileHeader.NumberOfSections;++i) {
        IMAGE_SECTION_HEADER section{};
        const auto at=sectionAt+i*sizeof(section);
        if(at+sizeof(section)>file.size())return false;
        std::memcpy(&section,file.data()+at,sizeof(section));
        const std::uint64_t end=static_cast<std::uint64_t>(section.VirtualAddress)+section.SizeOfRawData;
        if(rva<section.VirtualAddress||static_cast<std::uint64_t>(rva)+length>end)continue;
        const auto offset=static_cast<std::size_t>(section.PointerToRawData)+(rva-section.VirtualAddress);
        return offset+length<=file.size()&&std::memcmp(file.data()+offset,expected,length)==0;
      }
      return false;
    };
    constexpr unsigned char loaderGuard[]={0x55,0x8B,0xEC,0x6A,0xFF};
    constexpr unsigned char definitionResizeGuard[]={0x55,0x8B,0xEC,0x53,0x56,0x8B,0xF1};
    constexpr unsigned char nameMapResizeGuard[]={0x55,0x8B,0xEC,0x51,0x53,0x56};
    constexpr unsigned char definitionParserGuard[]={0x55,0x8B,0xEC,0x6A,0xFF};
    if(!matchesRva(0x001A2E70,loaderGuard,sizeof(loaderGuard))||
       !matchesRva(0x0044F550,definitionResizeGuard,sizeof(definitionResizeGuard))||
       !matchesRva(0x00453E10,nameMapResizeGuard,sizeof(nameMapResizeGuard))||
       !matchesRva(0x001A5E80,definitionParserGuard,sizeof(definitionParserGuard))) {
      std::fprintf(stderr,"current-build definition observer guard mismatch or incomplete instruction boundary\n");return 2;
    }
    constexpr unsigned char registerDefinitionNameGuard[]={0xFF,0x76,0x04,0x8D,0x4E,0x08};
    constexpr unsigned char xmlParseSuccessGuard[]={0x84,0xC0,0x74,0x14,0x56,0x51};
    constexpr unsigned char xmlParseReturnGuard[]={0x8B,0x4D,0x0C,0x8B,0x55,0x08};
    constexpr unsigned char serverSelectorGuard[]={0x55,0x8B,0xEC,0x6A,0xFF};
    constexpr unsigned char clientSelectorGuard[]={0x55,0x8B,0xEC,0x6A,0xFF};
    constexpr unsigned char registerNameHelperGuard[]={0x55,0x8B,0xEC,0x6A,0xFF};
    if(!matchesRva(0x001A5C0D,registerDefinitionNameGuard,sizeof(registerDefinitionNameGuard))||
       !matchesRva(0x001A5FD5,xmlParseSuccessGuard,sizeof(xmlParseSuccessGuard))||
       !matchesRva(0x001A5C42,xmlParseReturnGuard,sizeof(xmlParseReturnGuard))||
       !matchesRva(0x00334B60,serverSelectorGuard,sizeof(serverSelectorGuard))||
       !matchesRva(0x00330660,clientSelectorGuard,sizeof(clientSelectorGuard))||
       !matchesRva(0x001A5CE0,registerNameHelperGuard,sizeof(registerNameHelperGuard))) {
      std::fprintf(stderr,"custom vehicle prototype site guard mismatch or incomplete instruction boundary\n");return 2;
    }
    struct Frame { cc2x_cpu cpu; std::uint32_t stack[5]; } frame{};
    frame.cpu.ecx=0x11110000;
    frame.stack[0]=0x22220000;frame.stack[1]=0x33330000;frame.stack[2]=0x44440000;
    frame.stack[3]=12;frame.stack[4]=0x55550000;
    const auto event=DecodeFactoryWrapperEntry(&frame.cpu,frame.stack,7,0x66660000);
    if(event.returnAddress!=0x22220000||event.manager!=0x11110000||event.objectId!=0||
       event.definitionContext!=0x33330000||event.transform!=0x44440000||event.type!=12||
       event.owner!=0x55550000||event.generation!=7||event.server!=0x66660000||event.boundary!=1) {
      std::fprintf(stderr,"current-build wrapper argument decoding failed\n");return 1;
    }
    std::puts("Current-build definition observer and custom-type prototype site guards verified at complete instruction boundaries; no runtime call performed");
    return 0;
  }
  constexpr DWORD factoryRva=0x001EF970;
  constexpr unsigned char expected[6]={0x55,0x8B,0xEC,0x83,0xEC,0x08};
  std::size_t factoryOffset=0;bool found=false;
  auto sectionOffset=optionalOffset+fileHeader.SizeOfOptionalHeader;
  for(unsigned i=0;i<fileHeader.NumberOfSections;++i) {
    IMAGE_SECTION_HEADER section{};
    const auto at=sectionOffset+i*sizeof(section);
    if(at+sizeof(section)>file.size())break;
    std::memcpy(&section,file.data()+at,sizeof(section));
    const std::uint64_t rvaEnd=static_cast<std::uint64_t>(section.VirtualAddress)+section.SizeOfRawData;
    if(factoryRva>=section.VirtualAddress&&static_cast<std::uint64_t>(factoryRva)+sizeof(expected)<=rvaEnd) {
      factoryOffset=section.PointerToRawData+(factoryRva-section.VirtualAddress);
      found=factoryOffset+sizeof(expected)<=file.size();break;
    }
  }
  if(!found||std::memcmp(file.data()+factoryOffset,expected,sizeof(expected))!=0) {
    std::fprintf(stderr,"FUN_005EF970 prologue mismatch at RVA 0x001EF970\n");return 2;
  }
  struct Frame { cc2x_cpu cpu; std::uint32_t stack[6]; } frame{};
  frame.cpu.ecx=0x11110000;
  frame.stack[0]=0x22220000;frame.stack[1]=0x31;frame.stack[2]=0x33330000;
  frame.stack[3]=0x44440000;frame.stack[4]=12;frame.stack[5]=0x55550000;
  const auto event=DecodeFactoryEntry(&frame.cpu,frame.stack,7,0x66660000);
  if(event.returnAddress!=0x22220000||event.manager!=0x11110000||event.objectId!=0x31||
     event.definitionContext!=0x33330000||event.transform!=0x44440000||event.type!=12||
     event.owner!=0x55550000||event.generation!=7||event.server!=0x66660000) {
    std::fprintf(stderr,"factory entry ABI decoding failed\n");return 1;
  }
  std::uint32_t address=0;
  if(!ComputeDefinitionEntryCandidate(0x10000000,12,&address)||address!=0x10001980) {
    std::fprintf(stderr,"definition slot arithmetic failed\n");return 2;
  }
  if(ComputeDefinitionEntryCandidate(0x10000000,101,&address)==false||address!=0x1000d6a0) {
    std::fprintf(stderr,"last confirmed stock definition slot rejected\n");return 3;
  }
  if(ComputeDefinitionEntryCandidate(0x10000000,102,&address)||
     ComputeDefinitionEntryCandidate(0x10000000,103,&address)||
     ComputeDefinitionEntryCandidate(0xfffffff0,12,&address)||
     ComputeDefinitionEntryCandidate(0,12,&address)) {
    std::fprintf(stderr,"unsafe definition slot candidate accepted\n");return 3;
  }
  return 0;
}
