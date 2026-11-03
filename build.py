# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the integrated CC2X runtime and optional example plugins with Zig 0.15.2.

    python build.py            -> dist/dinput8.dll (C core + Lua host + generic registries)
    python build.py --test     -> unified runtime and SDK regression executables
    python build.py --examples -> also cc2x_hello.dll and cc2x_selftest.dll
    python build.py --release  -> also release/<version>/: dinput8.dll, CC2X-README.txt, SHA256SUMS.txt

The toolchain (Zig 0.15.2, used as a C compiler for 32-bit Windows) lives in .deps.
If it is missing, download https://ziglang.org/download/0.15.2/zig-x86_64-windows-0.15.2.zip
and unpack it there (sha256 3a0ed1e8799a2f8ce2a6e6290a9ff22e6906f8227865911fb7ddedc3cc14cb0c).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent
ZIG = Path(os.environ.get('CC2X_ZIG', ROOT / '.deps' / 'zig-x86_64-windows-0.15.2' / 'zig.exe'))
DIST = ROOT / 'dist'
CFLAGS = ['cc', '-target', 'x86-windows-gnu', '-std=c11', '-O2', '-Wall', '-Wextra',
          '-Wno-unused-parameter', '-Werror=implicit-function-declaration', '-fno-strict-aliasing']


def zig(*args, cpp=False):
    if not ZIG.is_file():
        sys.exit('Zig toolchain missing: see docs/GUIDE.md section 11.1 or pass --zig PATH')
    os.environ.setdefault('ZIG_GLOBAL_CACHE_DIR', str(ROOT / '.cache' / 'zig'))
    flags = CFLAGS
    if cpp:
        flags = ['c++', '-std=c++17', *[flag for flag in CFLAGS[1:]
                 if flag not in ('-std=c11', '-Werror=implicit-function-declaration')]]
    cmd = [str(ZIG), *flags, *[str(a) for a in args]]
    print(' '.join(Path(a).name if ('/' in a or chr(92) in a) else a for a in cmd[1:]), flush=True)
    subprocess.run(cmd, check=True)


def plugin(source, out, include=ROOT / 'include'):
    """Build a CC2X plugin DLL (usable by plugin projects: import build and call this)."""
    out.parent.mkdir(parents=True, exist_ok=True)
    sources = [source] if isinstance(source, (str, Path)) else source
    cpp = any(Path(item).suffix.lower() in ('.cpp', '.cc', '.cxx') for item in sources)
    zig('-shared', '-I', include, *sources, '-o', out, cpp=cpp)
    stems = {out.stem} | {Path(s).stem for s in ([source] if isinstance(source, (str, Path)) else source)}
    for stem in stems:
        for ext in ('.lib', '.pdb'):
            junk = out.parent / (stem + ext)
            if junk.exists():
                junk.unlink()


def version():
    return re.search(r'#define CC2X_VERSION "([^"]+)"', (ROOT / 'src/cc2x.c').read_text()).group(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--release', action='store_true')
    ap.add_argument('--test', action='store_true', help='Run native SDK regressions, never the game')
    ap.add_argument('--examples', action='store_true')
    ap.add_argument('--zig', type=Path, help='Zig 0.15.2 executable (or set CC2X_ZIG)')
    args = ap.parse_args()
    global ZIG
    if args.zig:
        ZIG = args.zig.resolve()
    DIST.mkdir(exist_ok=True)
    subprocess.run([sys.executable, str(ROOT / 'tools/build_unified.py'), '--zig', str(ZIG),
                    *(['--test'] if args.test else [])], check=True)
    if args.examples:
        plugin(ROOT / 'examples/hello_plugin/hello.c', DIST / 'cc2x_hello.dll')
        plugin(ROOT / 'tests/selftest/selftest.c', DIST / 'cc2x_selftest.dll')
    for junk in list(DIST.glob('*.lib')) + list(DIST.glob('*.pdb')):
        junk.unlink()
    if args.release:
        # players download the DLL itself (no archive, nothing to run) plus the readme
        rel = ROOT / 'release' / version()
        rel.mkdir(parents=True, exist_ok=True)
        shutil.copy2(DIST / 'dinput8.dll', rel / 'dinput8.dll')
        shutil.copy2(ROOT / 'docs/README_PLAYERS.txt', rel / 'CC2X-README.txt')
        shutil.copy2(ROOT / 'LICENSE', rel / 'LICENSE')
        shutil.copy2(ROOT / 'NOTICE.md', rel / 'NOTICE.md')
        shutil.copy2(ROOT / 'third_party/Lua-LICENSE.txt', rel / 'Lua-LICENSE.txt')
        notices = rel / 'LICENSES'
        notices.mkdir(exist_ok=True)
        for notice in (ROOT / 'licenses').glob('*.txt'):
            shutil.copy2(notice, notices / notice.name)
        shutil.copy2(ROOT / 'third_party/Lua-LICENSE.txt', notices / 'Lua-LICENSE.txt')
        shutil.copy2(ROOT / 'integrations/NativeBridge-NOTICE.original.md', notices / 'NativeBridge-NOTICE.original.md')
        shutil.copy2(DIST / 'dinput8.build.json', rel / 'dinput8.build.json')
        sha = hashlib.sha256((rel / 'dinput8.dll').read_bytes()).hexdigest()
        (rel / 'SHA256SUMS.txt').write_text('%s  dinput8.dll\n' % sha, encoding='ascii')
        print('release files in', rel)


if __name__ == '__main__':
    main()
