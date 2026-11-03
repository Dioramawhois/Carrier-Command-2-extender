# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import io
from pathlib import Path
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('package_release', ROOT / 'tools/package_release.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ReleasePackageTest(unittest.TestCase):
    def test_runtime_payload_preserves_all_linked_library_notices(self):
        files = module.license_payload(ROOT)
        for name in ('mingw-COPYING.txt', 'Zig-LICENSE.txt', 'libcxx-LICENSE.txt',
                     'libcxxabi-LICENSE.txt', 'libunwind-LICENSE.txt'):
            self.assertEqual(files['LICENSES/' + name], (ROOT / 'licenses' / name).read_bytes())
        self.assertIn('LICENSES/Lua-LICENSE.txt', files)
        self.assertIn('LICENSES/NativeBridge-NOTICE.original.md', files)

    def test_source_archives_reject_traversal_and_native_payloads(self):
        for name in ('../secret', '/absolute', 'C:/absolute', 'nested\\bad', 'runtime.dll', 'build/tool.exe'):
            with self.subTest(name=name):
                data = io.BytesIO()
                with zipfile.ZipFile(data, 'w') as archive:
                    archive.writestr(name.replace('\\', '/'), b'fixture')
                payload = data.getvalue()
                if '\\' in name:
                    # Windows' ZIP writer canonicalizes its own separators.
                    # Build the actual foreign ZIP spelling being validated.
                    payload = payload.replace(name.replace('\\', '/').encode(), name.encode())
                with self.assertRaises(ValueError):
                    module.checked_archive_names(payload)

    def test_zip_is_repeatable_and_keeps_source_bytes(self):
        files = {'source.c': b'owned source\r\n', 'LICENSE': b'license\n'}
        first = module.zip_bytes(files, 1793664000)
        self.assertEqual(first, module.zip_bytes(files, 1793664000))
        with zipfile.ZipFile(io.BytesIO(first)) as archive:
            self.assertEqual(archive.read('source.c'), files['source.c'])
