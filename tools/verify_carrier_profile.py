# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only carrier instruction/vtable verification; never loads the executable."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

PROFILES = {
    '461e12623bacb7de29ca27b4614425c87e8df2c8327a5e958a957c850effe42a':
        ('1.5.18-win32-r2', 0x6ae228f3, 0xd596ec, 0x707440,
         (0x7019de, 0x701b4d, 0x701b9a, 0x6fe580, 0x6ff31a, 0x6fdded, 0x6fde02)),
    '1c9a01c3007cb2bec2a04d716812cf9f24c87efa11056395c89ddd7d2c341026':
        ('1.5.18-win32', 0x6a36c177, 0xd596cc, 0x707330,
         (0x7018ce, 0x701a3d, 0x701a8a, 0x6fe470, 0x6ff20a, 0x6fdcdd, 0x6fdcf2)),
}
GUARDS = ('8b400483f805', '8d8528fdffff', 'ffb5fcfdffff8d4f34',
          'c685fbfdffff01', 'f30f10951cfdfffff30f1085ccfdffff',
          'f30f59d2f30f59db', '8b8d88fdffff034f3c')
DOCK = '558bec515657ff750c8bf18b4d08'


def verify(path):
    data = Path(path).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest not in PROFILES:
        raise ValueError('Unknown executable SHA-256; no profile selected')
    name, stamp, vtable, dock, addresses = PROFILES[digest]
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    machine, count, timestamp, _, _, opt_size, _ = struct.unpack_from('<HHIIIHH', data, pe + 4)
    optional = pe + 24
    if (data[pe:pe+4] != b'PE\0\0' or machine != 0x14c or timestamp != stamp
            or struct.unpack_from('<H', data, optional)[0] != 0x10b
            or struct.unpack_from('<I', data, optional + 56)[0] != 0xaf7000):
        raise ValueError('Executable PE profile mismatch')
    base = struct.unpack_from('<I', data, optional + 28)[0]
    if base != 0x400000:
        raise ValueError('Unexpected preferred image base')
    sections = [struct.unpack_from('<IIII', data, optional + opt_size + i*40 + 8)
                for i in range(count)]

    def read(va, size):
        rva = va - base
        for _, start, raw_size, raw in sections:
            if start <= rva and rva + size <= start + raw_size:
                result = data[raw + rva-start:raw + rva-start + size]
                if len(result) == size:
                    return result
        raise ValueError(f'Address not file backed: {va:#x}')

    guards = []
    for address, expected in [(dock, DOCK), *zip(addresses, GUARDS, strict=True)]:
        if read(address, len(bytes.fromhex(expected))) != bytes.fromhex(expected):
            raise ValueError(f'Carrier signature mismatch: {address:#x}')
        guards.append({'address': address, 'bytes': expected})
    if struct.unpack('<I', read(vtable + 0x7c, 4))[0] != dock:
        raise ValueError('Carrier vtable dock entry mismatch')
    return {'schema': 1, 'profile': name, 'exe_sha256': digest, 'guards': guards,
            'carrier_vtable': vtable, 'dock_entry': dock, 'static_passed': True,
            'game_runtime_tested': False, 'gameplay_tested': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    evidence = json.dumps(verify(args.exe), indent=2) + '\n'
    if args.output:
        args.output.write_text(evidence, encoding='utf-8')
    print(evidence, end='')
