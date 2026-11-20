"""Static profile guard for Steam build 26732430; never executes the target."""
import hashlib
import pathlib
import struct
import sys

path = pathlib.Path(sys.argv[1])
cc2x_source = pathlib.Path(sys.argv[2]).read_text(encoding="utf-8") if len(sys.argv) > 2 else ""
world_source = pathlib.Path(sys.argv[3]).read_text(encoding="utf-8") if len(sys.argv) > 3 else ""
hangar_source = pathlib.Path(sys.argv[4]).read_text(encoding="utf-8") if len(sys.argv) > 4 else ""
ui_hook_source = pathlib.Path(sys.argv[5]).read_text(encoding="utf-8") if len(sys.argv) > 5 else ""
data = path.read_bytes()
assert hashlib.sha256(data).hexdigest() == "1df609c8252fc0245decfab793e3b8e1884e553539a8831cbee67663b6725920"
pe = struct.unpack_from("<I", data, 0x3C)[0]
assert data[pe:pe + 4] == b"PE\0\0"
machine, sections_n, timestamp, _, _, opt_size, _ = struct.unpack_from("<HHIIIHH", data, pe + 4)
assert machine == 0x14C and timestamp == 0x6AFC5C17
opt = pe + 24
assert struct.unpack_from("<H", data, opt)[0] == 0x10B
image_size = struct.unpack_from("<I", data, opt + 56)[0]
assert image_size == 0x00af6000
image_base = struct.unpack_from("<I", data, opt + 28)[0]
reloc_rva, reloc_size = struct.unpack_from("<II", data, opt + 96 + 5 * 8)
assert image_base == 0x400000 and reloc_size > 0
section_table = opt + opt_size
sections = []
for i in range(sections_n):
    off = section_table + 40 * i
    virtual_size, rva, raw_size, raw_off = struct.unpack_from("<IIII", data, off + 8)
    sections.append((rva, max(virtual_size, raw_size), raw_off, raw_size))

def read_va(va, size):
    rva = va - 0x400000
    for start, span, raw, raw_size in sections:
        if start <= rva and rva + size <= start + span and rva - start + size <= raw_size:
            return data[raw + rva - start:raw + rva - start + size]
    raise AssertionError(f"VA not file-backed: {va:#x}")

def read_rva(rva, size):
    return read_va(rva + image_base, size)

relocations = set()
cursor = 0
while cursor < reloc_size:
    page_rva, block_size = struct.unpack_from("<II", read_rva(reloc_rva + cursor, 8))
    assert block_size >= 8 and block_size <= reloc_size - cursor and (block_size - 8) % 2 == 0
    block = read_rva(reloc_rva + cursor + 8, block_size - 8)
    for i in range(0, len(block), 2):
        item = struct.unpack_from("<H", block, i)[0]
        reloc_type, offset = item >> 12, item & 0x0FFF
        if reloc_type:
            relocations.add((page_rva + offset, reloc_type))
    cursor += block_size

assert read_va(0x836422, 6) == bytes.fromhex("c7071441d300")
# Core-owned stock UI script injection point and the exact game helpers used to
# append one 24-byte script record and allocate its two game strings.
assert read_va(0x5b56bb, 6) == bytes.fromhex("8d5f748d45c4")
assert read_va(0x858f00, 5) == bytes.fromhex("558bec6aff")
assert read_va(0x412b50, 10) == bytes.fromhex("558bec8b4508538bd957")
assert read_va(0xc4b112, 10) == bytes.fromhex("8bff558bec8b450885c0")
entries = struct.unpack("<III", read_va(0xd34114, 12))
assert entries == (0x7733a0, 0x77f330, 0x77f3c0), tuple(hex(x) for x in entries)
# Carrier RTTI Complete Object Locator and address point. This guards the
# narrowly ported throttle hook independently of the server lifecycle vtable.
assert struct.unpack("<I", read_va(0xd589cc, 4))[0] == 0xdc7dcc
assert struct.unpack("<I", read_va(0xdc7dcc + 12, 4))[0] == 0xe36e48
assert read_va(0xe36e50, len(b".?AUc_vehicle_server_carrier@@\0")) == b".?AUc_vehicle_server_carrier@@\0"
carrier_entries = struct.unpack("<III", read_va(0xd589d0 + 55 * 4, 12))
assert carrier_entries == (0x5ecd90, 0x705e20, 0x706ee0), tuple(hex(x) for x in carrier_entries)
assert read_va(0x705e20, 12) == bytes.fromhex("558bec83e4f081ecb8010000")
# Upstream 0.5.1 game-Lua ABI and World API sites for the 2026-10-28 rebuild.
# These are guarded in the runtime profile as well; this offline fixture makes
# sure addresses were not copied from the previous executable by accident.
assert read_va(0x446633, 12) == bytes.fromhex("6a20c78564ffffff00000000")
lua_helpers = {
    0x8ce390: "8b411485d27e0dc1",
    0x8ceb20: "558bec83ec085356",
    0x8fe360: "558bec83e4f883ec",
    0x906e10: "558bec83ec08538b",
    0x903700: "568b711033d26a20",
    0x902d00: "538bd956578b7b10",
}
for va, signature in lua_helpers.items():
    assert read_va(va, len(bytes.fromhex(signature))) == bytes.fromhex(signature), f"Lua helper mismatch at {va:#x}"
world_sites = {
    0x7785b7: "ffb050010000",
    0x62b10d: "8b576c33c0",
    0x6303cf: "8b018b4010ffd0",
    0x634c01: "8b078b4010ffd0",
}
for va, signature in world_sites.items():
    assert read_va(va, len(bytes.fromhex(signature))) == bytes.fromhex(signature), f"World hook mismatch at {va:#x}"
# Entrypoints used by the new World profile resolve to executable code and
# retain their expected x86 function prologues in this exact image.
for va, expected in ((0x5ee950, "55"), (0x6314c0, "55"), (0x6312c0, "55"),
                     (0x62b720, "55"), (0x855170, "55"), (0x8615b0, "55"),
                     (0x62ccb0, "53"), (0x628880, "53")):
    assert read_va(va, len(bytes.fromhex(expected))) == bytes.fromhex(expected), f"World function mismatch at {va:#x}"
if cc2x_source:
    assert '"1.5.20-win32", 0x6afc5c17, 0x00af6000' in cc2x_source
    assert '"1df609c8252fc0245decfab793e3b8e1884e553539a8831cbee67663b6725920", 1' in cc2x_source
    for value in ("0x008ce390", "0x008ceb20", "0x008fe360", "0x00906e10",
                  "0x00903700", "0x00902d00", "0x00d01800"):
        assert value in cc2x_source, f"Lua ABI profile value missing: {value}"
if world_source:
    assert '.build = "1.5.20-win32"' in world_source
    for value in ("0x005ee950", "0x006314c0", "0x006312c0", "0x0062b720",
                  "0x00855170", "0x008615b0", "0x007785b7", "0x0062b10d",
                  "0x006303cf", "0x00634c01", "0x006351bd", "0x0062ccb0",
                  "0x0062bb73", "0x00628880"):
        assert value in world_source, f"World profile value missing: {value}"
# Current animation profile: exact instruction windows, changed global operands,
# and relative destinations are checked against this authenticated EXE.
def call_target(va):
    assert read_va(va, 1) == b"\xe8", f"expected CALL at {va:#x}"
    rel = struct.unpack("<i", read_va(va + 1, 4))[0]
    return va + 5 + rel

for va in (0x700b43, 0x700f70):
    assert read_va(va, 10) == bytes.fromhex("f30f59c78b8d00feffff"), f"route-window mismatch at {va:#x}"
route_sites = (0x6fcff7, 0x6fd2ed, 0x6fd9c4, 0x6fdeab)
for va in route_sites:
    assert read_va(va, 8) == bytes.fromhex("f30f590d6467db00"), f"route multiplier mismatch at {va:#x}"
assert read_va(0x6fdc02, 5) == bytes.fromhex("f30f5e4078")
crane_sources = (0xdb5ffc, 0xdb69ec, 0xdb5ffc, 0xdb69ec, 0xdb5f94,
                 0xdb69bc, 0xdb5f94, 0xdb69bc, 0xdb5f9c, 0xdb69c4)
crane_operands = (0x701286, 0x701291, 0x701395, 0x7013a0, 0x701484,
                  0x70148c, 0x701516, 0x70151e, 0x701579, 0x701585)
for i, (operand, source) in enumerate(zip(crane_operands, crane_sources)):
    ins = operand - 4
    opcode = "f30f1015" if i % 2 == 0 else "f30f100d"
    assert read_va(ins, 8) == bytes.fromhex(opcode) + struct.pack("<I", source), f"crane source mismatch at {ins:#x}"
lift_sites = ((0x705f70, "f30f101d885fdb00"), (0x705f9a, "f30f5f0db869db00"))
for va, signature in lift_sites:
    assert read_va(va, 8) == bytes.fromhex(signature), f"lift source mismatch at {va:#x}"
for va in (0x7012b5, 0x7013bc, 0x70149e, 0x7014e4, 0x701534, 0x701589):
    assert call_target(va) == 0x871020, f"crane clamp helper mismatch at {va:#x}"
assert call_target(0x701241) == 0x844cf0, "crane factor's stock helper target mismatch"
assert read_va(0x871020, 12) == bytes.fromhex("f30f5fc1f30f5dc2c3cccccc")
hangar_entries = {
    0x5ec3e0: "558bec8b450883c0fe83f85677280fb6",
    0x5ee9b0: "558bec6aff689dbbc70064a100000000",
    0x5ee950: "558bec51538bd933c05657394344742d",
    0x850f70: "558bec568b71080375088bc62b41043b",
    0x40fb60: "558bec83e4f883ec108b4508f30f1040",
    0x5e9eb0: "8b91900000008b818c000000568bb194",
    0x40ffa0: "558bec83e4f883ec50a144cbe10033c4",
    0x5ec570: "558bec83e4f881ec24010000a144cbe1",
    0x5e9fb0: "558bec83e4f881ec3c010000a144cbe1",
    0x5eeb70: "558bec6aff68dd04ca0064a100000000",
    0x5e4790: "c701000000008bc1c74104ffffffffc7",
    0x41a750: "c701000000008bc1c7410400000000c3",
    0x5eab10: "558bec8b895c0100008b45088b0c8185",
    0x88e9a0: "558bec6aff680ee3cc0064a100000000",
    0x411570: "558bec6aff68f0adc70064a100000000",
    0x572310: "8b412cc3cccccccccccccccccccccccc",
    0x572330: "558bec578bf98b4d083b4f2c731d8b57",
    0x411620: "8b01c3cccccccccccccccccccccccccc",
    0x565a10: "558bec6aff685d68c90064a100000000",
}
for va, signature in hangar_entries.items():
    assert read_va(va, 16) == bytes.fromhex(signature), f"hangar native entry mismatch at {va:#x}"
assert (0x5ee9b6 - image_base, 3) in relocations, "hangar exception-handler absolute operand must be ASLR-relocated"
expected_hangar_relocations = {
    0x5ee9b0: ((6, 0x00c7bb9d),), 0x40ffa0: ((10, 0x00e1cb44),),
    0x5ec570: ((13, 0x00e1cb44),), 0x5e9fb0: ((13, 0x00e1cb44),),
    0x5eeb70: ((6, 0x00ca04dd),), 0x88e9a0: ((6, 0x00cce30e),),
    0x411570: ((6, 0x00c7adf0),), 0x565a10: ((6, 0x00c9685d),),
}
for va, signature in hangar_entries.items():
    start = va - image_base
    end = start + len(bytes.fromhex(signature))
    overlapping = [(field, kind) for field, kind in relocations
                   if field < end and field + 4 > start]
    assert all(kind == 3 and start <= field < end
               for field, kind in overlapping), f"unsupported/partial hangar relocation at {va:#x}"
    actual = tuple(sorted((field - start, struct.unpack("<I", read_rva(field, 4))[0])
                          for field, _ in overlapping))
    assert actual == expected_hangar_relocations.get(va, ()), (
        f"hangar relocation-offset profile mismatch at {va:#x}: {actual}")
if hangar_source:
    assert "pe_guard::MatchLoadedBytes" in hangar_source, "hangar runtime guards must normalize PE relocations"
if ui_hook_source:
    for value in ("0x005b56bb", "0x00858f00", "0x00412b50", "0x00c4b112",
                  "kScriptVectorHookBytes", "kVectorEmplaceBackPrefix",
                  "kStringReservePrefix", "kStringFreePrefix"):
        assert value in ui_hook_source, f"core UI script hook profile value missing: {value}"
assert read_va(0x005a2fc0, 5) == bytes.fromhex("558bec6aff"), "definition loader entry mismatch"
assert read_va(0x008545e0, 6) == bytes.fromhex("558bec515356"), "definition name-map resize mismatch"
print("PASS SHA/PE identity, lifecycle/throttle vtables, Lua ABI, World API, all 26 animation sites/targets, hangar entries, UI helpers and definition loader/name-map entry guards")

# Follow actual enqueue operands rather than nearby deduplicated data tables.
assert call_target(0x706410) == 0x565e80
for offset, target in ((6, 0xc9685d), (0x15, 0xe1cb44), (0x38, 0xd1e118), (0x45, 0xd24600)):
    assert struct.unpack("<I", read_va(0x565e80 + offset, 4))[0] == target
type_function = struct.unpack("<I", read_va(0xd24600 + 4, 4))[0]
assert read_va(type_function, 6) == bytes.fromhex("b814000000c3")
assert call_target(0x6fd0d4) == 0x63f7e0
assert call_target(0x6fdca2) == 0x63f7e0
print("PASS button enqueue targets, absolute operands, type vtable and readers")
