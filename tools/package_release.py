# SPDX-License-Identifier: GPL-3.0-or-later
"""Stage a clean, tested SDK release. Does not publish, install or run the game."""
import argparse
from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PROVIDER_SHA256 = '1dd28806e97a809c8cce6e235046f98af8767ffe80180636bff6b86cf22e1d7c'
REPOSITORY = 'Dioramawhois/Carrier-Command-2-extender'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def checked_archive_names(data):
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        # ZipInfo.filename normalizes backslashes on Windows. Validate the
        # original central-directory spelling so the same archive is safe
        # when a developer opens it on another host as well.
        names = [entry.orig_filename for entry in archive.infolist()]
        if len(names) != len(set(names)):
            raise ValueError('Duplicate source archive path')
        for name in names:
            path = PurePosixPath(name)
            if '\\' in name or path.is_absolute() or '..' in path.parts or ':' in name:
                raise ValueError('Unsafe source archive path')
            if path.suffix.lower() in {'.dll', '.exe', '.pdb', '.obj', '.lib'}:
                raise ValueError('Executable/native artifact in source archive')
        return names


def license_payload(root):
    files = {'LICENSE': (root / 'LICENSE').read_bytes(),
             'NOTICE.md': (root / 'NOTICE.md').read_bytes(),
             'LICENSES/Lua-LICENSE.txt': (root / 'third_party/Lua-LICENSE.txt').read_bytes(),
             'LICENSES/NativeBridge-NOTICE.original.md':
                 (root / 'integrations/NativeBridge-NOTICE.original.md').read_bytes()}
    for name in ('mingw-COPYING.txt', 'Zig-LICENSE.txt', 'libcxx-LICENSE.txt',
                 'libcxxabi-LICENSE.txt', 'libunwind-LICENSE.txt'):
        files['LICENSES/' + name] = (root / 'licenses' / name).read_bytes()
    return files


def zip_bytes(files, stamp):
    output = io.BytesIO()
    date = datetime.fromtimestamp(stamp, timezone.utc).timetuple()[:6]
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            entry = zipfile.ZipInfo(name, date)
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, data)
    return output.getvalue()


def json_bytes(value):
    return (json.dumps(value, indent=2) + '\n').encode('utf-8')


def release_version(tag, *, allow_development=False):
    if re.fullmatch(r'\d+\.\d+\.\d+(?:-client-beta\.\d+)?', tag):
        return '-beta.' in tag
    if allow_development and re.fullmatch(r'\d+\.\d+\.\d+-dev', tag):
        return True
    raise ValueError('A concrete release version is required; development previews require an explicit flag')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--provider-sources', required=True, type=Path,
                        help='Hash-pinned Zig 0.15.2 provider source archive')
    parser.add_argument('--acceptance', type=Path, help='Separate exact-binary game acceptance JSON')
    parser.add_argument('--allow-development-version', action='store_true',
                        help='Publish the unchanged tested -dev runtime as an explicit prerelease')
    args = parser.parse_args()
    git = lambda *arguments: subprocess.check_output(['git', *arguments], cwd=ROOT)
    if git('status', '--porcelain').strip():
        raise ValueError('Release source checkout must be clean')
    commit = git('rev-parse', 'HEAD').decode().strip()
    tag = re.search(r'#define CC2X_VERSION "([^"]+)"', (ROOT / 'src/cc2x.c').read_text()).group(1)
    prerelease = release_version(tag, allow_development=args.allow_development_version)
    binary = (ROOT / 'dist/dinput8.dll').read_bytes()
    native = json.loads((ROOT / 'dist/dinput8.build.json').read_text())
    evidence = json.loads((ROOT / 'build/unified/test-results.json').read_text())
    if native['source_dirty'] or native['source_commit'] != commit or native['artifact_sha256'] != sha(binary):
        raise ValueError('Runtime does not correspond to the clean source commit')
    for name, expected in native['source_sha256'].items():
        if sha((ROOT / name).read_bytes()) != expected:
            raise ValueError('Runtime source input changed: ' + name)
    if (evidence.get('status') != 'passed' or evidence['artifact_sha256'] != sha(binary)
            or len(evidence['tests']) < 26 or not all(test['passed'] for test in evidence['tests'])):
        raise ValueError('Matching native regressions have not passed')
    for name, expected in evidence['test_source_sha256'].items():
        if sha((ROOT / name).read_bytes()) != expected:
            raise ValueError('Regression source changed: ' + name)
    provider = args.provider_sources.read_bytes()
    if sha(provider) != PROVIDER_SHA256:
        raise ValueError('Provider source archive hash mismatch')
    checked_archive_names(provider)
    stamp = int(git('show', '-s', '--format=%ct', 'HEAD'))
    archived = git('archive', '--format=zip', '--prefix=CC2X-source/', 'HEAD')
    checked_archive_names(archived)
    with zipfile.ZipFile(io.BytesIO(archived)) as archive:
        sources = {name: archive.read(name) for name in archive.namelist() if not name.endswith('/')}
    # Generated inputs are reproducible, and included so all build hashes can
    # also be checked directly without rebuilding first.
    for name, expected in native['source_sha256'].items():
        key = 'CC2X-source/' + name
        if key not in sources and name.startswith('build/unified/generated/'):
            sources[key] = (ROOT / name).read_bytes()
        if key not in sources or sha(sources[key]) != expected:
            raise ValueError('Source archive differs from the compiled input: ' + name)
    game = {'game_runtime_tested': False, 'verified_scope': []}
    if args.acceptance:
        game = json.loads(args.acceptance.read_text())
        if game.get('runtime_sha256') != sha(binary):
            raise ValueError('Acceptance belongs to another runtime')
    result = {'schema_version': 1, 'release_tag': tag, 'prerelease': prerelease,
              'development_preview': tag.endswith('-dev'),
              'runtime_version': tag, 'compiled_source_commit': commit,
              'source_url': f'https://github.com/{REPOSITORY}/tree/{commit}',
              'sdk_api': native['api'], 'lua_api': native['lua_api'],
              'framework_api': native['framework_api'], 'capabilities': native['capabilities'],
              'runtime_sha256': sha(binary), 'runtime_bytes': len(binary),
              'package_id': 'cc2x.runtime', 'steam_app_id': '1489630',
              'steam_workshop_item_id': '3833014670', 'gameplay_mods_bundled': False,
              'offline_verification': {'native_passed': len(evidence['tests'])},
              'runtime_acceptance': game, 'remote_multiplayer_verified': False,
              'dedicated_verified': False, 'macos_native_verified': False,
              'provider_sources_sha256': PROVIDER_SHA256}
    files = license_payload(ROOT)
    files.update({'dinput8.dll': binary, 'CC2X-README.txt': (ROOT / 'docs/README_PLAYERS.txt').read_bytes(),
                  'dinput8.build.json': json_bytes(native), 'SDK-OFFLINE-TESTS.json': json_bytes(evidence),
                  'RELEASE.json': json_bytes(result)})
    files['SHA256SUMS.txt'] = ''.join(sha(data) + '  ' + name + '\n'
                                        for name, data in sorted(files.items())).encode('ascii')
    assets = {'dinput8.dll': binary, 'CC2X-README.txt': files['CC2X-README.txt'],
              'dinput8.build.json': files['dinput8.build.json'],
              'SDK-OFFLINE-TESTS.json': files['SDK-OFFLINE-TESTS.json'],
              'RELEASE.json': files['RELEASE.json'],
              f'CC2X-SDK-{tag}-windows-x86.zip': zip_bytes(files, stamp),
              f'CC2X-SDK-{tag}-source.zip': zip_bytes(sources, stamp),
              f'CC2X-SDK-{tag}-toolchain-sources.zip': provider}
    assets['SHA256SUMS.txt'] = ''.join(sha(data) + '  ' + name + '\n'
                                         for name, data in sorted(assets.items())).encode('ascii')
    output = ROOT / 'release' / tag
    output.mkdir(parents=True, exist_ok=False)
    for name, data in assets.items():
        (output / name).write_bytes(data)
    (output / 'ASSETS.json').write_bytes(json_bytes({
        'tag': tag, 'source_commit': commit,
        'assets': {name: {'sha256': sha(data), 'bytes': len(data)} for name, data in assets.items()}}))
    print(json.dumps({'output': str(output), 'tag': tag, 'source_commit': commit,
                      'runtime_sha256': sha(binary), 'assets': len(assets)}))


if __name__ == '__main__':
    main()
