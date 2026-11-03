# CC2X source notices

CC2X is licensed under GPL-3.0-or-later; see LICENSE. Native Bridge is code from
the same development team. In this integration session the project owner
explicitly confirmed that all permissions exist for incorporating and publishing
the team contributions in the existing GPL-3.0-or-later SDK. No additional
permission from the owner is pending for this integration.

The original team snapshot notice is preserved without editing at
`integrations/NativeBridge-NOTICE.original.md`, together with its source snapshot.
Its historical statement that the export did not assign a new license describes
that source export, before the owner's confirmation. Original source paths,
SHA-256 hashes and changes are recorded in the integration manifest. Upstream
CC2X attribution and the existing `licenses/` directory remain intact.

Lua 5.3.6, lua/lua commit `75ea9ccbea7c4886f30da147fb67b693b2624c26`,
is distributed under the MIT license in `third_party/Lua-LICENSE.txt`.
The linked runtime incorporates Lua; keep that notice with binary distributions.
Zig 0.15.2 is an external build tool; its downloaded distribution contains its
own notices and is not vendored in this source tree.
Linked compiler runtimes and standard libraries retain their original licenses:
MinGW and Zig notices are in licenses/mingw-COPYING.txt and Zig-LICENSE.txt;
libc++, libc++abi and libunwind notices are in the corresponding licenses/*-LICENSE.txt
files (Apache-2.0 WITH LLVM-exception). Binary redistributions retain these notices.
The ModLoader client-candidate asset set provides matching SDK source and Zig
provider library sources alongside the exact reviewed binary.

Aircraft models, registration scripts and gameplay mods are separate consumers;
they are not bundled into the SDK runtime. Research fixtures are not release
features. No vanilla game executable or stock game Lua scripts are included.

The framework's generic named-button/message-20 transport and guarded wing
throttle access use engine-layout research by Yolo / totolerigolo118 from
`carrier-command-2-mods` commit `a40d1572eabd6d9710e6d64aae431824ffc98790`,
under that project's GPL-3.0-or-later license. Its original attribution and
source hashes are retained in `integrations/cc2mods-source-lock.json`. Hold,
Modes and Brake gameplay policies are not linked into the SDK runtime.
