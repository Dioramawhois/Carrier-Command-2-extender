# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only verification of the exact stock attachment path; never runs the game."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def verify(path):
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != '461e12623bacb7de29ca27b4614425c87e8df2c8327a5e958a957c850effe42a':
        raise ValueError('Unreviewed executable')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count, optional = struct.unpack_from('<H', data, pe+6)[0], struct.unpack_from('<H', data, pe+20)[0]
    base = struct.unpack_from('<I', data, pe+24+28)[0]
    sections = [struct.unpack_from('<IIII', data, pe+24+optional+i*40+8) for i in range(count)]

    def read(va, size):
        for virtual_size, rva, raw_size, raw in sections:
            offset = va-base-rva
            if 0 <= offset and offset+size <= min(raw_size, virtual_size):
                return data[raw+offset:raw+offset+size]
        raise ValueError('Address outside file-backed section')

    guards = {0x656b90: '558bec6aff68adbbca0064a100000000',
              0x656c50: '558bec6aff68adbbca0064a100000000',
              0x5d05b0: '558bec83ec0853568b75085751568bf9',
              0x656c31: 'c20400', 0x656d0a: 'c20800', 0x5ea263: 'c21000'}
    for va, expected in guards.items():
        if read(va, len(bytes.fromhex(expected))).hex() != expected:
            raise ValueError('Signature/calling convention mismatch at '+hex(va))
    calls = {0x7799b9: 0x5e9e50, 0x779a07: 0x5d05b0, 0x779a51: 0x5ea9b0}
    for va, target in calls.items():
        instruction = read(va, 5)
        if instruction[0] != 0xe8 or va+5+struct.unpack_from('<i', instruction, 1)[0] != target:
            raise ValueError('Stock call path mismatch at '+hex(va))
    return {'status': 'passed', 'validation': 'read-only-exact-executable-static-profile',
            'executable_sha256': digest, 'entry_and_stack_guards': len(guards),
            'stock_replacement_physics_serializer_calls': len(calls), 'game_runtime_tested': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = verify(args.exe)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report))
