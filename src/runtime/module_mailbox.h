// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
namespace cc2::runtime {
// Game-screen Lua can request work, but never obtains an engine pointer or
// executes another VM. A selected module validates requests on its server tick.
// No topic, vehicle kind, gameplay setting or default action is built in here.
class ModuleMailbox {
 public:
  struct Message {std::string topic;std::uint32_t subject;double value;};
  bool Enable(const std::string& owner) {
    std::lock_guard<std::mutex> lock(mutex_);
    return !owner.empty()&&owner.size()<=128&&owners_.size()<256&&owners_.emplace(owner,Endpoint{}).second;
  }
  void Remove(const std::string& owner){std::lock_guard<std::mutex> lock(mutex_);owners_.erase(owner);}
  void Begin(std::uint64_t generation){std::lock_guard<std::mutex> lock(mutex_);Clear();generation_=generation;}
  void End(){Begin(0);}
  bool Send(const std::string& owner,const std::string& topic,std::uint32_t subject,double value) {
    std::lock_guard<std::mutex> lock(mutex_);auto it=owners_.find(owner);
    if(!generation_||it==owners_.end()||!Topic(topic)||!std::isfinite(value)||it->second.queue.size()>=64)return false;
    it->second.queue.push_back({topic,subject,value});return true;
  }
  std::vector<Message> Drain(const std::string& owner) {
    std::lock_guard<std::mutex> lock(mutex_);auto it=owners_.find(owner);
    if(!generation_||it==owners_.end())return {};
    std::vector<Message> result(it->second.queue.begin(),it->second.queue.end());it->second.queue.clear();return result;
  }
  bool Publish(const std::string& owner,const std::string& topic,std::uint32_t subject,double value) {
    std::lock_guard<std::mutex> lock(mutex_);auto it=owners_.find(owner);
    if(!generation_||it==owners_.end()||!Topic(topic)||!std::isfinite(value))return false;
    auto key=std::make_pair(topic,subject);auto& states=it->second.states;
    if(states.size()>=256&&!states.count(key))return false;
    states[key]=value;return true;
  }
  bool Read(const std::string& owner,const std::string& topic,std::uint32_t subject,double* value) {
    std::lock_guard<std::mutex> lock(mutex_);auto it=owners_.find(owner);
    if(!generation_||it==owners_.end()||!value||!Topic(topic))return false;
    auto state=it->second.states.find({topic,subject});if(state==it->second.states.end())return false;
    *value=state->second;return true;
  }
  void Forget(const std::string& owner,std::uint32_t subject) {
    std::lock_guard<std::mutex> lock(mutex_);auto it=owners_.find(owner);if(it==owners_.end())return;
    auto& states=it->second.states;
    for(auto state=states.begin();state!=states.end();)if(state->first.second==subject)state=states.erase(state);else ++state;
  }
 private:
  struct Endpoint {std::deque<Message> queue;std::map<std::pair<std::string,std::uint32_t>,double> states;};
  static bool Topic(const std::string& topic) {
    if(topic.empty()||topic.size()>64)return false;
    for(unsigned char c:topic)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-'))return false;
    return true;
  }
  void Clear(){for(auto& owner:owners_){owner.second.queue.clear();owner.second.states.clear();}}
  std::mutex mutex_;std::map<std::string,Endpoint> owners_;std::uint64_t generation_=0;
};
}
