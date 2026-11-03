# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a separate Lua/content mod using the external SDK. Never installs it.

Only the project-owned Python recipe may generate data. Lua is syntax-checked,
never executed; native binaries/sources cannot enter a player Lua package.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


packager = load('cc2x_byte_packager', ROOT / 'tools/mod_package.py')


def collect(project, metadata):
    project = project.resolve(strict=True)
    if metadata.get('plugin'):
        raise ValueError('Lua packages cannot declare a native plugin')
    if not (project / 'content/bridge.json').is_file():
        raise ValueError('An independent Lua module manifest is required')
    resources, sources = {}, {}
    pending = [project / 'content']
    paths = []
    while pending:
        directory = pending.pop()
        if len(directory.relative_to(project).parts) > 32:
            raise ValueError('Lua source path depth limit exceeded')
        with os.scandir(directory) as entries:
            for entry in entries:
                path = Path(entry.path)
                if path.is_symlink() or getattr(path, 'is_junction', lambda: False)():
                    raise ValueError('Lua source content cannot contain links')
                if entry.is_dir(follow_symlinks=False):
                    pending.append(path)
                elif entry.is_file(follow_symlinks=False):
                    paths.append(path)
                else:
                    raise ValueError('Only ordinary Lua content files are supported')
                if len(paths) + len(pending) > 19900:
                    raise ValueError('Too many Lua/content resources')
    for path in sorted(paths):
        name = path.relative_to(project).as_posix()
        if path.suffix.casefold() in ('.dll', '.exe', '.lib', '.pdb', '.c', '.cpp', '.h', '.obj'):
            raise ValueError('Native code is forbidden in Lua/content packages')
        resources[name] = packager.regular(project, name)
        sources[name] = {'path': name, 'sha256': packager.sha(resources[name])}
        if len(resources) > 19900:
            raise ValueError('Too many Lua/content resources')
    for target, source in metadata.get('resources', {}).items():
        name = packager.relative(target)
        if name in resources or Path(name).suffix.casefold() in ('.dll', '.exe', '.c', '.cpp', '.h', '.lib', '.pdb', '.obj'):
            raise ValueError('Duplicate or native explicit resource')
        resources[name] = packager.regular(project, source)
        sources[name] = {'path': source, 'sha256': packager.sha(resources[name])}
    if (project / 'thumbnail.png').is_file() and 'thumbnail.png' not in resources:
        resources['thumbnail.png'] = packager.regular(project, 'thumbnail.png')
        sources['thumbnail.png'] = {'path': 'thumbnail.png', 'sha256': packager.sha(resources['thumbnail.png'])}
    return resources, sources


def main(project):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path)  # resolved by the project's bootstrap
    parser.add_argument('--game', type=Path)
    parser.add_argument('--luac', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    project = project.resolve(strict=True)
    metadata = json.loads(packager.regular(project, 'package.json').decode('utf-8'))
    resources, sources = collect(project, metadata)
    provenance = {'project_commit': subprocess.check_output(['git', '-C', str(project), 'rev-parse', 'HEAD'], text=True).strip(),
                  'project_dirty': bool(subprocess.check_output(['git', '-C', str(project), 'status', '--porcelain'], text=True).strip()),
                  'package_spec_sha256': packager.sha(packager.regular(project, 'package.json')),
                  'source_sha256': sources, 'gameplay_language': 'Lua',
                  'sdk_lua_api': metadata.get('lua_api', 2),
                  'runtime_capabilities': metadata.get('runtime_capabilities', ['lua-mod-api-v2'])}
    recipe = project / 'package_recipe.py'
    if recipe.is_file():
        generated, report = load('owned_lua_data_recipe', recipe).resources(
            args.game.resolve(strict=True) if args.game else None, packager.regular)
        if resources.keys() & generated.keys():
            raise ValueError('Generated resource collision')
        resources.update(generated)
        provenance.update(report)
        provenance['recipe_sha256'] = packager.sha(packager.regular(project, 'package_recipe.py'))
    if any(Path(name).suffix.casefold() in ('.dll', '.exe', '.c', '.cpp', '.h', '.lib', '.pdb', '.obj') for name in resources):
        raise ValueError('Generated native code is forbidden')
    if 'thumbnail.png' not in resources:
        raise ValueError('A Workshop preview is required before packaging')
    output = args.output or project / 'dist' / (metadata['id'] + '-lua-' + metadata['version'])
    if not output.parent.exists():
        if output.parent != project / 'dist':
            raise ValueError('External output parent must already exist')
        output.parent.mkdir()
    print(packager.package(project, metadata, None, None, resources, output,
                           provenance=provenance, luac=args.luac.resolve(strict=True)))
