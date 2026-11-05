// SPDX-License-Identifier: GPL-3.0-or-later
#include "api/vehicle/attachment_rules.h"
#include <cassert>
#include <iostream>
int main(){using cc2::api::vehicle::AttachmentRequestAllowed;
 for(unsigned slot=0;slot<8;++slot)assert(AttachmentRequestAllowed(slot,30,8,4,0,0));
 assert(AttachmentRequestAllowed(7,-1,8,4,0,0));
 assert(!AttachmentRequestAllowed(8,30,8,4,0,0));
 assert(!AttachmentRequestAllowed(0,30,65,4,0,0));
 assert(!AttachmentRequestAllowed(0,-2,8,4,0,0));
 assert(!AttachmentRequestAllowed(0,102,8,4,0,0));
 assert(!AttachmentRequestAllowed(0,30,8,0,0,0));
 assert(!AttachmentRequestAllowed(0,30,8,4,100,0));
 assert(!AttachmentRequestAllowed(0,30,8,4,0,1));
 std::cout<<"PASS all hardpoints, clear sentinel, bounds, parked/parent/order admission\n";
}
