// SPDX-License-Identifier: GPL-3.0-or-later
#include "carrier_fixture.h"
int main(){
 auto a=Make();Recovery r(Dock);r.Configure(&a);r.Session(server,1);r.Tick(server,1,1);
 assert(r.Available(CC2X_CARRIER_DOCK)&&r.Available(CC2X_CARRIER_RECOVERY)&&commits==0);
 assert(!r.Request("native.a",2,1,7));
 auto id=r.Request("native.a",1,1,7);assert(id&&calls==0);
 assert(!r.Request("native.b",1,1,7)); // no duplicate command from another owner
 cc2x_dock_result result{sizeof(result)};
 assert(!r.Result("native.b",id,&result));
 assert(r.Result("native.a",id,&result)&&result.code==CC2X_DOCK_PENDING);
 r.Tick(server,1,2);assert(calls==1&&r.Result("native.a",id,&result)&&result.code==CC2X_DOCK_ACCEPTED);
 r.Tick(server,1,3);assert(calls==1);
 cc2x_recovery_options options{sizeof(options),600,3,30};
 assert(r.Arm("native.a",1,1,7,options));assert(commits==1&&patches==4);
 assert(!r.Arm("native.a",1,1,7,options)); // repeating never renews budgets
 assert(!r.Arm("native.b",1,1,7,options));
 cc2x_recovery_status state{sizeof(state)};assert(!r.Status("native.b",7,&state));
 assert(r.Status("native.a",7,&state)&&state.armed);
 uint32_t element[3]{7,0,9};cc2x_cpu cpu{};cpu.eax=reinterpret_cast<uint32_t>(element);cpu.edi=reinterpret_cast<uint32_t>(manager);
 Put(manager,0,1);Put(manager,0x5c,-1);
 hooks[0](&cpu,users[0]);assert(cpu.eax!=reinterpret_cast<uint32_t>(element)&&element[1]==0);
 hooks[1](&cpu,users[1]);assert(cpu.jump_to==Va(0x701b9a));
 assert(r.Status("native.a",7,&state)&&state.grants==1);
 element[1]=3;cpu.eax=reinterpret_cast<uint32_t>(element);cpu.jump_to=0;
 hooks[0](&cpu,users[0]);hooks[1](&cpu,users[1]);assert(!cpu.jump_to); // aborted queue entry is never forced again
 assert(r.Disarm("native.a",7));assert(!r.Disarm("native.b",7));
 assert(!r.Arm("native.a",1,1,7,options)); // disarm does not renew a continuous recall
 dock=0;r.Tick(server,1,4);dock=5;
 // Every tick and each hook independently enforce expiry and thread identity.
 assert(r.Arm("native.a",1,1,7,options));bool wrong=true;
 std::thread t([&]{wrong=r.Disarm("native.a",7);});t.join();assert(!wrong);
 r.Tick(server,1,604);assert(r.Status("native.a",7,&state)&&!state.armed);
 dock=0;r.Tick(server,1,605);dock=5;
 mode=1;assert(!r.Arm("native.a",1,1,7,options));mode=0;
 team=2;assert(!r.Arm("native.a",1,1,7,options));team=1;
 Put(bays,0x64,99u);assert(!r.Arm("native.a",1,1,7,options));Put(bays,0x64,0u);
 options.grants=4;assert(!r.Arm("native.a",1,1,7,options));options.grants=3;
 assert(r.Arm("native.a",1,1,7,options));r.DropOwner("native.a");assert(!r.Status("native.a",7,&state));
 assert(!r.Arm("native.b",1,1,7,options)); // module errors/reloads cannot reset the native safety cap
 dock=0;r.Tick(server,1,606);dock=5;assert(r.Arm("native.a",1,1,7,options));
 Put(manager,0x5c,-1);element[0]=8;element[1]=0;cpu={};cpu.eax=reinterpret_cast<uint32_t>(element);cpu.edi=reinterpret_cast<uint32_t>(manager);
 hooks[0](&cpu,users[0]);hooks[1](&cpu,users[1]);assert(!cpu.jump_to&&cpu.eax==reinterpret_cast<uint32_t>(element));element[0]=7;
 uint32_t xmmBefore[32],xmmAfter[32]{};for(unsigned i=0;i<32;++i)xmmBefore[i]=0x12345600+i;
 XmmCall(hooks[0],&cpu,users[0],xmmBefore,xmmAfter);assert(!std::memcmp(xmmBefore,xmmAfter,sizeof(xmmBefore)));
 hooks[1](&cpu,users[1]);assert(cpu.jump_to);
 for(int i=0;i<2;++i){cpu.eax=reinterpret_cast<uint32_t>(element);cpu.jump_to=0;hooks[0](&cpu,users[0]);hooks[1](&cpu,users[1]);assert(cpu.jump_to);}
 cpu.eax=reinterpret_cast<uint32_t>(element);cpu.jump_to=0;hooks[0](&cpu,users[0]);hooks[1](&cpu,users[1]);assert(!cpu.jump_to);
 assert(r.Status("native.a",7,&state)&&state.grants==3);
 // H5 changes only the copied register frame, with a separate 30-jump budget.
 unsigned char frame[0x310]{};auto* anchor=frame+0x300;
 Put(anchor-0x1fc,0,plane);Put(anchor-0x200,0,ship);Put(anchor-0x204,0,manager);
 Put(manager,0x1c,3);Put(manager,0x5c,7u);
 unsigned char originalFrame[sizeof(frame)];std::memcpy(originalFrame,frame,sizeof(frame));
 for(int i=0;i<30;++i){cpu={};cpu.ebp=reinterpret_cast<uint32_t>(anchor);cpu.edi=reinterpret_cast<uint32_t>(plane);
 hooks[2](&cpu,users[2]);assert(cpu.jump_to==Va(0x6ff31a)&&cpu.edi==reinterpret_cast<uint32_t>(manager)&&cpu.esi==reinterpret_cast<uint32_t>(ship));}
 cpu.jump_to=0;cpu.edi=reinterpret_cast<uint32_t>(plane);hooks[2](&cpu,users[2]);assert(!cpu.jump_to&&!std::memcmp(frame,originalFrame,sizeof(frame)));
 assert(r.Status("native.a",7,&state)&&state.touch_jumps==30);assert(r.Disarm("native.a",7));
 dock=0;r.Tick(server,1,607);dock=5;wing=0;options.window_ticks=300;assert(r.Arm("native.a",1,1,7,options));
 Put(anchor-0x270,0,plane);Put(manager,0x5c,-1);cpu={};cpu.ebp=reinterpret_cast<uint32_t>(anchor);cpu.edi=reinterpret_cast<uint32_t>(manager);
 hooks[3](&cpu,users[3]);assert(cpu.jump_to==Va(0x6fde02));cpu.jump_to=0;hooks[3](&cpu,users[3]);assert(!cpu.jump_to);
 assert(r.Status("native.a",7,&state)&&state.grants==1);r.Disarm("native.a",7);wing=1;
 dock=0;id=r.Request("native.a",1,1,7);assert(id);r.Session(nullptr,2);r.Session(server,3);r.Tick(server,3,1);
 assert(calls==1&&!r.Result("native.a",id,&result));
 // The exact carrier entry is checked again at execution, after acceptance.
 id=r.Request("native.a",3,1,7);assert(id);Put(reinterpret_cast<void*>(Va(0xd596ec)),0x7c,0u);
 r.Tick(server,3,2);assert(calls==1&&r.Result("native.a",id,&result)&&result.code==CC2X_DOCK_PROFILE_REFUSED);
 // Foreign ownership/byte changes must refuse discovery without patching.
 a=Make();Recovery occupied(Dock);occupied.Configure(&a);image[0x7019de-0x400000]=0xe9;
 assert(!occupied.Available(CC2X_CARRIER_RECOVERY)&&occupied.Available(CC2X_CARRIER_DOCK));
 a=Make();occupied.Configure(&a);occupied.Session(server,4);occupied.Tick(server,4,1);dock=5;commit=false;
 assert(!occupied.Arm("native.a",4,1,7,options)&&aborts>0);commit=true;
 // Engine events may remove the current owner and enqueue new work synchronously.
 Recovery reentrant(Dock);reentrant.Configure(&a);reentrant.Session(server,8);reentrant.Tick(server,8,1);dock=0;inCall=&reentrant;
 duringDock=[]{inCall->DropOwner("native.first");nestedRequest=inCall->Request("native.second",8,1,7);};
 id=reentrant.Request("native.first",8,1,7);assert(id);reentrant.Tick(server,8,2);duringDock=nullptr;
 assert(nestedRequest&&!reentrant.Result("native.first",id,&result));
 reentrant.Tick(server,8,3);assert(reentrant.Result("native.second",nestedRequest,&result)&&result.code==CC2X_DOCK_ALREADY_QUEUED);
 // Same-address session replacement from inside the native call drops the result.
 dock=0;duringDock=[]{inCall->Session(nullptr,9);inCall->Session(server,10);};
 id=reentrant.Request("native.first",8,1,7);assert(id);reentrant.Tick(server,8,4);duringDock=nullptr;
 reentrant.Tick(server,10,1);assert(!reentrant.Result("native.first",id,&result));
 // Native faults latch the capability off even through a later session.
 Recovery fault(Fault);fault.Configure(&a);fault.Session(server,11);fault.Tick(server,11,1);dock=0;
 id=fault.Request("native.a",11,1,7);assert(id);fault.Tick(server,11,2);
 assert(fault.Result("native.a",id,&result)&&result.code==CC2X_DOCK_ENGINE_FAULT&&!fault.Available(CC2X_CARRIER_DOCK));
 fault.Session(server,12);fault.Tick(server,12,1);assert(!fault.Available(CC2X_CARRIER_RECOVERY));
 // Completed results are bounded and evicted without token reuse.
 reentrant.Session(server,13);reentrant.Tick(server,13,1);dock=5;uint64_t first=0;
 for(unsigned i=0;i<65;++i){auto token=reentrant.Request("native.a",13,1,7);assert(token);if(!i)first=token;reentrant.Tick(server,13,i+2);}
 assert(!reentrant.Result("native.a",first,&result));
 std::cout<<"PASS carrier request lifetime, ownership, lazy transaction, loop breaker, expiry, guards\n";
}
