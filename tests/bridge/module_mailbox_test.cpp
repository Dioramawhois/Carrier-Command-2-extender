// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime/module_mailbox.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>
using cc2::runtime::ModuleMailbox;
#define CHECK(x) do {if(!(x)){std::cerr<<"line "<<__LINE__<<": " #x "\n";return 1;}} while(0)
int main() {
  ModuleMailbox bus;
  CHECK(!bus.Send("disabled.mod","mode",7,1));
  CHECK(bus.Enable("example.mod"));CHECK(!bus.Enable("example.mod"));
  CHECK(!bus.Send("example.mod","mode",7,1)); // no live session
  bus.Begin(1);
  CHECK(bus.Send("example.mod","mode",0xffffffffu,2));
  CHECK(!bus.Send("example.mod","",7,1));
  CHECK(!bus.Send("example.mod",std::string(65,'a'),7,1));
  CHECK(!bus.Send("example.mod","mode",7,std::numeric_limits<double>::infinity()));
  CHECK(!bus.Send("example.mod","mode",7,std::nan("")));
  auto messages=bus.Drain("example.mod");
  CHECK(messages.size()==1&&messages[0].subject==0xffffffffu&&messages[0].value==2);
  CHECK(bus.Drain("example.mod").empty());
  for(int i=0;i<64;++i)CHECK(bus.Send("example.mod","mode",7,i));
  CHECK(!bus.Send("example.mod","mode",7,65)); // bounded, never drops silently
  CHECK(bus.Drain("example.mod").size()==64);
  CHECK(bus.Publish("example.mod","status",7,3));double value=0;
  CHECK(bus.Read("example.mod","status",7,&value)&&value==3);
  CHECK(!bus.Read("other.mod","status",7,&value));
  for(unsigned i=0;i<255;++i)CHECK(bus.Publish("example.mod","state",i,i));
  CHECK(!bus.Publish("example.mod","state",256,0));
  CHECK(bus.Publish("example.mod","status",7,4)); // replace at capacity
  bus.Forget("example.mod",7);CHECK(!bus.Read("example.mod","status",7,&value));
  CHECK(bus.Publish("example.mod","state",256,0)); // reclaimed capacity
  bus.Begin(2);CHECK(!bus.Read("example.mod","status",7,&value));
  CHECK(bus.Send("example.mod","mode",7,1));bus.End();
  CHECK(bus.Drain("example.mod").empty());CHECK(!bus.Send("example.mod","mode",7,1));
  bus.Begin(3);CHECK(bus.Send("example.mod","mode",7,1));
  bus.Remove("example.mod");CHECK(bus.Drain("example.mod").empty());
  CHECK(!bus.Send("example.mod","mode",7,1));
  CHECK(bus.Enable("example.mod"));CHECK(bus.Drain("example.mod").empty());
  std::thread producer([&]{for(int i=0;i<1000;++i)bus.Send("example.mod","mode",7,i);});
  std::thread consumer([&]{for(int i=0;i<1000;++i)bus.Drain("example.mod");});
  producer.join();consumer.join();CHECK(bus.Drain("example.mod").size()<=64);
  std::cout<<"PASS bounded per-owner Lua mailbox: isolation, sessions, removal, overflow, threads\n";
}
