# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile-only comparison with a supplied fork header; never run its code.

Names are selected from our header; the C++ compiler checks actual layouts and
enum values. This is not a C parser or a runtime compatibility certificate.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--reference-header', type=Path, required=True)
    parser.add_argument('--zig', type=Path, required=True)
    args = parser.parse_args()
    local = ROOT / 'include/cc2x.h'
    foreign = args.reference_header.resolve(strict=True)
    text = re.sub(r'/\*.*?\*/', '', local.read_text(), flags=re.S)
    api = text[text.index('uint32_t api_version;'):text.index('} cc2x_api;')]
    fields = ['api_version', 'size', 'sdk_version', 'game_build', 'module_base', 'game_dir', 'config_dir']
    fields += [m.group(1) for statement in api.split(';')
               if (m := re.search(r'\(\*(\w+)\)', statement))]
    blocks = re.findall(r'enum\s*\{(.*?)\};', text, re.S)
    constants = sorted(set(re.findall(r'\bCC2X_[A-Z_0-9]+\b', '\n'.join(blocks))))
    lines = ['#include <stdint.h>', '#include <stddef.h>', '#include <math.h>', '#include <wchar.h>',
             'namespace current {', '#include "' + local.as_posix() + '"', '}',
             '#undef CC2X_H', '#undef CC2X_API_VERSION',
             'namespace partner {', '#include "' + foreign.as_posix() + '"', '}']
    for field in fields:
        lines.append('static_assert(offsetof(current::cc2x_api, ' + field
                     + ') == offsetof(partner::cc2x_api, ' + field + '), "prefix ' + field + '");')
    for name in constants:
        lines.append('static_assert((int)current::' + name + ' == (int)partner::' + name
                     + ', "enum ' + name + '");')
    for name in ('cc2x_cpu', 'cc2x_body', 'cc2x_hit', 'cc2x_vehicle_info', 'cc2x_tile_info'):
        lines.append('static_assert(sizeof(current::' + name + ') == sizeof(partner::' + name
                     + ') && alignof(current::' + name + ') == alignof(partner::' + name
                     + '), "record ' + name + '");')
    with tempfile.TemporaryDirectory(prefix='cc2x-prefix-') as folder:
        probe = Path(folder) / 'probe.cpp'
        probe.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        subprocess.run([str(args.zig.resolve()), 'c++', '-target', 'x86-windows-gnu',
                        '-std=c++17', '-c', str(probe), '-o', str(probe.with_suffix('.obj'))], check=True)
    print({'prefix_offsets': len(fields), 'enum_values': len(constants),
           'record_sizes_and_alignments': 5, 'runtime_tested': False})
