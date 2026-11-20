# SPDX-License-Identifier: GPL-3.0-or-later
"""Static proof of the optional surface-routing hooks. Never loads or runs an EXE."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

EXPECTED_SHA = '1df609c8252fc0245decfab793e3b8e1884e553539a8831cbee67663b6725920'
GUARDS = {
    0x5e189e: 'c20800',
    0x5e18ce: 'c20800',
    0x5e1a2a: 'c20c00',
    0x5e1a56: 'c20c00',
    0x5e2312: '6870235e0050518bcee8c0f5ffffeb108b75088d44240c50518bcee81ef4ffff8bf88bce8d44240c50ff74240ce80cf4ffff85ff742085c0741cff75148bd08bcfff7510ff750c56e83100000083c4105f5e8be55dc35f32c05e8be55dc3',
    0x5e231b: 'e8c0f5ffff',
    0x5e233f: 'e80cf4ffff',
    0x5e2883: '8b423083e002',
    0x5e2889: '752ec6421001',
    0x5e356e: '807d1000741a8bcbe8357527008b559c',
    0x6c2606: '56ff730c0f42d18bcfc1e206039740010000528955ece89f5300008b45ec8b55d0',
    0x6c261c: 'e89f530000',
    0x6c2789: 'c6872602000001',
    0x6c27b8: 'c6872602000001',
    0x6c2baa: '0f100656ff730c51ff73108bcf83ec188bc40f1100f30f7e4610660fd64010e8a24f0000e98a1a0000',
    0x6c2bc9: 'e8a24f0000',
    0x6c4670: 'c22800',
    0x6c7afc: 'f20f114708f30f104424140f5ac0f20f58c1f20f114710ffb650',
    0x6c7b6d: 'c20c00',
    0x6c7c6b: '66c787240200000101',
    0x6c849a: 'c22800',
    0x706f32: '8d442448c6875003000001508d87f8010000f20f1144245c508d87f0010000c744242400000000508d442428508d8fd801000051ff75188bcfff751c0f11442464ff7514ff750cff7510e8ffb5fbff8b8794000000',
    0x706f7c: 'e8ffb5fbff',
    0x7084e0: 'd905e067db00c20400',
    0x7084e6: 'c20400',
}
VTABLE = 0xd589d0


def check_branches(read):
    for address, signature in GUARDS.items():
        expected = bytes.fromhex(signature)
        if read(address, len(expected)) != expected:
            raise ValueError(f'Surface navigation instruction guard mismatch: {address:#x}')
    if read(VTABLE+0x34, 4) != struct.pack('<I', 0x7084e0):
        raise ValueError('Surface navigation radius vtable mismatch')
    return [{'address': a, 'bytes': b} for a, b in GUARDS.items()]


def verify(path):
    data = Path(path).read_bytes()
    if hashlib.sha256(data).hexdigest() != EXPECTED_SHA:
        raise ValueError('Unknown executable SHA-256; no surface navigation profile selected')
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    machine, count, stamp, _, _, optional_size, _ = struct.unpack_from('<HHIIIHH', data, pe+4)
    optional = pe+24
    if (data[pe:pe+4] != b'PE\0\0' or machine != 0x14C or stamp != 0x6AFC5C17
            or struct.unpack_from('<H', data, optional)[0] != 0x10B
            or struct.unpack_from('<I', data, optional+28)[0] != 0x400000
            or struct.unpack_from('<I', data, optional+56)[0] != 0xaf6000):
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

    return {'schema': 1, 'profile': '1.5.20-win32', 'exe_sha256': EXPECTED_SHA,
            'static_passed': True, 'guards': check_branches(read),
            'abi': {'follower': 'thiscall; 10 stack words; ret 0x28; return unused',
                    'helper': 'thiscall; 10 stack words; ret 0x28; return unused',
                    'projection': 'thiscall; 3 stack words; ret 0x0c; return unused',
                    'nearest': 'thiscall; 2 stack words; ret 8; pointer result',
                    'filtered_nearest': 'thiscall; 3 stack words; ret 0x0c; pointer result',
                    'radius': 'thiscall; 1 stack word; ret 4; x87 float result',
                    'astar': 'unchanged register ABI; caller cleans four stack words'},
            'gate': {'site': 0x5e2883, 'length': 6, 'resume': 0x5e2889,
                     'rejected': 0x5e288b, 'admitted': 0x5e28b9,
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
