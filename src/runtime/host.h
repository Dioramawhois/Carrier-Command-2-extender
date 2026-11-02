#pragma once
#include "cc2x.h"
#include "lifetime_policy.h"
#include "mod_loader.h"
#include "early_declaration.h"
#include <Windows.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
struct lua_State;
namespace cc2::runtime {
struct Module;
struct Metrics {
  unsigned long long generation=0, vmGeneration=0, updates=0, callbacks=0, errors=0;
  unsigned long long requested=0, applied=0, dropped=0, wrongThread=0, loads=0, unloads=0;
  std::size_t modules=0;
};
struct WorldReadMetrics {
  unsigned long long calls=0, failures=0, wrongThread=0, objects=0, microseconds=0, maxMicroseconds=0, loggedErrors=0;
};
class Host {
 public:
  explicit Host(bool(*inputSampler)(int)=nullptr);
  ~Host();
  bool Attach(const cc2x_api* api, bool installNative=true);
  void Session(void* server, bool started);
  void Tick(void* server);
  void AfterCarrierUpdate(void* carrier);
  bool QueueThrottle(int step);
  struct HangarSlotOption { std::uint32_t index; bool compatible; bool occupied; };
  struct HangarSpawnResult { std::uint32_t requestId; bool success; std::uint32_t vehicleId; std::uint32_t errorCode; };
  std::uint32_t QueueHangarSpawn(std::uint32_t carrierId,std::uint32_t slot,std::uint32_t chassisType);
  bool FindHangarSpawnResult(std::uint32_t requestId,HangarSpawnResult* result);
  void RecordHangarSpawnResult(std::uint32_t requestId,int success,std::uint32_t vehicleId,std::uint32_t errorCode);
  bool DescribeHangarSlots(std::uint32_t carrierId,std::uint32_t chassisType,
      std::vector<HangarSlotOption>* options,std::string* reason);
  void ProcessHangarSpawn(void* server);
  void CompleteHangarSpawn();
  void LogHangarSpawn(const char* event,const char* detail="");
  bool KeyDown(int key) const;
  void RequestReload();
  void ObserveGameResourceInit();
  void Shutdown();
  Metrics Stats();
  bool InLuaContext() const;
  bool OnRuntimeThread() const {return thread_==GetCurrentThreadId();}
  const cc2x_api* Api() const {return &api_;}
  unsigned long long Generation() const {return metrics_.generation;}
  unsigned long long TickId() const {return metrics_.updates;}
  unsigned long long SessionTickId() const {return sessionTicks_;}
  void RecordWorldRead(const char* operation,const char* error,unsigned long long microseconds,unsigned long long objects,bool success,bool wrongThread);
  void Log(const char* event,const char* mod,const char* detail="");
  std::string ResolveEarlyVehicle(const std::string& owner,const std::string& localId) const;
 private:
  void Load();
  void PrepareStartupCatalog();
  static DWORD WINAPI PrepareStartupCatalogThread(void* context);
  void CloseModules();
  bool Call(Module& m,const char* name,double dt,bool argument);
  void Register(Module& m);
  cc2x_api api_{};
  std::recursive_mutex mutex_;
  std::vector<std::unique_ptr<Module>> modules_;
  host::ModDiscovery startupCatalog_;
  host::DeclarationCatalog earlyCatalog_;
  volatile LONG startupCatalogState_=0; // 0=pending, 1=ready, -1=failed
  volatile LONG resourceInitObserved_=0;
  volatile LONG resourceInitCatalogState_=0;
  volatile LONG resourceInitThread_=0;
  bool resourceInitLogged_=false;
  bool runtimeInitialLoadDone_=false;
  std::vector<HangarSpawnResult> hangarSpawnResults_;
  lifetime::EpochQueue commands_;
  Metrics metrics_{};
  WorldReadMetrics worldReads_{};
  void* server_=nullptr; // identity only; never dereferenced or exposed to Lua
  DWORD thread_=0;
  unsigned long long threadGeneration_=0;
  unsigned long long sessionTicks_=0;
  bool attached_=false, loaded_=false, reload_=false, inLua_=false, stopped_=false;
  bool nativeExtensionsReady_=false;
  bool hangarSpawnRequested_=false, hangarSpawnInFlight_=false;
  bool hangarSpawnUnsafeLatched_=false, hangarSpawnRollbackPending_=false;
  bool hangarSpawnResultSuccess_=false, hangarSpawnResultSent_=false;
  unsigned long long hangarSpawnRequestGeneration_=0, hangarSpawnAttemptGeneration_=0;
  std::uint32_t hangarSpawnRequestId_=0,hangarSpawnResultVehicleId_=0,hangarSpawnResultError_=0;
  std::uint32_t hangarSpawnCarrierId_=0,hangarSpawnSlot_=0,hangarSpawnType_=0;
  unsigned long long hangarSpawnRollbackId_=0;
  unsigned hangarSpawnRollbackTicks_=0;
  ULONGLONG lastTick_=0,lastReport_=0;
  std::wstring storage_;
  bool(*inputSampler_)(int)=nullptr;
};
bool InstallNativeExtensions(const cc2x_api* api,Host* host);
void RegisterWorld(lua_State* L,Host* host);
}
