# SPDX-License-Identifier: GPL-3.0-or-later
"""Build CC2X (dinput8.dll) and the example plugin with the bundled Zig toolchain.

    python build.py            -> dist/dinput8.dll, dist/cc2x_hello.dll
    python build.py --release  -> also release/CC2X-<version>.zip

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
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
ZIG = Path(os.environ.get('CC2X_ZIG', ROOT / '.deps' / 'zig-x86_64-windows-0.15.2' / 'zig.exe'))
DIST = ROOT / 'dist'
CFLAGS = ['cc', '-target', 'x86-windows-gnu', '-std=c11', '-O2', '-Wall', '-Wextra',
          '-Wno-unused-parameter', '-Werror=implicit-function-declaration', '-fno-strict-aliasing']


def zig(*args):
    if not ZIG.is_file():
        sys.exit('Zig toolchain missing: see docs/BUILD.md or pass --zig PATH')
    os.environ.setdefault('ZIG_GLOBAL_CACHE_DIR', str(ROOT / '.cache' / 'zig'))
    cmd = [str(ZIG), *CFLAGS, *[str(a) for a in args]]
    print(' '.join(Path(a).name if ('/' in a or chr(92) in a) else a for a in cmd[1:]), flush=True)
    subprocess.run(cmd, check=True)


def plugin(source, out, include=ROOT / 'include'):
    """Build a CC2X plugin DLL (usable by plugin projects: import build and call this)."""
    out.parent.mkdir(parents=True, exist_ok=True)
    zig('-shared', '-I', include, *([source] if isinstance(source, (str, Path)) else source), '-o', out)
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
    ap.add_argument('--zig', type=Path, help='Zig 0.15.2 executable (or set CC2X_ZIG)')
    args = ap.parse_args()
    global ZIG
    if args.zig:
        ZIG = args.zig.resolve()
    DIST.mkdir(exist_ok=True)
    zig('-shared', '-I', ROOT / 'include', ROOT / 'src/cc2x.c', ROOT / 'src/dinput8.def', '-o', DIST / 'dinput8.dll')
    plugin(ROOT / 'examples/hello_plugin/hello.c', DIST / 'cc2x_hello.dll')
    for junk in list(DIST.glob('*.lib')) + list(DIST.glob('*.pdb')):
        junk.unlink()
    if args.release:
        rel = ROOT / 'release'
        rel.mkdir(exist_ok=True)
        z = rel / ('CC2X-%s.zip' % version())
        with zipfile.ZipFile(z, 'w', zipfile.ZIP_DEFLATED) as f:
            f.write(DIST / 'dinput8.dll', 'dinput8.dll')
            f.write(ROOT / 'docs/README_PLAYERS.txt', 'CC2X-README.txt')
            for notice in sorted((ROOT / 'licenses').glob('*.txt')):
                f.write(notice, 'licenses/' + notice.name)
        print('wrote', z)


if __name__ == '__main__':
    main()
