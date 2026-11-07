# SPDX-License-Identifier: GPL-3.0-or-later
"""Static proof of the optional surface-routing hooks. Never loads or runs an EXE."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

EXPECTED_SHA = '461e12623bacb7de29ca27b4614425c87e8df2c8327a5e958a957c850effe42a'
GUARDS = {
    0x5E173E: 'c20800',
    0x5E176E: 'c20800',
    0x5E18CA: 'c20c00',
    0x5E18F6: 'c20c00',
    0x5E21B2: '6810225e0050518bcee8c0f5ffffeb108b75088d44240c50518bcee81ef4ffff8bf88bce8d44240c50ff74240ce80cf4ffff85ff742085c0741cff75148bd08bcfff7510ff750c56e83100000083c4105f5e8be55dc35f32c05e8be55dc3',
    0x5E21BB: 'e8c0f5ffff',
    0x5E21DF: 'e80cf4ffff',
    0x5E2723: '8b423083e002',
    0x5E2729: '752ec6421001',
    0x5E340E: '807d1000741a8bcbe8d56e27008b559c',
    0x6C23B6: '56ff730c0f42d18bcfc1e206039740010000528955ece89f5300008b45ec8b55d0',
    0x6C23CC: 'e89f530000',
    0x6C2539: 'c6872602000001',
    0x6C2568: 'c6872602000001',
    0x6C295A: '0f100656ff730c51ff73108bcf83ec188bc40f1100f30f7e4610660fd64010e8a24f0000e98a1a0000',
    0x6C2979: 'e8a24f0000',
    0x6C4420: 'c22800',
    0x6C78AC: 'f20f114708f30f104424140f5ac0f20f58c1f20f114710ffb650',
    0x6C791D: 'c20c00',
    0x6C7A1B: '66c787240200000101',
    0x6C824A: 'c22800',
    0x706CE2: '8d442448c6875003000001508d87f8010000f20f1144245c508d87f0010000c744242400000000508d442428508d8fd801000051ff75188bcfff751c0f11442464ff7514ff750cff7510e8ffb5fbff8b8794000000',
    0x706D2C: 'e8ffb5fbff',
    0x708290: 'd905407adb00c20400',
    0x708296: 'c20400',
}
VTABLE = 0xD596EC


def check_branches(read):
    for address, signature in GUARDS.items():
        expected = bytes.fromhex(signature)
        if read(address, len(expected)) != expected:
            raise ValueError(f'Surface navigation instruction guard mismatch: {address:#x}')
    if read(VTABLE+0x34, 4) != struct.pack('<I', 0x708290):
        raise ValueError('Surface navigation radius vtable mismatch')
    return [{'address': a, 'bytes': b} for a, b in GUARDS.items()]


def verify(path):
    data = Path(path).read_bytes()
    if hashlib.sha256(data).hexdigest() != EXPECTED_SHA:
        raise ValueError('Unknown executable SHA-256; no surface navigation profile selected')
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    machine, count, stamp, _, _, optional_size, _ = struct.unpack_from('<HHIIIHH', data, pe+4)
    optional = pe+24
    if (data[pe:pe+4] != b'PE\0\0' or machine != 0x14C or stamp != 0x6AE228F3
            or struct.unpack_from('<H', data, optional)[0] != 0x10B
            or struct.unpack_from('<I', data, optional+28)[0] != 0x400000
            or struct.unpack_from('<I', data, optional+56)[0] != 0xAF7000):
        raise ValueError('Surface navigation PE profile mismatch')
    sections = [struct.unpack_from('<IIII', data, optional+optional_size+i*40+8)
                for i in range(count)]

    def read(address, length):
        relative = address-0x400000
        for _, start, size, raw in sections:
            if start <= relative and relative+length <= start+size:
                value = data[raw+relative-start:raw+relative-start+length]
                if len(value) == length:
                    return value
        raise ValueError(f'Surface navigation address not file-backed: {address:#x}')

    return {'schema': 1, 'profile': '1.5.18-win32-r2', 'exe_sha256': EXPECTED_SHA,
            'static_passed': True, 'guards': check_branches(read),
            'abi': {'follower': 'thiscall; 10 stack words; ret 0x28; return unused',
                    'helper': 'thiscall; 10 stack words; ret 0x28; return unused',
                    'projection': 'thiscall; 3 stack words; ret 0x0c; return unused',
                    'nearest': 'thiscall; 2 stack words; ret 8; pointer result',
                    'filtered_nearest': 'thiscall; 3 stack words; ret 0x0c; pointer result',
                    'radius': 'thiscall; 1 stack word; ret 4; x87 float result',
                    'astar': 'unchanged register ABI; caller cleans four stack words'},
            'gate': {'site': 0x5E2723, 'length': 6, 'resume': 0x5E2729,
                     'rejected': 0x5E272B, 'admitted': 0x5E2759,
                     'global_triangle_flags_modified': False},
            'native_ocean_initializes_success': True,
            'runtime_enabled': False, 'game_runtime_tested': False, 'gameplay_tested': False,
            'limitation': 'Matched bytes and ABI only; live routing and hull clearance require owner acceptance'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = json.dumps(verify(args.exe), indent=2)+'\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report, encoding='utf-8')
    print(report, end='')
