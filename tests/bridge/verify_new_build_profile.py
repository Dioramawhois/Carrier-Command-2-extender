"""Static profile guard for Steam build 26277078; never executes the target."""
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
assert hashlib.sha256(data).hexdigest() == "461e12623bacb7de29ca27b4614425c87e8df2c8327a5e958a957c850effe42a"
pe = struct.unpack_from("<I", data, 0x3C)[0]
assert data[pe:pe + 4] == b"PE\0\0"
machine, sections_n, timestamp, _, _, opt_size, _ = struct.unpack_from("<HHIIIHH", data, pe + 4)
assert machine == 0x14C and timestamp == 0x6AE228F3
opt = pe + 24
assert struct.unpack_from("<H", data, opt)[0] == 0x10B
image_size = struct.unpack_from("<I", data, opt + 56)[0]
assert image_size == 0x00AF7000
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

assert read_va(0x835CF2, 6) == bytes.fromhex("c707fc4bd300")
# Core-owned stock UI script injection point and the exact game helpers used to
# append one 24-byte script record and allocate its two game strings.
assert read_va(0x5B555B, 6) == bytes.fromhex("8d5f748d45c4")
assert read_va(0x858740, 5) == bytes.fromhex("558bec6aff")
assert read_va(0x412B30, 10) == bytes.fromhex("558bec8b4508538bd957")
assert read_va(0xC4C722, 10) == bytes.fromhex("8bff558bec8b450885c0")
entries = struct.unpack("<III", read_va(0xD34BFC, 12))
assert entries == (0x7730C0, 0x77EF00, 0x77EF90), tuple(hex(x) for x in entries)
# Carrier RTTI Complete Object Locator and address point. This guards the
# narrowly ported throttle hook independently of the server lifecycle vtable.
assert struct.unpack("<I", read_va(0xD596E8, 4))[0] == 0xDC8E9C
assert struct.unpack("<I", read_va(0xDC8E9C + 12, 4))[0] == 0xE37B64
assert read_va(0xE37B6C, len(b".?AUc_vehicle_server_carrier@@\0")) == b".?AUc_vehicle_server_carrier@@\0"
carrier_entries = struct.unpack("<III", read_va(0xD596EC + 55 * 4, 12))
assert carrier_entries == (0x5ECC20, 0x705BD0, 0x706C90), tuple(hex(x) for x in carrier_entries)
assert read_va(0x705BD0, 12) == bytes.fromhex("558bec83e4f081ecb8010000")
# Upstream 0.5.1 game-Lua ABI and World API sites for the 2026-10-28 rebuild.
# These are guarded in the runtime profile as well; this offline fixture makes
# sure addresses were not copied from the previous executable by accident.
assert read_va(0x4465EC, 8) == bytes.fromhex("6a20898564ffffff")
lua_helpers = {
    0x8CDBC0: "8b411485d27e0dc1",
    0x8CE350: "558bec83ec085356",
    0x8FF820: "558bec83e4f883ec",
    0x9083A0: "558bec83ec08538b",
    0x904BC0: "568b711033d26a20",
    0x9041C0: "538bd956578b7b10",
}
for va, signature in lua_helpers.items():
    assert read_va(va, len(bytes.fromhex(signature))) == bytes.fromhex(signature), f"Lua helper mismatch at {va:#x}"
world_sites = {
    0x7782C4: "ffb050010000",
    0x62AF9D: "8b576c33c0",
    0x63025F: "8b018b4010ffd0",
    0x634A91: "8b078b4010ffd0",
}
for va, signature in world_sites.items():
    assert read_va(va, len(bytes.fromhex(signature))) == bytes.fromhex(signature), f"World hook mismatch at {va:#x}"
# Entrypoints used by the new World profile resolve to executable code and
# retain their expected x86 function prologues in this exact image.
for va, expected in ((0x5EE7E0, "55"), (0x631350, "55"), (0x631150, "55"),
                     (0x62B5B0, "55"), (0x8549A0, "55"), (0x860DC0, "55"),
                     (0x62CB40, "53"), (0x628710, "53")):
    assert read_va(va, len(bytes.fromhex(expected))) == bytes.fromhex(expected), f"World function mismatch at {va:#x}"
if cc2x_source:
    assert '"1.5.18-win32-r2", 0x6ae228f3, 0x00af7000' in cc2x_source
    assert '"461e12623bacb7de29ca27b4614425c87e8df2c8327a5e958a957c850effe42a", 1' in cc2x_source
    for value in ("0x008cdbc0", "0x008ce350", "0x008ff820", "0x009083a0",
                  "0x00904bc0", "0x009041c0", "0x00d03530"):
        assert value in cc2x_source, f"Lua ABI profile value missing: {value}"
if world_source:
    assert '.build = "1.5.18-win32-r2"' in world_source
    for value in ("0x005ee7e0", "0x00631350", "0x00631150", "0x0062b5b0",
                  "0x008549a0", "0x00860dc0", "0x007782c4", "0x0062af9d",
                  "0x0063025f", "0x00634a91", "0x0063504d", "0x0062cb40",
                  "0x0062ba03", "0x00628710"):
        assert value in world_source, f"World profile value missing: {value}"
# Current animation profile: exact instruction windows, changed global operands,
# and relative destinations are checked against this authenticated EXE.
def call_target(va):
    assert read_va(va, 1) == b"\xe8", f"expected CALL at {va:#x}"
    rel = struct.unpack("<i", read_va(va + 1, 4))[0]
    return va + 5 + rel

for va in (0x7008F3, 0x700D20):
    assert read_va(va, 10) == bytes.fromhex("f30f59c78b8d00feffff"), f"route-window mismatch at {va:#x}"
route_sites = (0x6FCDA7, 0x6FD09D, 0x6FD774, 0x6FDC5B)
for va in route_sites:
    assert read_va(va, 8) == bytes.fromhex("f30f590dc479db00"), f"route multiplier mismatch at {va:#x}"
assert read_va(0x6FD9B2, 5) == bytes.fromhex("f30f5e4078")
crane_sources = (0xDB725C, 0xDB7C4C, 0xDB725C, 0xDB7C4C, 0xDB71F4,
                 0xDB7C1C, 0xDB71F4, 0xDB7C1C, 0xDB71FC, 0xDB7C24)
crane_operands = (0x701036, 0x701041, 0x701145, 0x701150, 0x701234,
                  0x70123C, 0x7012C6, 0x7012CE, 0x701329, 0x701335)
for i, (operand, source) in enumerate(zip(crane_operands, crane_sources)):
    ins = operand - 4
    opcode = "f30f1015" if i % 2 == 0 else "f30f100d"
    assert read_va(ins, 8) == bytes.fromhex(opcode) + struct.pack("<I", source), f"crane source mismatch at {ins:#x}"
lift_sites = ((0x705D20, "f30f101de871db00"), (0x705D4A, "f30f5f0d187cdb00"))
for va, signature in lift_sites:
    assert read_va(va, 8) == bytes.fromhex(signature), f"lift source mismatch at {va:#x}"
for va in (0x701065, 0x70116C, 0x70124E, 0x701294, 0x7012E4, 0x701339):
    assert call_target(va) == 0x870820, f"crane clamp helper mismatch at {va:#x}"
assert call_target(0x700FF1) == 0x844520, "crane factor's stock helper target mismatch"
assert read_va(0x870820, 12) == bytes.fromhex("f30f5fc1f30f5dc2c3cccccc")
hangar_entries = {
    0x5EC270: "558bec8b450883c0fe83f85677280fb6",
    0x5EE840: "558bec6aff689dd2c70064a100000000",
    0x5EE7E0: "558bec51538bd933c05657394344742d",
    0x8507A0: "558bec568b71080375088bc62b41043b",
    0x40FB60: "558bec83e4f883ec108b4508f30f1040",
    0x5E9D50: "8b91900000008b818c000000568bb194",
    0x40FFA0: "558bec83e4f883ec50a144dbe10033c4",
    0x5EC400: "558bec83e4f881ec24010000a144dbe1",
    0x5E9E50: "558bec83e4f881ec3c010000a144dbe1",
    0x5EEA00: "558bec6aff68dd1bca0064a100000000",
    0x5E4630: "c701000000008bc1c74104ffffffffc7",
    0x41A730: "c701000000008bc1c7410400000000c3",
    0x5EA9B0: "558bec8b895c0100008b45088b0c8185",
    0x88E1C0: "558bec6aff687ef9cc0064a100000000",
    0x411570: "558bec6aff68f0c4c70064a100000000",
    0x5721E0: "8b412cc3cccccccccccccccccccccccc",
    0x572200: "558bec578bf98b4d083b4f2c731d8b57",
    0x4380B0: "8b01c3cccccccccccccccccccccccccc",
    0x565990: "558bec6aff685d7fc90064a100000000",
}
for va, signature in hangar_entries.items():
    assert read_va(va, 16) == bytes.fromhex(signature), f"hangar native entry mismatch at {va:#x}"
assert (0x5EE846 - image_base, 3) in relocations, "hangar exception-handler absolute operand must be ASLR-relocated"
expected_hangar_relocations = {
    0x5EE840: ((6, 0x00C7D29D),), 0x40FFA0: ((10, 0x00E1DB44),),
    0x5EC400: ((13, 0x00E1DB44),), 0x5E9E50: ((13, 0x00E1DB44),),
    0x5EEA00: ((6, 0x00CA1BDD),), 0x88E1C0: ((6, 0x00CCF97E),),
    0x411570: ((6, 0x00C7C4F0),), 0x565990: ((6, 0x00C97F5D),),
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
    for value in ("0x005B555B", "0x00858740", "0x00412B30", "0x00C4C722",
                  "kScriptVectorHookBytes", "kVectorEmplaceBackPrefix",
                  "kStringReservePrefix", "kStringFreePrefix"):
        assert value in ui_hook_source, f"core UI script hook profile value missing: {value}"
assert read_va(0x005A2E70, 5) == bytes.fromhex("558bec6aff"), "definition loader entry mismatch"
assert read_va(0x00853E10, 6) == bytes.fromhex("558bec515356"), "definition name-map resize mismatch"
print("PASS SHA/PE identity, lifecycle/throttle vtables, Lua ABI, World API, all 26 animation sites/targets, hangar entries, UI helpers and definition loader/name-map entry guards")
