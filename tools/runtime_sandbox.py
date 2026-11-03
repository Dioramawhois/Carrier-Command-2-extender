# SPDX-License-Identifier: GPL-3.0-or-later
"""Stage a separate legitimate client copy and a private profile for runtime tests.

The Windows APPDATA environment alone does not redirect SHGetFolderPathA.
An explicitly test-only import adapter is independently verified first. Neither
the installed game nor its real user profile is edited. This never publishes.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import secrets
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def pe_exports(path):
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe+4] != b'PE\0\0' or struct.unpack_from('<H', data, pe+4)[0] != 0x14c:
        raise ValueError('Expected legitimate PE32 x86 library')
    opt, count = pe+24, struct.unpack_from('<H', data, pe+6)[0]
    section_table = opt+struct.unpack_from('<H', data, pe+20)[0]
    sections = [struct.unpack_from('<IIII', data, section_table+i*40+8) for i in range(count)]
    def offset(rva):
        for _, base, size, raw in sections:
            if base <= rva < base+size:
                return raw+rva-base
        raise ValueError('Unmapped export RVA')
    table = offset(struct.unpack_from('<I', data, opt+96)[0])
    base, functions, names_count, _, names, ordinals = struct.unpack_from('<IIIIII', data, table+16)
    result = []
    if functions > 65536 or names_count > functions:
        raise ValueError('Unexpected export count')
    for i in range(names_count):
        start = offset(struct.unpack_from('<I', data, offset(names)+4*i)[0])
        name = data[start:data.index(b'\0', start)].decode('ascii')
        if not name.replace('_', '').isalnum():
            raise ValueError('Unsupported export name')
        ordinal = base+struct.unpack_from('<H', data, offset(ordinals)+2*i)[0]
        result.append((name, ordinal))
    return result

def compile_proxy(compiler, output, definition):
    subprocess.run([str(compiler), 'cc', '-target', 'x86-windows-gnu', '-std=c11', '-O1', '-shared',
                    str(ROOT / 'tests/runtime/isolation_proxy.c'), str(definition), '-lshell32', '-o', str(output)], check=True)

def real_profile_hashes():
    root = Path(os.environ['APPDATA']) / 'Carrier Command 2'
    return {p.relative_to(root).as_posix(): sha(p) for p in sorted(root.rglob('*')) if p.is_file() and
            p.relative_to(root).parts[0] not in ('mods', 'cc2x')}

def stage(game, mod, compiler, sandbox):
    game, mod, compiler = game.resolve(strict=True), mod.resolve(strict=True), compiler.resolve(strict=True)
    sandbox = sandbox.absolute()
    if sandbox.exists() or sandbox.is_symlink() or getattr(sandbox, 'is_junction', lambda: False)():
        raise ValueError('Existing sandbox is preserved')
    profile = json.loads((ROOT / 'integrations/waypoint-origin-profile.json').read_text())
    if sha(game / 'carrier_command.exe') != profile['exe_sha256']:
        raise ValueError('Unreviewed client executable')
    build = json.loads((ROOT / 'dist/dinput8.build.json').read_text())
    evidence = json.loads((ROOT / 'build/unified/test-results.json').read_text())
    if evidence['status'] != 'passed' or evidence['artifact_sha256'] != build['artifact_sha256'] or sha(ROOT / 'dist/dinput8.dll') != build['artifact_sha256']:
        raise ValueError('Matching passing SDK artifact required')
    if sha(compiler) != build['compiler_sha256']:
        raise ValueError('Compiler differs from SDK evidence')
    for name,digest in build['source_sha256'].items():
        if sha(ROOT / name) != digest:
            raise ValueError('SDK inputs changed: '+name)
    for path in mod.rglob('*'):
        if path.is_symlink() or getattr(path, 'is_junction', lambda: False)():
            raise ValueError('Test mod cannot contain links')
        if path.suffix.lower() in ('.dll', '.exe', '.c', '.cpp'):
            raise ValueError('Expected the separate native-free Lua gameplay package')
    before = real_profile_hashes()
    sandbox.mkdir(parents=True)
    destination = sandbox / 'game'
    destination.mkdir()
    private = sandbox / 'profile'
    user = private / 'Carrier Command 2'
    user.mkdir(parents=True)
    definition = sandbox / 'steam-forward.def'
    exports = pe_exports(game / 'steam_api.dll')
    definition.write_text('LIBRARY steam_api\nEXPORTS\n'+''.join(
        f'    {name}=steam_api_real.{name} @{ordinal}\n' for name,ordinal in exports), encoding='ascii')
    manifest = {'owner': 'cc2x.isolated.test.v1', 'game_source': str(game), 'sandbox': str(sandbox),
                'profile': str(private), 'sdk_sha256': build['artifact_sha256'],
                'real_profile_before_sha256': before, 'game_runtime_tested': False}
    (sandbox / 'isolation.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    environment = os.environ.copy()
    environment['CC2X_TEST_PROFILE_ROOT'] = str(private)
    environment['APPDATA'] = str(private)
    compile_proxy(compiler, destination / 'steam_api.dll', definition)
    # Pinned Zig's x86 MinGW .def forwarders use this leading-underscore module
    # name. The probe resolves a real forwarded export before any game launch.
    shutil.copy2(game / 'steam_api.dll', destination / '_steam_api_real.dll')
    # Verify the real Windows import redirection using our own executable first.
    probe = destination / 'carrier_command.exe'
    subprocess.run([str(compiler), 'cc', '-target', 'x86-windows-gnu', '-O1',
                    str(ROOT / 'tests/runtime/isolation_probe.c'), '-lshell32', '-o', str(probe)], check=True)
    subprocess.run([str(probe)], cwd=destination, env=environment, check=True, timeout=15)
    # The probe file is known, owned and non-recursive. Replace only that file.
    probe.unlink()
    for name in ('carrier_command.exe', 'openvr_api.dll', 'steam_appid.txt'):
        shutil.copy2(game / name, destination / name)
    shutil.copy2(ROOT / 'dist/dinput8.dll', destination / 'dinput8.dll')
    for name in ('rom_0', 'libraries'):
        shutil.copytree(game / name, destination / name)
    selected = user / 'mods/Island Airfields'
    shutil.copytree(mod, selected)
    config = ET.Element('data')
    active = ET.SubElement(config, 'active_mod_folders')
    ET.SubElement(active, 'path', value=str(selected))
    ET.ElementTree(config).write(user / 'mods.xml', encoding='utf-8', xml_declaration=True)
    settings = ET.Element('data', uuid=secrets.token_hex(32), version='1', gfx_fullscreen='false', gfx_vsync='true', audio_mute='true',
                          audio_volume_master='0', language='0', ui_scale='1')
    ET.SubElement(settings, 'gfx_resolution', x='1280', y='720')
    ET.ElementTree(settings).write(user / 'settings.xml', encoding='utf-8', xml_declaration=True)
    if before != real_profile_hashes():
        raise RuntimeError('Real profile changed during staging; no game launch is authorized by this adapter')
    manifest['isolation_probe_passed'] = True
    manifest['copied_exe_sha256'] = sha(destination / 'carrier_command.exe')
    manifest['adapter_sha256'] = sha(destination / 'steam_api.dll')
    (sandbox / 'isolation.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'staged': str(sandbox), 'isolation_probe': 'passed', 'game_launched': False}))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--mod', type=Path, required=True)
    parser.add_argument('--zig', type=Path, required=True)
    parser.add_argument('--sandbox', type=Path, required=True)
    args = parser.parse_args()
    stage(args.game,args.mod,args.zig,args.sandbox)
