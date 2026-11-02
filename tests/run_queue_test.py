# SPDX-License-Identifier: GPL-3.0-or-later
"""Run only CC2X's own address-free queue regression; no plugin/game execution."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--zig', type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='cc2x-queue-') as folder:
        for name in ('session_queue_test', 'session_api_test'):
            output = Path(folder) / (name + '.exe')
            subprocess.run([str(args.zig.resolve()), 'cc', '-std=c11', '-Wall', '-Wextra',
                            '-Werror', '-I', str(ROOT / 'include'),
                            str(ROOT / 'tests' / (name + '.c')), '-o', str(output)], check=True)
            subprocess.run([str(output)], check=True, timeout=10)
