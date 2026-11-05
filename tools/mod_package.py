# SPDX-License-Identifier: GPL-3.0-or-later
"""Generic developer packaging. Copies bytes; never installs or runs a mod."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import tempfile
from xml.etree import ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def relative(value):
    path = PurePosixPath(value)
    if (path.is_absolute() or any(p in ('', '.', '..') for p in value.split('/'))
            or len(path.parts) > 32 or any(c in value for c in '\\:\x00<>"|?*')):
        raise ValueError('Unsafe package resource path')
    for part in path.parts:
        if part.endswith(('.', ' ')) or part.split('.')[0].casefold() in {
                'con', 'prn', 'aux', 'nul', *(f'com{i}' for i in range(1, 10)),
                *(f'lpt{i}' for i in range(1, 10))}:
            raise ValueError('Reserved package path')
    return path.as_posix()


def regular(root, name):
    path = root
    for part in PurePosixPath(relative(name)).parts:
        path /= part
        if path.is_symlink() or getattr(path, 'is_junction', lambda: False)():
            raise ValueError('Source links are forbidden')
    if not path.is_file() or not path.resolve().is_relative_to(root):
        raise ValueError('Source escapes project')
    if path.stat().st_size > 16 * 1024 * 1024:
        raise ValueError('Source size limit exceeded')
    return path.read_bytes()


def validate_screen_descriptor(value):
    if (not isinstance(value, dict) or set(value) != {'schema', 'extensions'}
            or type(value['schema']) is not int or value['schema'] not in (1, 2)
            or not isinstance(value['extensions'], list) or len(value['extensions']) > 16):
        raise ValueError('Invalid screen extension descriptor')
    targets, sources = set(), []
    for entry in value['extensions']:
        if not isinstance(entry, dict) or set(entry) not in ({'screen', 'source'}, {'screen', 'source', 'profiles'}):
            raise ValueError('Invalid screen extension entry')
        for key, prefix in [('screen', 'scripts/'), ('source', 'cc2x/ui/')]:
            item = entry[key]
            if (not isinstance(item, str) or len(item) > 240 or not item.startswith(prefix)
                    or not item.endswith('.lua') or not re.fullmatch(r'[A-Za-z0-9_.\-/]+', item)):
                raise ValueError('Invalid screen extension path')
            relative(item)
        if entry['screen'] in targets:
            raise ValueError('Duplicate screen target')
        targets.add(entry['screen']); sources.append('content/' + entry['source'])
        if 'profiles' not in entry:
            continue
        profiles = entry['profiles']
        if value['schema'] != 2 or not isinstance(profiles, list) or not 1 <= len(profiles) <= 8:
            raise ValueError('Invalid screen source profiles')
        digests, inserted = set(), 0
        for profile in profiles:
            if (not isinstance(profile, dict) or set(profile) != {'sha256', 'edits'}
                    or not isinstance(profile['sha256'], str)
                    or not re.fullmatch(r'[0-9a-f]{64}', profile['sha256'])
                    or profile['sha256'] in digests or not isinstance(profile['edits'], list)
                    or not 1 <= len(profile['edits']) <= 64):
                raise ValueError('Invalid or duplicate source profile')
            digests.add(profile['sha256'])
            previous_offset, previous_end = -1, 0
            for edit in profile['edits']:
                if (not isinstance(edit, dict) or set(edit) != {'offset', 'remove', 'insert'}
                        or type(edit['offset']) is not int or type(edit['remove']) is not int
                        or edit['offset'] <= previous_offset or edit['offset'] < previous_end
                        or edit['remove'] < 0 or edit['offset'] + edit['remove'] > 2*1024*1024
                        or not isinstance(edit['insert'], str) or '\0' in edit['insert']):
                    raise ValueError('Invalid, overlapping or out-of-range source edit')
                inserted += len(edit['insert'].encode('utf-8'))
                if inserted > 256*1024:
                    raise ValueError('Source edit byte limit exceeded')
                previous_offset, previous_end = edit['offset'], edit['offset'] + edit['remove']
    return sources


def validate_resource(name, data, luac):
    suffix = PurePosixPath(name).suffix.lower()
    if suffix == '.json' and name.casefold() in {
            'content/bridge.json', 'content/native_bridge/bridge.json', 'content/cc2x/screen_extensions.json'}:
        if len(data) > 65536:
            raise ValueError('Module manifest exceeds 64 KiB')
        def unique(pairs):
            result = {}
            for key, value in pairs:
                if key in result:
                    raise ValueError('Duplicate module JSON key')
                result[key] = value
            return result
        value = json.loads(data.decode('utf-8-sig'), object_pairs_hook=unique)
        if not isinstance(value, dict):
            raise ValueError('Module metadata must be an object')
        if name.casefold() == 'content/cc2x/screen_extensions.json':
            validate_screen_descriptor(value)
        return
    if suffix in ('.png', '.mesh', '.txtr', '.fbx', '.dds', '.ogg', '.wav'):
        if suffix == '.txtr' and not data.startswith(b'TXTR'):
            raise ValueError('Invalid TXTR resource')
        return
    if suffix not in ('.lua', '.xml', '.txt', '.glsl', '.ini') or b'\0' in data:
        raise ValueError('Unknown package resource type')
    text = data.decode('utf-8-sig')
    if suffix == '.xml':
        if re.search(r'<!\s*(DOCTYPE|ENTITY)', text, re.IGNORECASE):
            raise ValueError('Unsafe XML declarations')
        try:
            ET.fromstring(text)
        except ET.ParseError as exc:
            raise ValueError('Invalid XML resource') from exc
    if suffix == '.lua':
        if luac is None:
            raise ValueError('Lua 5.3 syntax checker is required')
        flags = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
        version = subprocess.run([str(luac), '-v'], capture_output=True, timeout=5,
                                 check=False, creationflags=flags)
        if version.returncode or not re.search(rb'\bLua 5\.3(?:\.|\s)', version.stdout + version.stderr):
            raise ValueError('Lua syntax checker must be version 5.3')
        with tempfile.TemporaryDirectory(prefix='cc2x-package-lua-') as temporary:
            source = Path(temporary) / 'input.lua'
            source.write_bytes(data)
            parsed = subprocess.run([str(luac), '-p', str(source)], capture_output=True,
                                    timeout=5, check=False, creationflags=flags)
        if parsed.returncode:
            raise ValueError('Lua syntax validation failed')


def package(project, metadata, plugin, build_report, resources, destination, *, provenance, luac=None):
    source_project = project.absolute()
    project = project.resolve(strict=True)
    destination = destination.absolute()
    if destination.exists() or destination.is_symlink():
        raise ValueError('Existing output is preserved')
    parent = destination.parent.resolve(strict=True)
    target = parent / destination.name
    if target == project or project.is_relative_to(target) or target.is_relative_to(ROOT):
        raise ValueError('Package output must not replace SDK or project sources')
    if target.is_relative_to(project) and not target.is_relative_to(project / 'dist'):
        raise ValueError('Only project dist is a valid in-project output')
    if os.environ.get('APPDATA') and target.is_relative_to(Path(os.environ['APPDATA']).resolve()):
        raise ValueError('Package output must be outside application data')
    for ancestor in (parent, *parent.parents):
        if (ancestor / 'carrier_command.exe').exists() or (
                ancestor != project and (ancestor / 'mod.xml').exists()):
            raise ValueError('Package output must be outside game/mod installations')
    identity = metadata['id']
    if not re.fullmatch(r'[A-Za-z0-9_-]+(?:\.[A-Za-z0-9_-]+)*', identity):
        raise ValueError('Invalid stable package identity')
    output = {'LICENSE': regular(project, 'LICENSE')}
    if plugin is not None:
        filename = relative(metadata['plugin'])
        if '/' in filename or Path(filename).suffix.lower() != '.dll' or filename.casefold() == 'dinput8.dll':
            raise ValueError('Plugin filename must be a separate native DLL')
        plugin = plugin.absolute()
        if plugin.is_relative_to(source_project):
            source_name = plugin.relative_to(source_project).as_posix()
        elif plugin.is_relative_to(project):
            source_name = plugin.relative_to(project).as_posix()
        else:
            raise ValueError('Native artifact must be inside the source project')
        # Keep the lexical relative path for link checks; the root may expand a
        # Windows 8.3 spelling (RUNNER~1) during resolve(). Never resolve the DLL
        # itself first, which would hide source symlinks from regular().
        data = regular(project, source_name)
        if sha(data) != build_report['artifact_sha256'] or build_report['artifact'] != filename:
            raise ValueError('Native artifact mismatch')
        if build_report['sdk_header_sha256'] != sha((ROOT / 'include/cc2x.h').read_bytes()):
            raise ValueError('SDK header changed since native build')
        headers = build_report.get('sdk_headers_sha256')
        if headers is not None and headers != {
                path.name: sha(path.read_bytes()) for path in sorted((ROOT / 'include').glob('*.h'))}:
            raise ValueError('Public SDK headers changed since native build')
        source_hashes = build_report.get('source_sha256', {})
        if not source_hashes or any(sha(regular(project, name)) != expected
                                    for name, expected in source_hashes.items()):
            raise ValueError('Native source changed since build')
        if len(data) < 128 or data[:2] != b'MZ':
            raise ValueError('Native artifact is not a Windows DLL')
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        if (pe + 26 > len(data) or data[pe:pe + 4] != b'PE\0\0'
                or struct.unpack_from('<H', data, pe + 4)[0] != 0x14c
                or not struct.unpack_from('<H', data, pe + 22)[0] & 0x2000
                or struct.unpack_from('<H', data, pe + 24)[0] != 0x10b):
            raise ValueError('Native artifact must be PE32 x86 DLL')
        output['content/cc2x/' + filename] = data
        for notice in ('mingw-COPYING.txt', 'Zig-LICENSE.txt'):
            output['LICENSES/' + notice] = regular(ROOT, 'licenses/' + notice)
    elif build_report is not None:
        raise ValueError('Native build report without a native artifact')
    if (project / 'CREDITS_ET_LICENCE.md').is_file():
        output['CREDITS_ET_LICENCE.md'] = regular(project, 'CREDITS_ET_LICENCE.md')
    keys = {key.casefold() for key in output}
    total = sum(len(value) for value in output.values())
    if len(resources) > 19990:
        raise ValueError('Package entry limit exceeded')
    for name, content in resources.items():
        logical = relative(name)
        screen_resource = (logical == 'content/cc2x/screen_extensions.json' or
                           (logical.startswith('content/cc2x/ui/') and logical.endswith('.lua')))
        if (logical.casefold() in keys or (logical.lower().startswith('content/cc2x/') and not screen_resource)
                or not (logical.startswith('content/') or logical == 'thumbnail.png'
                        or logical.startswith('config_defaults/'))):
            raise ValueError('Reserved or duplicate resource path')
        if len(content) > 16 * 1024 * 1024 or total + len(content) > 256 * 1024 * 1024:
            raise ValueError('Package resource size limit exceeded')
        validate_resource(logical, content, luac)
        keys.add(logical.casefold())
        total += len(content)
        output[logical] = content
    if 'content/cc2x/screen_extensions.json' in output:
        descriptor = json.loads(output['content/cc2x/screen_extensions.json'].decode('utf-8-sig'))
        if any(name not in output for name in validate_screen_descriptor(descriptor)):
            raise ValueError('Screen extension source is missing from the package')
    xml = ET.Element('data', name=metadata['name'], author=metadata['author'],
                     description=metadata.get('description', ''), version=metadata['version'])
    declaration = ET.SubElement(xml, 'cc2mce', schema='1', type='mod', id=identity,
                                 sdk='cc2x', api=str(metadata.get('api', 4)))
    if metadata.get('lua_api'):
        declaration.set('lua_api', str(metadata['lua_api']))
    if metadata.get('framework_api'):
        declaration.set('framework_api', str(metadata['framework_api']))
    ET.SubElement(declaration, 'requires', id='cc2x.runtime')
    output['mod.xml'] = ET.tostring(xml, encoding='utf-8', xml_declaration=True)
    validate_resource('mod.xml', output['mod.xml'], None)
    report = {'owner': 'cc2x.mod.package', 'package_id': identity, 'version': metadata['version'],
              'mod_order': [identity], 'native_build': build_report, 'provenance': provenance,
              'packager_sha256': sha(Path(__file__).read_bytes()),
              'output_sha256': {name: sha(content) for name, content in sorted(output.items())},
              'complete_game_package': True, 'game_runtime_tested': False}
    output['BUILD_CHECK.json'] = (json.dumps(report, indent=2, sort_keys=True) + '\n').encode()
    with tempfile.TemporaryDirectory(prefix='.cc2x-mod-package-', dir=parent) as temporary:
        stage = Path(temporary) / 'package'
        stage.mkdir()
        for name, content in output.items():
            path = stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        if any(sha((stage / name).read_bytes()) != sha(content) for name, content in output.items()):
            raise ValueError('Staged package verification failed')
        stage.rename(target)
    return target
