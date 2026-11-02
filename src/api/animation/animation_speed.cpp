// Port of the working aircraft-speed prototype v5.2 patch locations/formulas.
// The original project remains unchanged. Proxy/DllMain and diagnostic-only
// probes are deliberately not duplicated here. Defaults reproduce x1 arithmetic.
#include "animation_speed.h"

#ifdef CC2_ANIMATION_TESTING
#include <TlHelp32.h>
#include <bcrypt.h>
#endif
#include <array>
#include <cmath>
#include <cstring>

namespace cc2::api::animation {
namespace {
#ifdef CC2_ANIMATION_TESTING
constexpr char kLegacyHash[] = "1c9a01c3007cb2bec2a04d716812cf9f24c87efa11056395c89ddd7d2c341026";
constexpr char kCurrentBuildHash[] = "461e12623bacb7de29ca27b4614425c87e8df2c8327a5e958a957c850effe42a";
#endif
struct Profile {
  const char* build;
  std::uintptr_t routeWindowSites[2];
  std::uintptr_t routeSites[4];
  std::uintptr_t directSite;
  std::uintptr_t craneOperands[10];
  std::uintptr_t craneSources[10];
  std::uintptr_t craneCalls[6];
  std::uintptr_t craneRestore[6];
  std::uintptr_t factorCallSite;
  std::uintptr_t liftSites[2];
  std::uintptr_t liftSources[2];
  std::uintptr_t routeConstant;
  std::uintptr_t clampTarget;
  std::uintptr_t factorTarget;
};
constexpr Profile kLegacyProfile{
  "1.5.18-win32", {0x3007e3,0x300c10}, {0x2fcc97,0x2fcf8d,0x2fd664,0x2fdb4b}, 0x2fd8a2,
  {0x300f26,0x300f31,0x301035,0x301040,0x301124,0x30112c,0x3011b6,0x3011be,0x301219,0x301225},
  {0x9b7224,0x9b7c14,0x9b7224,0x9b7c14,0x9b71bc,0x9b7be4,0x9b71bc,0x9b7be4,0x9b71c4,0x9b7bec},
  {0x300f55,0x30105c,0x30113e,0x301184,0x3011d4,0x301229},
  {0x9b7224,0x9b7224,0x9b71bc,0x9b71bc,0x9b71bc,0x9b71c4},
  0x300ee1, {0x305c10,0x305c3a}, {0x9b71b0,0x9b7be0}, 0x9b798c, 0x470700, 0x4443f0
};
// The October executable was independently checked by exact SHA, PE identity,
// every instruction window, every source operand and all seven relative call
// targets.  The +0x110 code / +0x38 data movement is specific to that hash.
constexpr Profile kSteam26277078Profile{
  "1.5.18-win32-r2", {0x3008f3,0x300d20}, {0x2fcda7,0x2fd09d,0x2fd774,0x2fdc5b}, 0x2fd9b2,
  {0x301036,0x301041,0x301145,0x301150,0x301234,0x30123c,0x3012c6,0x3012ce,0x301329,0x301335},
  {0x9b725c,0x9b7c4c,0x9b725c,0x9b7c4c,0x9b71f4,0x9b7c1c,0x9b71f4,0x9b7c1c,0x9b71fc,0x9b7c24},
  {0x301065,0x30116c,0x30124e,0x301294,0x3012e4,0x301339},
  {0x9b725c,0x9b725c,0x9b71f4,0x9b71f4,0x9b71f4,0x9b71fc},
  0x300ff1, {0x305d20,0x305d4a}, {0x9b71e8,0x9b7c18}, 0x9b79c4, 0x470820, 0x444520
};
constexpr std::size_t kCount = 26;

struct Patch {
  BYTE* site = nullptr;
  BYTE expected[10]{};
  BYTE replacement[10]{};
  std::size_t size = 0;
};
std::array<Patch, kCount> g_patches{};
std::size_t g_count = 0;
BYTE* g_code = nullptr;
std::size_t g_codeUsed = 0;
alignas(4) float g_routes = 1, g_runway = 1, g_craneFactor = 1;
alignas(4) const float g_factorCap = 1;
alignas(4) std::array<float, 2> g_lift{};
alignas(4) std::array<float, 10> g_crane{};
std::array<float, 2> g_originalLift{};
std::array<float, 10> g_originalCrane{};
volatile LONG g_ready = 0;
SRWLOCK g_settingsLock = SRWLOCK_INIT;
#if defined(CC2_ANIMATION_TRACE)
volatile LONG g_routeHitCounts[3]{};
#endif

const Profile* SelectProfile(const char* build) {
  if (!build) return nullptr;
  if (std::strcmp(build,kLegacyProfile.build)==0) return &kLegacyProfile;
  if (std::strcmp(build,kSteam26277078Profile.build)==0) return &kSteam26277078Profile;
  return nullptr;
}

std::uint32_t Address(const void* pointer) {
  return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
}
void Word(BYTE* out, std::uint32_t value) { std::memcpy(out, &value, 4); }
void Relative(BYTE* code, BYTE opcode, const void* target) {
  code[0] = opcode;
  Word(code+1, Address(target) - Address(code+5)); // x86 wraparound rel32
}
void AtomicFloat(float& target, float value) {
  LONG bits = 0;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  InterlockedExchange(reinterpret_cast<volatile LONG*>(&target), bits);
}
void Mul(BYTE* code, BYTE xmm, const float* factor) {
  const BYTE bytes[]{0xf3,0x0f,0x59,static_cast<BYTE>(0x05 | (xmm<<3))};
  std::memcpy(code, bytes, 4); Word(code+4, Address(factor));
}
#if defined(CC2_ANIMATION_TRACE)
void EmitRouteHitCounter(BYTE* code, volatile LONG* counter) {
  // Preserve EFLAGS around LOCK INC; no general-purpose or XMM registers change.
  code[0] = 0x9c;
  const BYTE increment[]{0xf0,0xff,0x05};
  std::memcpy(code+1, increment, sizeof(increment));
  Word(code+4, Address(const_cast<LONG*>(counter)));
  code[8] = 0x9d;
}
#endif
BYTE* Cave() {
  if (!g_code || g_codeUsed + 64 > 4096) return nullptr;
  BYTE* result = g_code + g_codeUsed;
  g_codeUsed += 64;
  return result;
}
Patch* Expect(BYTE* base, std::size_t imageBytes, std::uintptr_t rva,
              const BYTE* bytes, std::size_t size) {
  if (g_count >= g_patches.size() || size > 10 || rva > imageBytes || size > imageBytes-rva)
    return nullptr;
  if (std::memcmp(base+rva, bytes, size) != 0) return nullptr;
  auto& patch = g_patches[g_count++];
  patch.site = base+rva; patch.size = size;
  std::memcpy(patch.expected, bytes, size);
  std::memcpy(patch.replacement, bytes, size);
  return &patch;
}
bool Detour(Patch* patch, BYTE* cave) {
  if (!patch || !cave) return false;
  std::memset(patch->replacement, 0x90, patch->size);
  patch->replacement[0] = 0xe9;
  Word(patch->replacement+1, Address(cave)-Address(patch->site+5));
  return true;
}
bool Prepare(BYTE* base, std::size_t imageBytes, const Profile& profile) {
  // Largest referenced constant must be mapped before dereferencing any table.
  std::uintptr_t maxData=profile.routeConstant;
  for(auto value:profile.liftSources) if(value>maxData) maxData=value;
  for(auto value:profile.craneSources) if(value>maxData) maxData=value;
  if (maxData > imageBytes || imageBytes-maxData < sizeof(float)) return false;
  const BYTE window[]{0xf3,0x0f,0x59,0xc7,0x8b,0x8d,0x00,0xfe,0xff,0xff};
  const auto& ab=profile.routeWindowSites;
  for (int i=0;i<2;++i) {
    auto* p = Expect(base,imageBytes,ab[i],window,10); BYTE* c=Cave();
    if (!p || !c) return false;
    BYTE* body=c;
#if defined(CC2_ANIMATION_TRACE)
    if(i==1){EmitRouteHitCounter(body,&g_routeHitCounts[0]);body+=9;}
#endif
    std::memcpy(body,window,4); Mul(body+4,0,i==0?&g_runway:&g_routes);
    std::memcpy(body+12,window+4,6); Relative(body+18,0xe9,p->site+10);
    if (!Detour(p,c)) return false;
  }
  for (std::size_t i=0;i<std::size(profile.routeSites);++i) {
    BYTE original[]{0xf3,0x0f,0x59,0x0d,0,0,0,0}; Word(original+4,Address(base+profile.routeConstant));
    auto* p=Expect(base,imageBytes,profile.routeSites[i],original,8); BYTE* c=Cave();
    if (!p || !c) return false;
    BYTE* body=c;
#if defined(CC2_ANIMATION_TRACE)
    if(i<2){EmitRouteHitCounter(body,&g_routeHitCounts[i+1]);body+=9;}
#endif
    std::memcpy(body,original,8); Mul(body+8,1,i>=2?&g_runway:&g_routes);
    Relative(body+16,0xe9,p->site+8);
    if (!Detour(p,c)) return false;
  }
  {
    const BYTE original[]{0xf3,0x0f,0x5e,0x40,0x78};
    auto* p=Expect(base,imageBytes,profile.directSite,original,5); BYTE* c=Cave();
    if (!p || !c) return false;
    // Preserve all of XMM1, ESP and EFLAGS; LEA is flag-neutral.
    const BYTE code[]{0x8d,0x64,0x24,0xf0, 0x0f,0x11,0x0c,0x24,
      0xf3,0x0f,0x10,0x48,0x78, 0xf3,0x0f,0x59,0x0d,0,0,0,0,
      0xf3,0x0f,0x5e,0xc1, 0x0f,0x10,0x0c,0x24, 0x8d,0x64,0x24,0x10};
    std::memcpy(c,code,sizeof(code)); Word(c+17,Address(&g_runway));
    Relative(c+33,0xe9,p->site+5);
    if (!Detour(p,c)) return false;
  }
  for (std::size_t i=0;i<std::size(profile.liftSites);++i) {
    BYTE original[]{0xf3,0x0f,static_cast<BYTE>(i?0x5f:0x10),static_cast<BYTE>(i?0x0d:0x1d),0,0,0,0};
    Word(original+4,Address(base+profile.liftSources[i]));
    auto* p=Expect(base,imageBytes,profile.liftSites[i],original,8);
    if (!p) return false;
    std::memcpy(&g_originalLift[i],base+profile.liftSources[i],4); g_lift[i]=g_originalLift[i];
    Word(p->replacement+4,Address(&g_lift[i]));
  }
  for (std::size_t i=0;i<std::size(profile.craneOperands);++i) {
    BYTE original[]{0xf3,0x0f,0x10,static_cast<BYTE>((i%2)?0x0d:0x15),0,0,0,0};
    Word(original+4,Address(base+profile.craneSources[i]));
    auto* p=Expect(base,imageBytes,profile.craneOperands[i]-4,original,8);
    if (!p) return false;
    std::memcpy(&g_originalCrane[i],base+profile.craneSources[i],4); g_crane[i]=g_originalCrane[i];
    Word(p->replacement+4,Address(&g_crane[i]));
  }
  for (std::size_t i=0;i<std::size(profile.craneCalls);++i) {
    BYTE original[5]{0xe8};
    Word(original+1,Address(base+profile.clampTarget)-Address(base+profile.craneCalls[i]+5));
    auto* p=Expect(base,imageBytes,profile.craneCalls[i],original,5); BYTE* c=Cave();
    if (!p || !c) return false;
    // Original clamp call and XMM0 result; restore the ORIGINAL normalization
    // denominator for downstream completion logic, as in the fixed v5 patch.
    Relative(c,0xe8,base+profile.clampTarget);
    const BYTE mov[]{0xf3,0x0f,0x10,0x15}; std::memcpy(c+5,mov,4);
    Word(c+9,Address(base+profile.craneRestore[i])); Relative(c+13,0xe9,p->site+5);
    if (!Detour(p,c)) return false;
  }
  {
    BYTE original[5]{0xe8}; Word(original+1,Address(base+profile.factorTarget)-Address(base+profile.factorCallSite+5));
    auto* p=Expect(base,imageBytes,profile.factorCallSite,original,5); BYTE* c=Cave();
    if (!p || !c) return false;
    Mul(c,6,&g_craneFactor);
    const BYTE cap[]{0xf3,0x0f,0x5d,0x35}; std::memcpy(c+8,cap,4); Word(c+12,Address(&g_factorCap));
    Relative(c+16,0xe8,base+profile.factorTarget); Relative(c+21,0xe9,p->site+5);
    if (!Detour(p,c)) return false;
  }
  return g_count == kCount;
}

#ifdef CC2_ANIMATION_TESTING
struct Threads {
  std::array<HANDLE,256> handles{};
  std::size_t count=0, suspended=0;
  bool Collect() {
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(snap==INVALID_HANDLE_VALUE) return false;
    THREADENTRY32 e{sizeof(e)}; bool ok=Thread32First(snap,&e)!=FALSE;
    if(ok) do {
      if(e.th32OwnerProcessID!=GetCurrentProcessId() || e.th32ThreadID==GetCurrentThreadId()) continue;
      if(count==handles.size()){ok=false;break;}
      HANDLE h=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,e.th32ThreadID);
      if(!h){ok=false;break;} handles[count++]=h;
    }while(Thread32Next(snap,&e));
    CloseHandle(snap); return ok;
  }
  bool Pause() {
    for(std::size_t i=0;i<count;++i){
      if(SuspendThread(handles[i])==DWORD(-1)) return false;
      ++suspended; CONTEXT c{}; c.ContextFlags=CONTEXT_CONTROL;
      if(!GetThreadContext(handles[i],&c)) return false;
      for(const auto& p:g_patches)
        if(c.Eip>=Address(p.site) && c.Eip<Address(p.site+p.size)) return false;
    }
    return true;
  }
  void Release() {
    while(suspended) ResumeThread(handles[--suspended]);
    while(count) CloseHandle(handles[--count]);
  }
};

DWORD Install(BYTE* base, std::size_t bytes, bool pauseThreads, const Profile& profile) {
  if(IsReady()) return ERROR_ALREADY_INITIALIZED;
  g_count=0; g_codeUsed=0;
  g_code=static_cast<BYTE*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
  if(!g_code) return ERROR_NOT_ENOUGH_MEMORY;
  if(!Prepare(base,bytes,profile)) { VirtualFree(g_code,0,MEM_RELEASE);g_code=nullptr;return ERROR_REVISION_MISMATCH; }
  DWORD oldCode=0;
  if(!VirtualProtect(g_code,4096,PAGE_EXECUTE_READ,&oldCode) ||
     !FlushInstructionCache(GetCurrentProcess(),g_code,g_codeUsed)) return ERROR_WRITE_FAULT;
  Threads threads;
  if(pauseThreads && !threads.Collect()){threads.Release();return ERROR_BUSY;}
  // Prepare permissions before suspending. No allocator, file logger, STL
  // container mutation or loader call is used while other threads are paused.
  std::array<DWORD,kCount> protections{};
  std::size_t writable=0;
  for(;writable<g_count;++writable){
    auto& p=g_patches[writable];
    if(!VirtualProtect(p.site,p.size,PAGE_EXECUTE_READWRITE,&protections[writable])) break;
  }
  DWORD result=writable==g_count?ERROR_SUCCESS:ERROR_ACCESS_DENIED;
  if(result==ERROR_SUCCESS && pauseThreads && !threads.Pause()) result=ERROR_BUSY;
  if(result==ERROR_SUCCESS){
    for(const auto& p:g_patches) if(std::memcmp(p.site,p.expected,p.size)!=0) result=ERROR_REVISION_MISMATCH;
    if(result==ERROR_SUCCESS){
      for(const auto& p:g_patches) std::memcpy(p.site,p.replacement,p.size);
      if(!FlushInstructionCache(GetCurrentProcess(),base,bytes)) {
        for(const auto& p:g_patches) std::memcpy(p.site,p.expected,p.size);
        FlushInstructionCache(GetCurrentProcess(),base,bytes);
        result=ERROR_WRITE_FAULT;
      }
    }
  }
  // Multiple sites share a page: restore permissions in reverse acquisition order.
  while(writable){ --writable; DWORD unused=0;
    if(!VirtualProtect(g_patches[writable].site,g_patches[writable].size,protections[writable],&unused) && result==ERROR_SUCCESS)
      result=ERROR_WRITE_FAULT;
  }
  threads.Release();
  // Keep cave storage until process exit even on an uncertain failure. Never
  // free executable memory that a partially installed detour might reference.
  if(result==ERROR_SUCCESS) InterlockedExchange(&g_ready,1);
  return result;
}

bool MatchHash(const wchar_t* path, const char* expectedHash) {
  BCRYPT_ALG_HANDLE alg=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
  if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return false;
  bool ok=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0;
  HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE) ok=false;
  std::array<BYTE,65536> buffer{}; std::array<BYTE,32> digest{};
  while(ok){DWORD n=0;if(!ReadFile(file,buffer.data(),static_cast<DWORD>(buffer.size()),&n,nullptr)){ok=false;break;}
    if(!n) break; ok=BCryptHashData(hash,buffer.data(),n,0)>=0;}
  if(ok) ok=BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
  char hex[65]{}; constexpr char chars[]="0123456789abcdef";
  for(std::size_t i=0;i<digest.size();++i){hex[i*2]=chars[digest[i]>>4];hex[i*2+1]=chars[digest[i]&15];}
  if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
  if(hash)BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg,0);
  return ok && std::strcmp(hex,expectedHash)==0;
}

#endif
} // namespace

bool IsReady(){return InterlockedCompareExchange(&g_ready,0,0)==1;}
DWORD InitializeWithApi(const cc2x_api* api) {
  if(IsReady()) return ERROR_ALREADY_INITIALIZED;
  if(!api || api->api_version<5 || api->size<offsetof(cc2x_api,patch_bytes)+sizeof(api->patch_bytes) || !api->patch_bytes || !api->patch_begin ||
     !api->patch_abort || !api->patch_commit || !api->module_base) return ERROR_NOT_SUPPORTED;
  auto* base=reinterpret_cast<BYTE*>(api->module_base);
  const auto* profile=SelectProfile(api->game_build);
  if(!profile) return ERROR_NOT_SUPPORTED;
  const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
  g_count=0;g_codeUsed=0;
  g_code=static_cast<BYTE*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
  if(!g_code) return ERROR_NOT_ENOUGH_MEMORY;
  if(!Prepare(base,pe->OptionalHeader.SizeOfImage,*profile)) { VirtualFree(g_code,0,MEM_RELEASE);g_code=nullptr;return ERROR_REVISION_MISMATCH; }
  DWORD old=0;
  if(!VirtualProtect(g_code,4096,PAGE_EXECUTE_READ,&old) ||
     !FlushInstructionCache(GetCurrentProcess(),g_code,g_codeUsed)) return ERROR_WRITE_FAULT;
  api->patch_begin("native_bridge.animation");
  for(const auto& p:g_patches) {
    const auto va=static_cast<std::uint32_t>(p.site-base+0x400000);
    if(!api->patch_bytes(va,static_cast<std::uint32_t>(p.size),p.expected,p.replacement)) {
      api->patch_abort();return ERROR_REVISION_MISMATCH;
    }
  }
  if(!api->patch_commit()) return ERROR_WRITE_FAULT;
  InterlockedExchange(&g_ready,1);return ERROR_SUCCESS;
}
void ReadRoutePatchHitCounts(std::uint32_t counts[3]) {
  if(!counts)return;
#if defined(CC2_ANIMATION_TRACE)
  for(std::size_t i=0;i<3;++i)
    counts[i]=static_cast<std::uint32_t>(InterlockedCompareExchange(&g_routeHitCounts[i],0,0));
#else
  counts[0]=counts[1]=counts[2]=0;
#endif
}
bool SetSpeed(Group group,std::uint32_t value){
  if(!IsReady() || value<1 || static_cast<std::uint32_t>(group)>3) return false;
  // These coefficients are consumed as float by the game. The old 10/50/100
  // ceilings were test-profile limits, not a native ABI constraint.
  if(group==Group::kLift)
    for(float original:g_originalLift)if(!std::isfinite(original*static_cast<float>(value)))return false;
  if(group==Group::kCrane)
    for(float original:g_originalCrane)if(!std::isfinite(original*static_cast<float>(value)))return false;
  AcquireSRWLockExclusive(&g_settingsLock);
  switch(group){
    case Group::kRoutes: AtomicFloat(g_routes,1.0f/value);break;
    case Group::kRunway: AtomicFloat(g_runway,1.0f/value);break;
    case Group::kLift: for(std::size_t i=0;i<g_lift.size();++i) AtomicFloat(g_lift[i],g_originalLift[i]*value);break;
    case Group::kCrane:
      for(std::size_t i=0;i<g_crane.size();++i) AtomicFloat(g_crane[i],g_originalCrane[i]*value);
      AtomicFloat(g_craneFactor,static_cast<float>(value));break;
  }
  ReleaseSRWLockExclusive(&g_settingsLock);return true;
}
#ifdef CC2_ANIMATION_TESTING
DWORD InstallFixture(void* base,std::size_t bytes,const char* build){
  const auto* profile=SelectProfile(build);
  return profile?Install(static_cast<BYTE*>(base),bytes,true,*profile):ERROR_NOT_SUPPORTED;
}
bool VerifyFixtureTarget(const wchar_t* path){return MatchHash(path,kLegacyHash)||MatchHash(path,kCurrentBuildHash);}
bool FixtureHasLegacyPatchProfile(const wchar_t* path){return MatchHash(path,kLegacyHash);}
#endif
} // namespace cc2::api::animation
