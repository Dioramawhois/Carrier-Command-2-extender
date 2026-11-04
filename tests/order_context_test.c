/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifdef NDEBUG
#error Order origin regression requires assertions
#endif
#include "order_context.h"
#include "waypoint_origin.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
static uint64_t epoch=9;
static uint64_t generation(void){return epoch;}
static unsigned char memory[0x6000];
static int read_memory(uint32_t address,void *out,uint32_t bytes,void *user) {
    (void)user;if(address>sizeof(memory)||bytes>sizeof(memory)-address)return 0;
    memcpy(out,memory+address,bytes);return 1;
}
static void put(uint32_t address,uint32_t value){memcpy(memory+address,&value,4);}
static DWORD WINAPI other_thread(void* unused){(void)unused;cc2x_waypoint_order out={.size=sizeof(out)};assert(!cc2x_orders_current(&out));return 0;}
int main(void) {
    const cc2x_orders_api *api=cc2x_orders_resolve(cc2x_query_orders_v1);assert(api);
    assert(!cc2x_orders_resolve(NULL));assert(!cc2x_query_orders_v1(2,sizeof(*api)));
    assert(!cc2x_query_orders_v1(1,sizeof(*api)-1));
    cc2x_waypoint_order out={.size=sizeof(out)};
    cc2x_orders_profile(1);assert(api->available()&&!api->current(&out));
    /* The manager's initial deque is empty in a real local session. The
     * engine sender lookup uses the deque at manager+0x20, not manager+0. */
    uint32_t q[4]={0x3000,4,3,2};memcpy(memory+0x1c68,q,sizeof(q));
    put(0x2000-0x36c,23);put(0x4000+0x80,17);put(0x3000+12,0x5000);put(0x3000,0x5100);put(0x5000,23);put(0x5100,42);
    cc2x_waypoint_order record={0};
    assert(cc2x_decode_waypoint_origin(0x1000,0x1000,0x2000,0x4000,epoch,read_memory,NULL,&record));
    assert(record.peer_id==23&&record.vehicle_id==17&&record.flags==CC2X_ORDER_BROADCAST_PENDING);
    assert(!cc2x_decode_waypoint_origin(0x1000,0x1001,0x2000,0x4000,epoch,read_memory,NULL,&record));
    assert(!cc2x_decode_waypoint_origin(0xfffffff0,0xfffffff0,0x2000,0x4000,epoch,read_memory,NULL,&record));
    put(0x5100,23);assert(!cc2x_decode_waypoint_origin(0x1000,0x1000,0x2000,0x4000,epoch,read_memory,NULL,&out));put(0x5100,42);
    put(0x2000-0x36c,99);assert(!cc2x_decode_waypoint_origin(0x1000,0x1000,0x2000,0x4000,epoch,read_memory,NULL,&out));
    put(0x2000-0x36c,0);put(0x5000,0);assert(cc2x_decode_waypoint_origin(0x1000,0x1000,0x2000,0x4000,epoch,read_memory,NULL,&record));
    cc2x_order_scope previous,nested;cc2x_orders_enter(&record,generation,&previous);
    assert(api->current(&out)&&out.peer_id==0&&out.generation==epoch);
    HANDLE thread=CreateThread(NULL,0,other_thread,NULL,0,NULL);assert(thread);
    assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
    cc2x_orders_enter(NULL,generation,&nested);assert(!api->current(&out));cc2x_orders_leave(&nested);assert(api->current(&out));
    out.size=0;assert(!api->current(&out));out.size=sizeof(out);
    ++epoch;assert(!api->current(&out));--epoch;
    cc2x_orders_profile(0);assert(!api->available()&&!api->current(&out));cc2x_orders_profile(1);
    cc2x_orders_leave(&previous);assert(!api->current(&out));
    puts("PASS public order origin: guarded decoder, FIFO roster wrap, absent/duplicate/zero sender, TLS, nested scope, epoch and ABI refusal");
    return 0;
}
