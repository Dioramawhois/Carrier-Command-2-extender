// Executes only emitted patch sequences against a private relocated image.
// No game entry point or game function is executed: two replayed callees are
// replaced with RET in this private fixture before the emitted code is tested.
#include "../src/api/animation/animation_speed.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
float g_two=2, g_sixty=60, g_small=0.2f, g_result=0, g_second=0;
float g_vector[]{1,2,3,4}, g_vectorAfter[4]{};
DWORD g_flags=0;
int failures=0;
void Check(bool ok,const char* what){if(!ok){std::printf("FAIL %s\n",what);++failures;}}
bool Near(float a,float b){return std::fabs(a-b)<0.00002f;}
DWORD WINAPI WaitingThread(void* event){return WaitForSingleObject(static_cast<HANDLE>(event),5000);}
float At(BYTE* image,std::size_t operand){
  std::uint32_t pointer=0;std::memcpy(&pointer,image+operand,4);
  float result=0;std::memcpy(&result,reinterpret_cast<void*>(pointer),4);return result;
}
__declspec(naked) void RunWindow(void*,void*){
  __asm {
    push ebp
    mov eax,[esp+8]
    mov ebp,[esp+12]
    movss xmm0,g_two
    movss xmm7,g_sixty
    stc
    call eax
    movss g_result,xmm0
    pushfd
    pop eax
    mov g_flags,eax
    pop ebp
    ret
  }
}
__declspec(naked) void RunRoute(void*){
  __asm {
    mov eax,[esp+4]
    movss xmm1,g_two
    stc
    call eax
    movss g_result,xmm1
    pushfd
    pop eax
    mov g_flags,eax
    ret
  }
}
__declspec(naked) void RunDirect(void*,void*){
  __asm {
    mov edx,[esp+4]
    mov eax,[esp+8]
    movss xmm0,g_sixty
    movups xmm1,g_vector
    stc
    call edx
    movss g_result,xmm0
    movups g_vectorAfter,xmm1
    pushfd
    pop eax
    mov g_flags,eax
    ret
  }
}
__declspec(naked) void RunClamp(void*){
  __asm {
    mov eax,[esp+4]
    movss xmm0,g_two
    movss xmm2,g_sixty
    stc
    call eax
    movss g_result,xmm0
    movss g_second,xmm2
    pushfd
    pop eax
    mov g_flags,eax
    ret
  }
}
__declspec(naked) void RunFactor(void*){
  __asm {
    mov eax,[esp+4]
    movss xmm6,g_small
    stc
    call eax
    movss g_result,xmm6
    pushfd
    pop eax
    mov g_flags,eax
    ret
  }
}
}

int wmain(int argc,wchar_t** argv){
  using namespace cc2::api::animation;
  if(argc!=2){std::puts("Usage: CC2AnimationPatchTest.exe <audited-exe-path>");return 2;}
  if(!VerifyFixtureTarget(argv[1])){std::puts("Target SHA-256 mismatch");return 2;}
  std::ifstream in(argv[1],std::ios::binary);
  std::vector<char> file{std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
  auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(file.data());
  auto* pe=reinterpret_cast<IMAGE_NT_HEADERS32*>(file.data()+dos->e_lfanew);
  auto* image=static_cast<BYTE*>(VirtualAlloc(nullptr,pe->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
  if(!image)return 2;
  std::memcpy(image,file.data(),pe->OptionalHeader.SizeOfHeaders);
  const auto* sections=IMAGE_FIRST_SECTION(pe);
  for(int i=0;i<pe->FileHeader.NumberOfSections;++i)
    if(sections[i].SizeOfRawData)std::memcpy(image+sections[i].VirtualAddress,file.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
  const auto delta=static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(image))-pe->OptionalHeader.ImageBase;
  const auto& reloc=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
  std::size_t cursor=0;
  while(cursor+sizeof(IMAGE_BASE_RELOCATION)<=reloc.Size){
    auto* block=reinterpret_cast<IMAGE_BASE_RELOCATION*>(image+reloc.VirtualAddress+cursor);
    if(block->SizeOfBlock<8 || block->SizeOfBlock>reloc.Size-cursor)break;
    auto* words=reinterpret_cast<WORD*>(block+1);
    for(std::size_t i=0;i<(block->SizeOfBlock-8)/2;++i)
      if((words[i]>>12)==IMAGE_REL_BASED_HIGHLOW){
        auto* value=reinterpret_cast<DWORD*>(image+block->VirtualAddress+(words[i]&0xfff));*value+=delta;
      }
    cursor+=block->SizeOfBlock;
  }
  const bool legacy=FixtureHasLegacyPatchProfile(argv[1]);
  const char* build=legacy?"1.5.18-win32":"1.5.18-win32-r2";
  const std::size_t codeShift=legacy?0:0x110;
  const std::size_t clampShift=legacy?0:0x120;
  const std::size_t factorShift=legacy?0:0x130;
  const std::size_t routeCallSites[]{0x2fcc97,0x2fcf8d,0x2fd664,0x2fdb4b};
  const std::size_t craneCallSites[]{0x300f55,0x30105c,0x30113e,0x301184,0x3011d4,0x301229};
  Check(!SetSpeed(Group::kRoutes,2),"setter unavailable before startup installation");
  const auto windowA=0x3007e3+codeShift,windowB=0x300c10+codeShift;
  const BYTE first=image[windowA];image[windowA]^=1;
  Check(InstallFixture(image,pe->OptionalHeader.SizeOfImage,build)==ERROR_REVISION_MISMATCH,"changed instruction guard rejects transaction");
  Check(image[windowB]==0xf3,"failed validation left other game code untouched");
  image[windowA]=first;
  HANDLE wake=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  HANDLE worker=CreateThread(nullptr,0,WaitingThread,wake,0,nullptr);
  Check(InstallFixture(image,pe->OptionalHeader.SizeOfImage,build)==ERROR_SUCCESS,"26-site installation on relocated private image");
  SetEvent(wake);
  Check(WaitForSingleObject(worker,1000)==WAIT_OBJECT_0,"startup suspension released the fixture worker");
  CloseHandle(worker);CloseHandle(wake);
  if(!IsReady())return 1;
  // No original engine code is executed below. Only emitted sequences, and
  // explicitly terminated original instruction windows, are exercised.
  DWORD old=0;VirtualProtect(image,pe->OptionalHeader.SizeOfImage,PAGE_EXECUTE_READWRITE,&old);
  for(std::size_t rva:{0x3007ed,0x300c1a,0x2fcc9f,0x2fcf95,0x2fd66c,0x2fdb53,0x2fd8a7,
                       0x300f5a,0x301061,0x301143,0x301189,0x3011d9,0x30122e,0x300ee6,0x470700,0x4443f0})
    image[rva]=0xc3;
  for(std::size_t rva:{0x3007ed,0x300c1a,0x2fcc9f,0x2fcf95,0x2fd66c,0x2fdb53,0x2fd8a7,
                       0x300f5a,0x301061,0x301143,0x301189,0x3011d9,0x30122e,0x300ee6})
    image[rva+codeShift]=0xc3;
  image[0x470700+clampShift]=0xc3;image[0x4443f0+factorShift]=0xc3;
  FlushInstructionCache(GetCurrentProcess(),image,pe->OptionalHeader.SizeOfImage);
  const std::vector<BYTE> installedImage(image,image+pe->OptionalHeader.SizeOfImage);
  std::array<BYTE,0x240> frame{};std::array<BYTE,0x80> object{};std::memcpy(object.data()+0x78,&g_two,4);
  RunWindow(image+windowA,frame.data()+0x200);
  Check(Near(g_result,120) && (g_flags&1),"x1 runway cave replays MULSS/MOV, preserves carry");
  RunRoute(image+routeCallSites[0]+codeShift);Check(Near(g_result,60) && (g_flags&1),"x1 route replay/relocated 30.0 and flags");
  Check(SetSpeed(Group::kRoutes,50) && SetSpeed(Group::kRunway,100),"legacy route/runway multipliers accepted");
  RunWindow(image+windowA,frame.data()+0x200);Check(Near(g_result,1.2f),"runway x100 denominator");
  std::uint32_t routeHits[3]{};ReadRoutePatchHitCounts(routeHits);
  const auto routeWindowBaseline=routeHits[0];
  RunWindow(image+windowB,frame.data()+0x200);Check(Near(g_result,2.4f),"other routes x50 denominator");
  ReadRoutePatchHitCounts(routeHits);
  Check(routeHits[0]==routeWindowBaseline+1,"diagnostic counter records 0x300c10 route-path execution");
  const auto siteA=routeHits[1],siteB=routeHits[2];
  for(std::size_t i=0;i<std::size(routeCallSites);++i){
    RunRoute(image+routeCallSites[i]+codeShift);Check(Near(g_result,i<2?1.2f:0.6f) && (g_flags&1),"route group selection and flags");
    ReadRoutePatchHitCounts(routeHits);
  }
  ReadRoutePatchHitCounts(routeHits);
  Check(routeHits[1]==siteA+1 && routeHits[2]==siteB+1,"diagnostic counters record both 0x2f route-path executions");
  RunDirect(image+0x2fd8a2+codeShift,object.data());
  Check(Near(g_result,3000) && std::memcmp(g_vector,g_vectorAfter,16)==0 && (g_flags&1),"direct DIVSS scaling, full XMM1 and flags preserved");
  const float liftPos=At(image,0x305c14+codeShift),liftNeg=At(image,0x305c3e+codeShift),cranePos=At(image,0x300f26+codeShift);
  Check(SetSpeed(Group::kLift,50) && Near(At(image,0x305c14+codeShift),liftPos*50) && Near(At(image,0x305c3e+codeShift),liftNeg*50),"symmetric local lift bounds");
  Check(SetSpeed(Group::kCrane,10) && Near(At(image,0x300f26+codeShift),cranePos*10),"crane step bounds updated");
  for(std::size_t i=0;i<std::size(craneCallSites);++i){
    RunClamp(image+craneCallSites[i]+codeShift);Check(Near(g_result,2) && (g_flags&1),"crane clamp result/carry preserved through replay");
  }
  RunClamp(image+craneCallSites[0]+codeShift);Check(Near(g_second,cranePos),"crane normalization remains ORIGINAL denominator");
  RunFactor(image+0x300ee1+codeShift);Check(Near(g_result,1) && (g_flags&1),"crane interpolation multiplier capped at 1");
  Check(SetSpeed(Group::kCrane,11) && SetSpeed(Group::kLift,51) && SetSpeed(Group::kRunway,101),"old experimental ceilings are not native limits");
  Check(!SetSpeed(Group::kLift,0) && !SetSpeed(Group::kRoutes,0),"zero denominator/factor rejected");
  Check(!SetSpeed(static_cast<Group>(99),1),"unknown group rejected");
  Check(SetSpeed(Group::kRoutes,1)&&SetSpeed(Group::kRunway,1)&&SetSpeed(Group::kLift,1)&&SetSpeed(Group::kCrane,1),"reset coefficients without unhook");
  RunWindow(image+windowA,frame.data()+0x200);Check(Near(g_result,120),"x1 restored after runtime changes");
  Check(Near(At(image,0x305c14+codeShift),liftPos)&&Near(At(image,0x300f26+codeShift),cranePos),"lift/crane original bounds restored");
  Check(std::memcmp(image,installedImage.data(),installedImage.size())==0,"all runtime speed changes leave installed code/game constants unchanged");
  std::printf("animation patch fixture (%s): %s; game was not launched\n",build,failures?"FAIL":"PASS");
  return failures?1:0;
}
