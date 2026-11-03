// SPDX-License-Identifier: GPL-3.0-or-later
#include "api/vehicle/buttons.h"
#include <cstring>
#include <iostream>
#include <vector>
namespace {template<class T>void Store(unsigned char* p,int offset,T value){std::memcpy(p+offset,&value,sizeof(value));}
int messages=0;void __attribute__((thiscall)) Enqueue(void*,std::uint32_t vehicle,std::uint32_t index,std::uint32_t value,std::uint32_t flag){if(vehicle==7&&index==1&&value==1&&flag==0)++messages;}}
#define CHECK(x) do{if(!(x)){std::cerr<<"line "<<__LINE__<<": " #x "\n";return 1;}}while(0)
int main(){
  unsigned char vehicle[0x600]{},definition[0x140]{},entries[2*0x58]{},states[16]{};
  const char first[]="example_button",second[]="example_copy";
  Store(vehicle,0x80,7u);Store(vehicle,0x88,definition);Store(vehicle,0x88+0x4c,states);Store(vehicle,0x88+0x50,2u);
  Store(definition,0x120,entries);Store(definition,0x124,2u);Store(definition,0x128,0u);Store(definition,0x12c,2u);
  Store(entries,0,first);Store(entries,4,static_cast<std::uint32_t>(sizeof(first)-1));
  Store(entries,0x58,second);Store(entries,0x58+4,static_cast<std::uint32_t>(sizeof(second)-1));states[2]=states[10]=1;states[0]=1;
  unsigned char peers[16]{},player[0x70]{},queue[0x18]{};void* players[1]{player};
  Store(peers,0,players);Store(peers,4,1u);Store(peers,8,0u);Store(peers,12,1u);Store(player,0x64,queue);
  using namespace cc2::api::vehicle;
  Store(vehicle,0x560,0);Store(vehicle,0x564,states);Store(vehicle,0x568,entries);
  ButtonContext query;ButtonReference alternate;
  CHECK(BeginButtonQuery(vehicle,&query));CHECK(query.sourceIndex==0);
  states[0]=0;states[8]=1;CHECK(BeginButtonQuery(vehicle,&query));
  CHECK(SelectButtonReference(query,1,&alternate));
  CHECK(alternate.index==1&&alternate.state==states+8&&alternate.definition==entries+0x58);
  CHECK(alternate.state[0]==1&&states[0]==0); // immediate read alias, no delayed state write
  CHECK(!StageButtons(&query,{{"example_copy",false}})); // read phase cannot mutate
  states[2]=0;CHECK(BeginButtonQuery(vehicle,&query));CHECK(!SelectButtonReference(query,1,&alternate));states[2]=1;
  Store(vehicle,0x564,states+8);CHECK(!BeginButtonQuery(vehicle,&query));Store(vehicle,0x564,states);
  states[0]=1;states[8]=0;
  ButtonContext context;
  CHECK(BeginButtons(vehicle,peers,Enqueue,&context));CHECK(context.vehicleId==7&&context.buttons.size()==2);
  CHECK(context.buttons[0].on&&context.buttons[1].enabled&&!context.buttons[1].on);
  CHECK(StageButtons(&context,{{"example_copy",true}}));CHECK(states[8]==0&&messages==0); // request is staged
  CHECK(CommitButtons(&context));CHECK(states[8]==1&&messages==1);CHECK(states[10]==1);
  CHECK(BeginButtons(vehicle,peers,Enqueue,&context));
  CHECK(!StageButtons(&context,{{"unknown",true}}));CHECK(states[0]==1);
  states[10]=0;CHECK(BeginButtons(vehicle,peers,Enqueue,&context));CHECK(!StageButtons(&context,{{"example_copy",false}}));
  states[10]=1;CHECK(BeginButtons(vehicle,peers,Enqueue,&context));CHECK(StageButtons(&context,{{"example_copy",false}}));
  CHECK(!StageButtons(&context,{{"example_copy",true}}));CHECK(!CommitButtons(&context));CHECK(states[8]==1); // conflicting requests are neutral
  CHECK(BeginButtons(vehicle,peers,Enqueue,&context));CHECK(StageButtons(&context,{{"example_copy",false}}));
  states[10]=0;CHECK(!CommitButtons(&context));CHECK(states[8]==1); // game changed usability
  Store(definition,0x12c,3u);CHECK(!BeginButtons(vehicle,peers,Enqueue,&context));
  std::cout<<"PASS generic named buttons: snapshots, staged network sync, usability, collisions, malformed layouts\n";
}
