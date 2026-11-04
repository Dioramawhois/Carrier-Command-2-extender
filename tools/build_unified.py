# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the single integrated Windows x86 runtime. Never installs/loads it."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/unified'
GENERATED = OUT / 'generated'

CPP_SOURCES = [
    'api/framework.cpp',
    'runtime/host.cpp', 'runtime/world.cpp', 'runtime/mod_loader.cpp', 'runtime/early_declaration.cpp',
    'api/vehicle/definition_registry.cpp', 'api/vehicle/definition_adapter.cpp',
    'api/vehicle/ui_catalog.cpp', 'api/vehicle/lua_api.cpp', 'api/vehicle/ui_library_injection.cpp',
    'api/vehicle/control.cpp',
    'api/vehicle/buttons.cpp',
    'api/animation/animation_speed.cpp', 'api/carrier/hangar_spawn.cpp',
    'api/mods/workshop_upload.cpp',
    'api/ui/resource_registry.cpp', 'api/ui/weapon_catalog.cpp', 'api/ui/atlas_factory.cpp',
    'api/ui/second_atlas.cpp', 'native_extensions.cpp',
]
LIBS = ['-lbcrypt', '-lwindowscodecs', '-lole32', '-ladvapi32', '-luser32', '-luuid']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command):
    result = subprocess.run([str(x) for x in command], cwd=ROOT, text=True, capture_output=True)
    if result.returncode:
        log = OUT / 'compiler-error.txt'
        log.write_text(result.stdout + result.stderr, encoding='utf-8')
        print('\n'.join(line for line in result.stderr.splitlines() if ': error:' in line), flush=True)
        print('Full diagnostic: ' + str(log), flush=True)
        raise RuntimeError('Compiler failed: ' + ' '.join(str(x) for x in command[1:5]))
    if result.stderr.strip():
        print(result.stderr, flush=True)


def generate():
    GENERATED.mkdir(parents=True, exist_ok=True)
    replacements = [
        ('src/runtime/sdk_data.h.in', 'sdk_data.h', 'CC2_SDK_SOURCE', 'sdk/cc2.lua'),
        ('src/api/vehicle/ui_library_shim_source.h.in', 'ui_library_shim_source.h',
         'CC2X_UI_SHIM_SOURCE', 'src/api/vehicle/ui_library_shim.lua'),
    ]
    for template, destination, variable, source in replacements:
        text = (ROOT / template).read_text(encoding='utf-8')
        text = text.replace('@' + variable + '@', (ROOT / source).read_text(encoding='utf-8'))
        path = GENERATED / destination
        if not path.exists() or path.read_text(encoding='utf-8') != text:
            path.write_text(text, encoding='utf-8')
    header = (ROOT / 'include/cc2x.h').read_text()
    constants = sorted(set(re.findall(r'CC2X_(?:WORLD|VEHICLE|WAYPOINT|TILE|ISLAND|AIRFIELD|DOCK|FLIGHT|BODY|RAY)_[A-Z0-9_]+', header)))
    (GENERATED / 'world_constants.inc').write_text(''.join('CC2_WORLD_CONSTANT(' + name + ')\n' for name in constants))
    origin = json.loads((ROOT / 'integrations/waypoint-origin-profile.json').read_text())
    if origin['schema'] != 1 or origin['profile'] != '1.5.18-win32-r2' or len(origin['guards']) != 5:
        raise ValueError('Unsupported waypoint origin profile')
    guards = []
    for guard in origin['guards']:
        data = bytes.fromhex(guard['bytes'])
        if not 1 <= len(data) <= 64 or not 0x400000 <= guard['address'] < 0x1000000:
            raise ValueError('Unsafe waypoint origin signature')
        guards.append('{0x%x,%d,{%s}}' % (guard['address'], len(data), ','.join('0x%02x' % b for b in data)))
    (GENERATED / 'waypoint_origin_profile.h').write_text(
        'static const struct {uint32_t address,size; unsigned char bytes[64];} origin_guards[]={\n'
        + ',\n'.join(guards) + '\n};\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--zig', type=Path, required=True)
    parser.add_argument('--test', action='store_true')
    parser.add_argument('--output-directory', type=Path,
                        help='Separate candidate output; does not replace dist release files')
    args = parser.parse_args()
    compiler = args.zig.resolve(strict=True)
    if subprocess.check_output([str(compiler), 'version'], text=True).strip() != '0.15.2':
        raise ValueError('Pinned Zig 0.15.2 is required')
    os.environ.setdefault('ZIG_GLOBAL_CACHE_DIR', str(ROOT / '.cache/zig'))
    generate()
    headers = sorted([*(ROOT / 'src').rglob('*.h'), *(ROOT / 'include').glob('*.h'),
                      *(ROOT / 'third_party/lua').glob('*.h'), *GENERATED.glob('*')])
    header_hash = hashlib.sha256(''.join(sha(p) for p in headers).encode()).hexdigest()
    common = ['-target', 'x86-windows-gnu', '-O2', '-fno-strict-aliasing', '-fms-extensions',
              '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-U_WIN32_WINNT', '-D_WIN32_WINNT=0x0601',
              '-I', ROOT / 'include', '-I', ROOT / 'src', '-I', ROOT / 'third_party/lua', '-I', GENERATED]
    core = [ROOT / 'src' / name for name in ('cc2x.c', 'world.c', 'lua_world.c', 'entity_registry.c', 'order_context.c')]
    lua = [p for p in sorted((ROOT / 'third_party/lua').glob('*.c'))
           if p.name not in ('lua.c', 'luac.c', 'onelua.c', 'ltests.c')]
    runtime = [ROOT / 'src' / name for name in CPP_SOURCES]
    sources = core + lua + runtime + [ROOT / 'src/plugin.cpp', ROOT / 'src/platform/seh_guard.c']
    inputs = sources + headers + [ROOT / name for name in (
        'build.py', 'tools/build_unified.py', 'sdk/cc2.lua',
        'src/api/vehicle/ui_library_shim.lua', 'src/dinput8.def',
        'src/runtime/sdk_data.h.in', 'src/api/vehicle/ui_library_shim_source.h.in',
        'integrations/native-bridge-source-manifest.json', 'integrations/cc2mods-source-lock.json',
        'integrations/waypoint-origin-profile.json',
        'sdk/schemas/framework-capabilities.json', 'sdk/schemas/screen_extensions.schema.json', 'NOTICE.md',
        'LICENSE', 'third_party/Lua-LICENSE.txt')]
    input_hashes = {p.relative_to(ROOT).as_posix(): sha(p) for p in inputs}
    compiler_hash = sha(compiler)
    def compile_source(path):
        relative = path.relative_to(ROOT).as_posix()
        output = OUT / 'objects' / (relative.replace('/', '_') + '.obj')
        output.parent.mkdir(parents=True, exist_ok=True)
        is_cpp = path.suffix == '.cpp'
        flags = [*common, '-std=c++17' if is_cpp else '-std=c11']
        if path in core:
            flags += ['-DCC2X_NATIVE_BRIDGE_BUILTIN']
        if path.name == 'seh_guard.c':
            flags = ['-target', 'x86-windows-msvc', '-O2', '-std=c11', '-nostdlib', '-nostdinc',
                     '-ffreestanding', '-fno-stack-protector', '-fms-extensions']
        # Native fixtures can include production .c/.cpp directly. Invalidate all
        # test objects on production changes, not only a named special case.
        is_test = path.is_relative_to(ROOT / 'tests')
        if is_test:
            flags += ['-UNDEBUG']  # Zig's optimized C modes otherwise erase assert().
        included_source = ''.join(sha(p) for p in core + runtime) if is_test else ''
        key = hashlib.sha256((sha(path) + included_source + header_hash + compiler_hash + str(flags)).encode()).hexdigest()
        stamp = output.with_suffix('.key')
        if not output.exists() or not stamp.exists() or stamp.read_text() != key:
            run([compiler, 'c++' if is_cpp else 'cc', *flags, '-c', path, '-o', output])
            stamp.write_text(key)
        return output
    with ThreadPoolExecutor(max_workers=4) as pool:
        objects = list(pool.map(compile_source, sources))
    output = (args.output_directory or ROOT / 'dist') / 'dinput8.dll'
    output.parent.mkdir(parents=True, exist_ok=True)
    print('Linking unified CC2X runtime', flush=True)
    run([compiler, 'c++', *common, '-shared', *objects, ROOT / 'src/dinput8.def', *LIBS, '-o', output])
    if input_hashes != {p.relative_to(ROOT).as_posix(): sha(p) for p in inputs} or compiler_hash != sha(compiler):
        raise ValueError('Build inputs changed during compilation; provenance is not certified')
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).strip())
    report = {'runtime': 'unified CC2X', 'api': 8, 'artifact': output.name,
              'artifact_sha256': sha(output), 'compiler_sha256': compiler_hash,
              'source_commit': revision, 'source_dirty': dirty,
              'capabilities': ['independent-lua-v1', 'ordered-module-bundles-v1', 'session-query-v1',
                               'lua-mod-api-v2', 'framework-c-api-v1', 'waypoint-origin-v1',
                               'screen-extensions-v1'],
              'lua_api': 2,
              'framework_api': 1,
              'source_sha256': input_hashes,
              'game_runtime_tested': False, 'gameplay_mods_bundled': False}
    (output.parent / 'dinput8.build.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'built': str(output), 'sha256': sha(output)}), flush=True)
    if args.test:
        (OUT / 'test-results.json').write_text(json.dumps({
            'status': 'running', 'artifact_sha256': sha(output),
            'game_runtime_tested': False}, indent=2) + '\n')
        by_source = dict(zip(sources, objects, strict=True))
        lua_objects = [by_source[p] for p in lua]
        def src_objects(names):
            return [by_source[ROOT / 'src' / name] for name in names]
        registry = ['api/vehicle/definition_registry.cpp', 'api/vehicle/ui_catalog.cpp', 'api/ui/resource_registry.cpp']
        tests = [
            ('cc2x_api_layout_test.c', [], []),
            ('framework_api_layout_test.c', [], []),
            ('lifetime_policy_test.cpp', [], []),
            ('module_mailbox_test.cpp', [], []),
            ('vehicle_control_test.cpp', src_objects(['api/vehicle/control.cpp']), []),
            ('vehicle_buttons_test.cpp', src_objects(['api/vehicle/buttons.cpp', 'platform/seh_guard.c']), []),
            ('pe_relocation_guard_test.cpp', [], []),
            ('second_atlas_contract_test.cpp', [], []),
            ('resource_registry_test.cpp', src_objects(['api/ui/resource_registry.cpp']), []),
            ('weapon_ui_catalog_test.cpp', src_objects(['api/ui/resource_registry.cpp', 'api/ui/weapon_catalog.cpp']), []),
            ('definition_registry_test.cpp', src_objects(registry), []),
            ('definition_adapter_hooks_test.cpp', src_objects([
                'api/vehicle/definition_adapter.cpp', 'api/vehicle/definition_registry.cpp',
                'platform/seh_guard.c']), []),
            ('entity_registry_test.c', src_objects(['entity_registry.c']), []),
            ('mod_loader_test.cpp', src_objects(['runtime/mod_loader.cpp']) + lua_objects, []),
            ('screen_extensions_test.cpp', src_objects(['runtime/mod_loader.cpp']) + lua_objects, []),
            ('screen_extension_hook_test.cpp', src_objects(registry + [
                'runtime/mod_loader.cpp', 'api/vehicle/ui_library_injection.cpp']) + lua_objects, []),
            ('early_declaration_test.cpp', src_objects(registry + ['runtime/mod_loader.cpp', 'runtime/early_declaration.cpp']) + lua_objects, []),
            ('vehicle_ui_lua_api_test.cpp', src_objects(registry + ['api/vehicle/lua_api.cpp', 'api/ui/weapon_catalog.cpp']) + lua_objects, []),
            ('vehicle_spawn_sdk_test.cpp', lua_objects, [ROOT / 'sdk/cc2.lua']),
            ('lua_policy_fixture.cpp', lua_objects, []),
            ('vehicle_ui_library_shim_test.cpp', lua_objects, [ROOT / 'src/api/vehicle/ui_library_shim.lua']),
            ('runtime_integration_test.cpp', src_objects(CPP_SOURCES + ['entity_registry.c', 'order_context.c', 'platform/seh_guard.c']) + lua_objects, []),
            ('framework_parity_test.cpp', src_objects(CPP_SOURCES + ['entity_registry.c', 'order_context.c', 'platform/seh_guard.c']) + lua_objects, []),
            ('framework_buttons_parity_test.cpp', src_objects([p for p in CPP_SOURCES if p != 'api/vehicle/buttons.cpp'] + ['entity_registry.c', 'order_context.c', 'platform/seh_guard.c']) + lua_objects, []),
            ('cc2x_core_test.c', src_objects(['world.c', 'lua_world.c', 'entity_registry.c', 'order_context.c']), []),
        ]
        results = []
        test_hashes = {}
        for filename, extra, arguments in tests:
            test_source = ROOT / 'tests/bridge' / filename
            test_hashes[test_source.relative_to(ROOT).as_posix()] = sha(test_source)
            print('TEST ' + filename, flush=True)
            obj = compile_source(test_source)
            executable = OUT / 'tests' / (test_source.stem + '.exe')
            executable.parent.mkdir(exist_ok=True)
            run([compiler, 'c++', *common, obj, *extra, *LIBS, '-o', executable])
            completed = subprocess.run([str(executable), *map(str, arguments)], cwd=ROOT,
                                       text=True, capture_output=True, timeout=45)
            (OUT / 'tests' / (test_source.stem + '.log')).write_text(completed.stdout + completed.stderr, encoding='utf-8')
            results.append({'name': test_source.stem, 'passed': completed.returncode == 0})
            print(completed.stdout[-1000:] + completed.stderr[-1000:], flush=True)
            if completed.returncode:
                raise RuntimeError('SDK regression failed: ' + test_source.stem)
        for filename, extra in [('session_api_test.c', []), ('session_queue_test.c', []),
                                ('order_context_test.c', src_objects(['order_context.c'])),
                                ('seh_guard_test.c', src_objects(['platform/seh_guard.c']))]:
            test_source = ROOT / 'tests' / filename
            test_hashes[test_source.relative_to(ROOT).as_posix()] = sha(test_source)
            obj = compile_source(test_source)
            executable = OUT / 'tests' / (test_source.stem + '.exe')
            run([compiler, 'cc', *common, obj, *extra, '-o', executable])
            completed = subprocess.run([str(executable)], cwd=ROOT, text=True, capture_output=True, timeout=15)
            print(completed.stdout + completed.stderr, flush=True)
            if completed.returncode:
                raise RuntimeError('Regression failed: ' + filename)
            results.append({'name': test_source.stem, 'passed': True})
        if test_hashes != {name: sha(ROOT / name) for name in test_hashes}:
            raise ValueError('Regression sources changed during verification')
        (OUT / 'test-results.json').write_text(json.dumps({
            'status': 'passed', 'test_source_sha256': test_hashes,
            'tests': results, 'artifact_sha256': sha(output),
            'source_commit': revision, 'source_dirty': dirty,
            'game_runtime_tested': False}, indent=2) + '\n')


if __name__ == '__main__':
    main()
