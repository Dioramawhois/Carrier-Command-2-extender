# SPDX-License-Identifier: GPL-3.0-or-later
"""Developer source extraction only; never installs or runs a mod or game."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def regular_below(root, relative):
    logical = PurePosixPath(relative)
    if logical.is_absolute() or '\\' in relative or ':' in relative or any(p in ('.', '..') for p in relative.split('/')):
        raise ValueError('Unsafe source path')
    path = root
    for part in logical.parts:
        path /= part
        if path.is_symlink() or getattr(path, 'is_junction', lambda: False)():
            raise ValueError('Source links are forbidden')
    if not path.is_file() or not path.resolve().is_relative_to(root):
        raise ValueError('Source file must remain below the supplied checkout')
    if path.stat().st_size > 2 * 1024 * 1024:
        raise ValueError('Source file exceeds extraction limit')
    return path


def extract(source, destination, lock):
    source = source.resolve(strict=True)
    destination = destination.absolute()
    if destination.exists() or destination.is_symlink():
        raise ValueError('Existing output is preserved')
    parent = destination.parent.resolve(strict=True)
    if parent == source or parent.is_relative_to(source) or source.is_relative_to(parent / destination.name):
        raise ValueError('Extraction must use a separate directory')
    # Validate all bytes and target names before creating anything.
    contents = {}
    for project in lock['projects']:
        name = project['directory']
        if PurePosixPath(name).name != name or name in ('', '.', '..') or '\\' in name or ':' in name:
            raise ValueError('Unsafe project directory')
        for target, entry in project['files'].items():
            logical = PurePosixPath(target)
            if logical.is_absolute() or any(p in ('', '.', '..') for p in target.split('/')) or '\\' in target or ':' in target:
                raise ValueError('Unsafe destination path')
            if logical.suffix.lower() in ('.exe', '.dll', '.pdb', '.mesh', '.txtr'):
                raise ValueError('Only reviewed source files are extracted')
            payload = regular_below(source, entry['source']).read_bytes()
            if digest(payload) != entry['sha256']:
                raise ValueError('Upstream source hash mismatch: ' + entry['source'])
            key = name + '/' + target
            if key.casefold() in {p.casefold() for p in contents}:
                raise ValueError('Duplicate output')
            contents[key] = payload
    stage = parent / ('.cc2mods-extract-' + uuid.uuid4().hex)
    stage.mkdir()
    for target, payload in contents.items():
        path = stage / target
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
    template = (ROOT / 'tools/extracted_mod_build.py.in').read_bytes()
    for project in lock['projects']:
        folder = stage / project['directory']
        for name, data in {
            'build.py': template,
            '.gitignore': b'dist/\n.cache/\n__pycache__/\n*.py[cod]\n*.exe\n*.dll\n*.pdb\n*.o\n',
            '.gitattributes': b'* -text\n',  # preserve upstream provenance bytes on every checkout
            'SOURCE_MANIFEST.json': (json.dumps({
                'upstream': lock['upstream'], 'upstream_commit': lock['commit'],
                'license': 'GPL-3.0-or-later', 'author': 'Yolo / totolerigolo118',
                'project': project, 'status': 'source-only; UI packaging/runtime acceptance pending',
            }, indent=2) + '\n').encode(),
            'README.md': ('# ' + project['name'] + '\n\n'
                'Separate source project extracted from carrier-command-2-mods.\n'
                'Original author: Yolo / totolerigolo118; GPL-3.0-or-later. See LICENSE and CREDITS_ET_LICENCE.md.\n\n'
                'Native source and tests are byte-identical to the pinned upstream commit in SOURCE_MANIFEST.json.\n'
                'No SDK, game assets or runtime DLL is embedded. `build.py --sdk PATH --zig PATH`\n'
                'builds only this native plugin using the separate CC2X SDK. It never installs or runs it.\n'
                'The report records source, SDK-header, compiler and DLL hashes.\n\n'
                'This is a developer source extraction, not a complete player package.\n'
                'Upstream UI generators in reference/ remain reference sources; they are not invoked.\n'
                'No mod.xml/Workshop ID is invented. Existing package folder: `' + project['game_folder'] + '`.\n'
                'Complete UI/resource packaging, supported-runtime review and live gameplay tests remain required.\n'
                'No changes are made to the game, Workshop, mods.xml or upstream checkout.\n').encode(),
        }.items():
            if (folder / name).exists():
                raise ValueError('Generated metadata collision')
            (folder / name).write_bytes(data)
    stage.rename(destination)
    return destination


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    lock = json.loads((ROOT / 'integrations/cc2mods-source-lock.json').read_text(encoding='utf-8'))
    revision = subprocess.check_output(['git', '-C', str(args.source), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != lock['commit']:
        raise ValueError('Use the reviewed upstream commit')
    protected = [ROOT, args.source.resolve()]
    if os.environ.get('APPDATA'):
        protected.append(Path(os.environ['APPDATA']).resolve())
    for path in protected:
        if args.output.resolve().is_relative_to(path):
            raise ValueError('Output cannot be inside SDK, upstream or application data')
    for parent in (args.output.resolve(), *args.output.resolve().parents):
        if (parent / 'carrier_command.exe').is_file():
            raise ValueError('Output cannot be inside the game installation')
    print(extract(args.source, args.output, lock))
