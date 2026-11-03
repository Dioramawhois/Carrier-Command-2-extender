# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('lua_mod_package', ROOT / 'tools/package_lua_mod.py')


class LuaPackageTest(unittest.TestCase):
    def setUp(self):
        self.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.project = Path(self.temp.name)
        (self.project / 'content/bridge').mkdir(parents=True)
        (self.project / 'content/bridge.json').write_text('{"id":"example","scripts":["bridge/main.lua"]}')
        (self.project / 'content/bridge/main.lua').write_text('return {}')

    def test_collects_just_lua_content_not_source_or_private_dll(self):
        (self.project / 'native').mkdir()
        (self.project / 'native/reference.c').write_text('not a player resource')
        result, sources = self.module.collect(self.project, {'resources': {}})
        self.assertEqual(set(result), {'content/bridge.json', 'content/bridge/main.lua'})
        self.assertEqual(len(sources), 2)

    def test_native_payload_refused_before_staging(self):
        for name in ('payload.dll', 'dinput8.dll', 'tool.exe', 'source.c'):
            with self.subTest(name=name):
                path = self.project / 'content' / name
                path.write_bytes(b'untrusted')
                with self.assertRaises(ValueError):
                    self.module.collect(self.project, {'resources': {}})
                path.unlink()

    def test_explicit_native_declaration_refused(self):
        with self.assertRaises(ValueError):
            self.module.collect(self.project, {'plugin': 'old.dll', 'resources': {}})

    def test_manifest_is_required(self):
        (self.project / 'content/bridge.json').unlink()
        with self.assertRaises(ValueError):
            self.module.collect(self.project, {'resources': {}})


if __name__ == '__main__':
    unittest.main()
