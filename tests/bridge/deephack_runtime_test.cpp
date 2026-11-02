#include "runtime/host.h"
#include "api/animation/animation_speed.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstring>
#include <cstdarg>
#include <iostream>
extern "C" void cc2x_native_hangar_spawn_result(std::uint32_t,int,std::uint32_t,std::uint32_t) {}
namespace fs=std::filesystem;
namespace {
std::vector<std::wstring> roots;std::vector<std::string> logs;
bool up=false,right=false;void* image=nullptr;
struct Patch {unsigned char* site;std::vector<unsigned char> before,after;};
std::vector<Patch> pending;
void Begin(const char*){pending.clear();}
void Abort(){pending.clear();}
int Bytes(uint32_t va,uint32_t n,const uint8_t*expected,const uint8_t*replacement){
  auto*p=static_cast<unsigned char*>(image)+va-0x400000;
  if(memcmp(p,expected,n))return 0;
  pending.push_back({p,{expected,expected+n},{replacement,replacement+n}});return 1;
}
int Commit(){for(auto&p:pending)if(memcmp(p.site,p.before.data(),p.before.size()))return 0;for(auto&p:pending)memcpy(p.site,p.after.data(),p.after.size());return 1;}
bool Keys(int key){return key==VK_UP?up:key==VK_RIGHT?right:false;}
void Log(const char*,int,const char*format,...){char s[8192];va_list a;va_start(a,format);vsnprintf(s,sizeof(s),format,a);va_end(a);logs.emplace_back(s);}
void TickRegister(void(*)(void*,void*),void*){}
void SessionRegister(void(*)(void*,int,void*),void*){}
int Count(){return static_cast<int>(roots.size());}
int Path(int i,wchar_t*out,uint32_t n){if(i<0||i>=Count()||roots[i].size()>=n)return 0;wcscpy_s(out,n,roots[i].c_str());return 1;}
bool Has(const char*s){for(auto&v:logs)if(v.find(s)!=std::string::npos)return true;return false;}
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL line "<<__LINE__<<": "<<#x<<"\n";for(auto&s:logs)std::cerr<<s<<"\n";return 1;}}while(0)
}
int wmain(int argc,wchar_t**argv){
  if(argc!=3)return 2;
  // Map the audited file as PRIVATE DATA only. No game function/entry is executed.
  std::ifstream in(argv[1],std::ios::binary);std::vector<char>file{std::istreambuf_iterator<char>(in),{}};
  CHECK(file.size()>4096);auto*dos=reinterpret_cast<IMAGE_DOS_HEADER*>(file.data());
  auto*pe=reinterpret_cast<IMAGE_NT_HEADERS32*>(file.data()+dos->e_lfanew);
  CHECK(pe->Signature==IMAGE_NT_SIGNATURE&&pe->FileHeader.Machine==IMAGE_FILE_MACHINE_I386);
  const bool legacyAnimationProfile=pe->FileHeader.TimeDateStamp==0x6a36c177;
  const char* build=legacyAnimationProfile?"1.5.18-win32":"1.5.18-win32-r2";
  image=VirtualAlloc(nullptr,pe->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);CHECK(image);
  auto*data=static_cast<unsigned char*>(image);memcpy(data,file.data(),pe->OptionalHeader.SizeOfHeaders);
  auto*section=IMAGE_FIRST_SECTION(pe);for(int i=0;i<pe->FileHeader.NumberOfSections;++i)if(section[i].SizeOfRawData)memcpy(data+section[i].VirtualAddress,file.data()+section[i].PointerToRawData,section[i].SizeOfRawData);
  auto delta=static_cast<DWORD>(reinterpret_cast<uintptr_t>(image))-pe->OptionalHeader.ImageBase;
  auto reloc=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
  for(size_t p=0;p+8<=reloc.Size;){auto*block=reinterpret_cast<IMAGE_BASE_RELOCATION*>(data+reloc.VirtualAddress+p);CHECK(block->SizeOfBlock>=8&&block->SizeOfBlock<=reloc.Size-p);auto*w=reinterpret_cast<WORD*>(block+1);for(size_t i=0;i<(block->SizeOfBlock-8)/2;++i)if((w[i]>>12)==IMAGE_REL_BASED_HIGHLOW)*reinterpret_cast<DWORD*>(data+block->VirtualAddress+(w[i]&0xfff))+=delta;p+=block->SizeOfBlock;}
  wchar_t temp[MAX_PATH],oldLocal[32768];GetTempPathW(MAX_PATH,temp);auto root=fs::path(temp)/(L"nb_deephack_"+std::to_wstring(GetCurrentProcessId()));
  fs::create_directories(root/L"CarrierCommand2/CoreBridge");GetEnvironmentVariableW(L"LOCALAPPDATA",oldLocal,32768);SetEnvironmentVariableW(L"LOCALAPPDATA",root.c_str());
  roots={fs::absolute(argv[2]).wstring()};std::string config=root.string();
  cc2x_api api{};api.api_version=CC2X_API_VERSION;api.size=sizeof(api);api.game_build=build;api.module_base=reinterpret_cast<uintptr_t>(image);api.config_dir=config.c_str();api.log=Log;
  api.on_server_tick=TickRegister;api.on_session=SessionRegister;api.active_mod_count=Count;api.active_mod_path=Path;api.patch_begin=Begin;api.patch_abort=Abort;api.patch_bytes=Bytes;api.patch_commit=Commit;
  const auto animationStatus=cc2::api::animation::InitializeWithApi(&api);
  CHECK(animationStatus==ERROR_SUCCESS&&pending.size()==26);
  cc2::runtime::Host host(Keys);CHECK(host.Attach(&api,false));auto server=reinterpret_cast<void*>(0x12340000);host.Session(server,true);host.Tick(server);
  CHECK(host.Stats().modules==1&&host.Stats().errors==0);
  alignas(16) unsigned char carrier[0xD00]{};auto*throttle=reinterpret_cast<float*>(carrier+0xC94);
  up=true;host.Tick(server);host.AfterCarrierUpdate(carrier);CHECK(*throttle==0.025f);host.Tick(server);host.AfterCarrierUpdate(carrier);CHECK(*throttle==0.025f);
  up=false;host.Tick(server);up=true;host.Tick(server);host.AfterCarrierUpdate(carrier);CHECK(*throttle==0.05f);
  right=true;host.Tick(server);
  CHECK(Has("movement x50 applied=true")&&Has("lift x50=applied, crane x10=applied"));
  right=false;up=false;host.Tick(server);right=true;host.Tick(server);
  CHECK(Has("movement x1 applied=true")&&Has("lift x1=applied, crane x1=applied"));
  up=true;host.Tick(server);CHECK(host.Stats().requested==3);host.Session(server,false);CHECK(host.Stats().dropped==1);
  host.Session(server,true);up=false;right=false;host.Tick(server);CHECK(host.Stats().loads==1);up=true;host.Tick(server);host.AfterCarrierUpdate(carrier);CHECK(*throttle==0.075f);
  CHECK(host.Stats().callbacks>=host.Stats().updates*4&&host.Stats().errors==0);host.Shutdown();
  std::ifstream diag(root/L"CarrierCommand2/CoreBridge/deephack-lifecycle.jsonl");std::string lines{std::istreambuf_iterator<char>(diag),{}};
  CHECK(lines.find("hook.controls.carrier")!=std::string::npos&&lines.find("hook.movement.parking")!=std::string::npos&&lines.find("hook.movement.mechanisms")!=std::string::npos&&lines.find("on_unload")!=std::string::npos);
  SetEnvironmentVariableW(L"LOCALAPPDATA",oldLocal);std::error_code ec;fs::remove_all(root,ec);VirtualFree(image,0,MEM_RELEASE);
  std::cout<<"PASS Deephack Lua + throttle + authenticated animation profile ("<<build<<")\n";
}
