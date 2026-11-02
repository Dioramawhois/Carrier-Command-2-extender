#include "definition_registry.h"

#include <Windows.h>
#include <algorithm>
#include <limits>
#include <set>

namespace cc2::api::vehicle {
namespace {

std::string Utf8(const std::wstring& value) {
  const int length=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
  std::string result(static_cast<std::size_t>(length),'\0');
  if (length) WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),length,nullptr,nullptr);
  return result;
}

}  // namespace

bool DefinitionRegistry::Publish(std::vector<host::VehicleDeclaration> declarations,
                                 std::uint32_t firstId,std::string* error) {
  if (published_.load(std::memory_order_acquire)) {if (error) *error="registry already published";return false;}
  constexpr std::uint32_t stride=0x220;
  const auto lastRepresentable=UINT32_MAX/stride;
  if (firstId<102 || firstId>lastRepresentable ||
      declarations.size()>static_cast<std::size_t>(lastRepresentable-firstId)+1u) {
    if (error) *error="definition IDs cannot be represented by the verified record offset";
    return false;
  }
  // The engine persists numeric definition IDs. Normalize the declaration
  // sequence before allocating contiguous IDs so the same active identity set
  // does not depend on mod discovery or Lua declaration order. An explicit
  // order is a migration hint (not an ID); all other entries sort by identity.
  std::sort(declarations.begin(),declarations.end(),[](const auto& left,const auto& right) {
    if (left.order!=right.order) return left.order<right.order;
    return left.identity<right.identity;
  });
  std::set<std::wstring> identities,resources;
  for (const auto& value:declarations) {
    if (value.identity.empty() || value.resourceName.empty() ||
        (value.nativeBaseType!=8 && value.nativeBaseType!=10) ||
        !identities.insert(value.identity).second ||
        !resources.insert(value.resourceName).second) {
      if (error) *error="duplicate or invalid vehicle declaration";
      return false;
    }
  }
  auto records=std::make_unique<DefinitionRecord[]>(declarations.size());
  for (std::size_t i=0;i<declarations.size();++i) {
    declarations[i].definitionId=firstId+static_cast<std::uint32_t>(i);
    records[i].descriptor=std::move(declarations[i]);
    records[i].resourceName=Utf8(records[i].descriptor.resourceName);
    records[i].displayName=Utf8(records[i].descriptor.name);
  }
  records_=std::move(records);
  firstId_=firstId;count_=declarations.size();published_.store(true,std::memory_order_release);
  return true;
}

bool DefinitionRegistry::MatchesCatalog(
    std::vector<host::VehicleDeclaration> declarations,std::string* error) const {
  if (!published_.load(std::memory_order_acquire)) {
    if (error) *error="definition registry is not published";
    return false;
  }
  if (declarations.size()!=count_) {
    if (error) *error="vehicle declaration count changed (expected "+
        std::to_string(count_)+", got "+std::to_string(declarations.size())+")";
    return false;
  }
  std::sort(declarations.begin(),declarations.end(),[](const auto& left,const auto& right) {
    if (left.order!=right.order) return left.order<right.order;
    return left.identity<right.identity;
  });
  for (std::size_t i=0;i<count_;++i) {
    const auto& expected=records_[i].descriptor;
    const auto& actual=declarations[i];
    if (expected.owner!=actual.owner || expected.localId!=actual.localId ||
        expected.identity!=actual.identity || expected.name!=actual.name ||
        expected.definitionPath!=actual.definitionPath ||
        expected.resourceName!=actual.resourceName || expected.ui!=actual.ui ||
        expected.nativeBaseType!=actual.nativeBaseType || expected.order!=actual.order) {
      if (error) *error="vehicle declaration changed for identity "+Utf8(expected.identity);
      return false;
    }
  }
  return true;
}

const DefinitionRecord* DefinitionRegistry::Find(std::uint32_t id) const {
  if (!published_.load(std::memory_order_acquire) || id<firstId_ || id>=Limit()) return nullptr;
  return &records_[id-firstId_];
}

const DefinitionRecord* DefinitionRegistry::Find(const std::wstring& identity) const {
  if (!published_.load(std::memory_order_acquire)) return nullptr;
  std::wstring resolvedIdentity=identity;
  const auto separator=identity.find(L':');
  if(separator!=std::wstring::npos) {
    if(separator==0||separator+1>=identity.size()||
        identity.find(L':',separator+1)!=std::wstring::npos)return nullptr;
    resolvedIdentity=identity.substr(0,separator)+L"."+identity.substr(separator+1);
  }
  for (std::size_t i=0;i<count_;++i)
    if (records_[i].descriptor.identity==resolvedIdentity) return &records_[i];
  return nullptr;
}

void DefinitionRegistry::BeginLoad() {
  if (!published_.load(std::memory_order_acquire)) return;
  for (std::size_t i=0;i<count_;++i) records_[i].state.store(0,std::memory_order_release);
}

bool DefinitionRegistry::Mark(std::uint32_t id,DefinitionState state) {
  auto* record=const_cast<DefinitionRecord*>(Find(id));
  if (!record) return false;
  const auto current=record->state.load(std::memory_order_acquire);
  if (state==ArrayInitialized && !(current&NameRegistered)) return false;
  if (state==XmlParsed && !(current&ArrayInitialized)) return false;
  if (state==Ready && (current&(NameRegistered|ArrayInitialized|XmlParsed))!=
      (NameRegistered|ArrayInitialized|XmlParsed)) return false;
  record->state.fetch_or(state,std::memory_order_acq_rel);
  return true;
}

bool DefinitionRegistry::ReadyFor(std::uint32_t id) const {
  const auto* record=Find(id);
  return record && (record->state.load(std::memory_order_acquire)&Ready)!=0;
}

std::uint32_t DefinitionRegistry::CompatibilityType(std::uint32_t id) const {
  const auto* record=Find(id);
  return record && ReadyFor(id) ? record->descriptor.nativeBaseType : id;
}

DefinitionRegistry& Registry() {static DefinitionRegistry registry;return registry;}

}  // namespace cc2::api::vehicle
