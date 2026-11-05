# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only verification of the isolated CPU tile placement path; never runs the game."""
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

    guards = {0x625fd0:'538bdc83ec0883e4f883c404558b6b04',
              0x626310:'558bec6aff68b457ca0064a100000000',
              0x626420:'558bec6aff683858ca0064a100000000',
              0x626f50:'558bec6aff684459ca0064a100000000',
              0xc3f9fd:'558beceb0dff7508e8daf601005985c0',
              0x626264:'c20800', 0x62640f:'c21800',
              0x626965:'c3', 0x627031:'c20400',
              0x631074:'c7460401000000'}
    for va, expected in guards.items():
        if read(va, len(bytes.fromhex(expected))).hex() != expected:
            raise ValueError('Signature/calling convention mismatch at '+hex(va))
    calls = {0x631010:0xc3f9fd, 0x63103f:0x626310}
    for va, target in calls.items():
        instruction = read(va, 5)
        if instruction[0] != 0xe8 or va+5+struct.unpack_from('<i', instruction, 1)[0] != target:
            raise ValueError('Stock call path mismatch at '+hex(va))
    return {'status': 'passed', 'validation': 'read-only-exact-executable-static-profile',
            'executable_sha256': digest, 'entry_and_stack_guards': len(guards),
            'stock_task_producer_calls': len(calls), 'game_runtime_tested': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = verify(args.exe)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report))
