#pragma once
#include "cc2x.h"
#include "cc2x_framework.h"
#include "lifetime_policy.h"
#include "mod_loader.h"
#include "early_declaration.h"
#include "module_mailbox.h"
#include "api/vehicle/buttons.h"
#include "api/carrier/recovery.h"
#include "module_settings.h"
#include <Windows.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <map>
#include <string>
#include <vector>
#include <functional>
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
  explicit Host(bool(*inputSampler)(int)=nullptr,api::carrier::Recovery::DockInvoker dockInvoker=nullptr);
  ~Host();
  bool Attach(const cc2x_api* api, bool installNative=true,bool deferCatalog=false);
  void PrepareStartupCatalog(); // startup worker; fixtures may explicitly finish a deferred catalog
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
  ModuleMailbox& Mailbox(){return mailbox_;}
  int WorldEvent(const char* name,double a,double b=0,double c=0,int neutral=0,bool decision=false);
  void ButtonsEvent(api::vehicle::ButtonContext* context);
  int ButtonQueryEvent(api::vehicle::ButtonContext* context);
  bool HasEventSubscribers(const char* name);
  bool EventAvailable(const std::string& name,bool rules) const;
  std::uint64_t SubscribeNative(const std::string& event,cc2x_framework_event_fn callback,void* user);
  bool UnsubscribeNative(std::uint64_t subscription);
  bool InServerContext() const {return attached_&&!stopped_&&server_&&OnRuntimeThread();}
  std::uint64_t BindNative(const std::string& owner,const std::string& name,cc2x_vm_fn callback,void* user);
  bool UnbindNative(std::uint64_t binding);
  bool FindNativeBinding(std::uint64_t binding,cc2x_vm_fn* callback,void** user);
  std::uint64_t OpenNativeMailbox(const std::string& owner);
  bool CloseNativeMailbox(std::uint64_t mailbox);
  int PollNativeMailbox(std::uint64_t mailbox,cc2x_message* out,std::uint32_t capacity);
  bool PublishNativeMailbox(std::uint64_t mailbox,const std::string& topic,std::uint32_t subject,double value);
  bool ForgetNativeMailbox(std::uint64_t mailbox,std::uint32_t subject);
  bool RegisterNativeEntity(const std::string& owner,const char* id,const char* base,const char* definition);
  unsigned long long ActiveGeneration();
  unsigned long long ActiveTick();
  std::uint64_t PostTask(const std::string& owner,std::uint64_t generation,std::function<void()> run,std::function<void()> cleanup={});
  bool CancelTask(const std::string& owner,std::uint64_t task);
  void DropTasks(const std::string& owner);
  bool ReadVehicleThrottle(std::uint32_t vehicle,double* value);
  bool WriteVehicleThrottle(std::uint32_t vehicle,double value);
  api::carrier::Recovery& Carrier(){return recovery_;}
  ModuleSettings* Settings(const std::string& owner);
  bool SettingsIo(const std::string& owner);
  bool DescribeNativePackage(const std::string& owner,const std::wstring& pluginDirectory,host::ModDescriptor* package);
  bool CommitNativeDeclarations(const std::vector<host::ModDescriptor>& packages,const std::vector<host::VehicleDeclaration>& declarations);
  api::vehicle::ButtonContext* CurrentButtons(){return InLuaContext()?buttonsContext_:nullptr;}
 private:
  void Load();
  void LoadRules();
  static DWORD WINAPI PrepareStartupCatalogThread(void* context);
  void CloseModules();
  bool Call(Module& m,const char* name,double dt,bool argument);
  void Register(Module& m);
  void RegisterNativeBindings(Module& m);
  cc2x_api api_{};
  api::carrier::Recovery recovery_;
  std::map<std::string,std::unique_ptr<ModuleSettings>> settings_;
  struct SettingsBudget {ULONGLONG second=0;unsigned count=0;};
  std::map<std::string,SettingsBudget> settingsBudgets_;
  std::recursive_mutex mutex_;
  std::vector<std::unique_ptr<Module>> modules_;
  std::vector<std::unique_ptr<Module>> ruleModules_;
  bool rulesLoaded_=false;
  bool nativeDeclarationsFrozen_=false;
  std::map<std::wstring,host::ModDescriptor> nativePackages_;
  std::vector<host::VehicleDeclaration> nativeDeclarations_;
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
  ModuleMailbox mailbox_;
  std::map<std::string,bool> eventCapabilities_;
  api::vehicle::ButtonContext* buttonsContext_=nullptr; // stack-owned during one engine phase
  struct NativeListener {std::uint64_t id;std::string event;cc2x_framework_event_fn callback;void* user;};
  struct NativeBinding {std::uint64_t id;std::string owner,name;cc2x_vm_fn callback;void* user;};
  std::vector<NativeListener> nativeListeners_;
  std::vector<NativeBinding> nativeBindings_;
  std::map<std::uint64_t,std::string> nativeMailboxes_;
  std::vector<std::string> nativeEntityOwners_;
  struct Task {std::uint64_t id,generation;std::string owner;std::function<void()> run,cleanup;};
  std::vector<Task> tasks_;
  void RunTasks(std::uint64_t cutoff);
  std::uint64_t nextNativeId_=0;
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
Host* SharedHost(); // process-lifetime integrated host; null without the framework
int InvokeNativeBinding(lua_State* L,Host* host,std::uint64_t binding);
}
