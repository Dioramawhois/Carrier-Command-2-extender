# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract the reviewed team aircraft package, preserving its identity and bytes.

Developer source preparation only. Never installs or executes a mod or DLL.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('source_paths', ROOT / 'tools/extract_cc2mods.py')
paths = importlib.util.module_from_spec(spec)
spec.loader.exec_module(paths)


def extract(source, destination, lock):
    source = source.resolve(strict=True)
    destination = destination.absolute()
    if destination.exists() or destination.is_symlink():
        raise ValueError('Existing output is preserved')
    parent = destination.parent.resolve(strict=True)
    target = parent / destination.name
    protected = [source, ROOT]
    if os.environ.get('APPDATA'):
        protected.append(Path(os.environ['APPDATA']).resolve())
    if any(target.is_relative_to(p) or p.is_relative_to(target) for p in protected):
        raise ValueError('Output must be separate from upstream, SDK and application data')
    if any((p / 'carrier_command.exe').is_file() for p in (parent, *parent.parents)):
        raise ValueError('Output must be outside the game installation')
    contents = {}
    keys = set()
    for relative, entry in lock['files'].items():
        logical = PurePosixPath(relative)
        if (logical.is_absolute() or any(p in ('', '.', '..') for p in relative.split('/'))
                or '\\' in relative or ':' in relative
                or logical.suffix.lower() not in ('.md', '.lua', '.xml', '.json', '.png', '.mesh', '.txtr')
                or relative.casefold() in keys):
            raise ValueError('Unsafe or unsupported aircraft destination')
        keys.add(relative.casefold())
        data = paths.regular_below(source, entry['source']).read_bytes()
        if hashlib.sha256(data).hexdigest() != entry['sha256']:
            raise ValueError('Upstream hash mismatch: ' + entry['source'])
        contents[relative] = data
    generated = {
        'LICENSE': (ROOT / 'LICENSE').read_bytes(),
        '.gitattributes': b'* -text\n',
        '.gitignore': b'dist/\n.cache/\n__pycache__/\n*.py[cod]\n*.dll\n*.exe\n',
        'SOURCE_MANIFEST.json': (json.dumps({
            **lock, 'license': 'GPL-3.0-or-later',
            'authorization': 'Project owner confirmed all permissions for team code/assets',
            'status': 'separate developer mod package; fresh game acceptance pending',
        }, indent=2) + '\n').encode(),
        'cc2x-sdk.json': (json.dumps({
            'sdk': 'cc2x.runtime', 'minimum_version': '0.6.0-dev', 'api_version': 8,
            'owner_id': lock['owner_id'], 'embedded_sdk': False,
            'required_capabilities': ['independent-lua-v1'],
            'multiplayer_verified': False,
        }, indent=2) + '\n').encode(),
        'NOTICE.md': b'Team Native Bridge aircraft contribution, integrated with owner permission.\n'
                     b'GPL-3.0-or-later; original author attribution is retained in mod.xml.\n'
                     b'This package contains no SDK, native loader or stock game Lua scripts.\n',
        'README.md': b'# F-16 / F-15 - separate CC2X consumer\n\n'
            b'This evolves the existing team aircraft package, preserving its mod.xml, Bridge owner ID,\n'
            b'F-16/F-15 identities, declaration ranks, hotkeys and resource paths byte-for-byte.\n'
            b'No new Workshop item or replacement identity is created. See UPSTREAM_README.md\n'
            b'for the original disposable-save check and SOURCE_MANIFEST.json for source hashes.\n\n'
            b'The external CC2X 0.6 development SDK supplies require("cc2"), isolated Lua VMs,\n'
            b'generic registration and resource operations. No SDK C/C++ or DLL is embedded.\n'
            b'Development SDK path convention: --sdk/CC2X_SDK_DIR, otherwise ../../cc2x.\n\n'
            b'This is source preparation, not an installation. The released ModLoader beta still\n'
            b'needs module-container publishing and an approved runtime profile before selecting\n'
            b'this package there. Do not flatten bridge.json/main.lua from multiple modules.\n'
            b'Fresh game acceptance, changed-catalog saves, networking and dedicated support\n'
            b'are unverified. Do not claim multiplayer or publish it as a finished player release.\n',
    }
    if set(contents) & set(generated):
        raise ValueError('Generated metadata collision')
    with tempfile.TemporaryDirectory(prefix='.cc2x-aircraft-', dir=parent) as temporary:
        stage = Path(temporary) / 'package'
        stage.mkdir()
        for relative, data in {**contents, **generated}.items():
            path = stage / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        stage.rename(target)
    return target


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True, help='Team Native Bridge snapshot')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    lock = json.loads((ROOT / 'integrations/aircraft-source-lock.json').read_text())
    print(extract(args.source, args.output, lock))
