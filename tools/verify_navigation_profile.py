# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only proof of carrier native waypoint/manual branches; never loads or patches an EXE."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

EXPECTED_SHA = '461e12623bacb7de29ca27b4614425c87e8df2c8327a5e958a957c850effe42a'
VTABLE = 0xD596EC
MOTION = 0x706C90
GUARDS = {
    MOTION: '558bec83e4f883ec58',
    # mode 0 selects the native path follower; nonzero branches to manual/idle.
    0x706CD4: '8b87d001000085c00f85c4020000',
    # Native path follower receives target +0x1d8 and speed output +0x1f8.
    0x706D0F: '8d8fd801000051ff75188bcfff751c0f11442464ff7514ff750cff7510e8ffb5fbff',
    # mode 1 reads physical helm factors; +0x39e selects side thrusters.
    0x706FA6: '83f801755b80bf9e03000000f30f1087900c00007416f30f108f940c0000',
    # Seven stack words, including an argument omitted by old decompiler output.
    0x707429: '5f5e8be55dc21c00',
}


def check_branches(read):
    """Inspect file-backed bytes through a bounded reader, without executing them."""
    for address, signature in GUARDS.items():
        expected = bytes.fromhex(signature)
        if read(address, len(expected)) != expected:
            raise ValueError(f'Navigation instruction guard mismatch: {address:#x}')
    if read(VTABLE + 56*4, 8) != struct.pack('<II', 0x705BD0, MOTION):
        raise ValueError('Carrier update/motion vtable mismatch')
    return [{'address': address, 'bytes': signature} for address, signature in GUARDS.items()]


def verify(path):
    data = Path(path).read_bytes()
    if hashlib.sha256(data).hexdigest() != EXPECTED_SHA:
        raise ValueError('Unknown executable SHA-256; no navigation profile selected')
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    machine, count, stamp, _, _, optional_size, _ = struct.unpack_from('<HHIIIHH', data, pe+4)
    optional = pe+24
    if (data[pe:pe+4] != b'PE\0\0' or machine != 0x14C or stamp != 0x6AE228F3
            or struct.unpack_from('<H', data, optional)[0] != 0x10B
            or struct.unpack_from('<I', data, optional+28)[0] != 0x400000
            or struct.unpack_from('<I', data, optional+56)[0] != 0xAF7000):
        raise ValueError('Navigation PE profile mismatch')
    sections = [struct.unpack_from('<IIII', data, optional+optional_size+i*40+8)
                for i in range(count)]

    def read(address, length):
        relative = address-0x400000
        for _, start, size, raw in sections:
            if start <= relative and relative+length <= start+size:
                value = data[raw+relative-start:raw+relative-start+length]
                if len(value) == length:
                    return value
        raise ValueError(f'Navigation address not file-backed: {address:#x}')

    return {'schema': 1, 'profile': '1.5.18-win32-r2', 'exe_sha256': EXPECTED_SHA,
            'static_passed': True, 'carrier_vtable': VTABLE, 'motion_function': MOTION,
            'native_waypoint_mode': 0, 'manual_helm_mode': 1, 'guards': check_branches(read),
            'native_path_function': 0x6C2330, 'runtime_enabled': False,
            'game_runtime_tested': False, 'gameplay_tested': False,
            'limitation': 'Branch presence only; speed regulation, ownership and save/load are unverified'}


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
