/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Address-free decoder for the separately guarded 1.5.18-r2 receive frame.
 * The reader refuses inaccessible/unmapped regions. No engine function runs. */
#ifndef CC2X_WAYPOINT_ORIGIN_H
#define CC2X_WAYPOINT_ORIGIN_H
#include "cc2x_orders.h"
typedef int (*cc2x_origin_read_fn)(uint32_t address,void *out,uint32_t bytes,void *user);
static inline int cc2x_decode_waypoint_origin(uint32_t server,uint32_t hook_server,uint32_t frame,
                                             uint32_t vehicle,uint64_t generation,
                                             cc2x_origin_read_fn read,void *user,cc2x_waypoint_order *out) {
    uint32_t peer,vid,q[4],found=0;
    if(!server||server!=hook_server||frame<0x36c||!vehicle||!generation||!read||!out||
       server>UINT32_MAX-0xc78||vehicle>UINT32_MAX-0x84||
       !read(frame-0x36c,&peer,4,user)||!read(vehicle+0x80,&vid,4,user)||
       !read(server+0xc48+0x20,q,sizeof(q),user))return 0;
    if(!vid||!q[0]||!q[1]||q[1]>4096||q[2]>=q[1]||!q[3]||q[3]>256||q[3]>q[1])return 0;
    for(uint32_t i=0;i<q[3];++i) {
        uint32_t index=(q[2]+i)%q[1],pointer=0,id=0;
        if(q[0]>UINT32_MAX-index*4||!read(q[0]+index*4,&pointer,4,user)||
           !pointer||!read(pointer,&id,4,user))return 0;
        if(id==peer)++found;
    }
    if(found!=1)return 0; /* missing/ambiguous sender is never attributed */
    cc2x_waypoint_order record={sizeof(*out),vid,peer,CC2X_ORDER_BROADCAST_PENDING,generation};
    *out=record;
    return 1;
}
#endif
