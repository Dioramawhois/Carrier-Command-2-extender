# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('mod_package', ROOT / 'tools/mod_package.py')
module = importlib.util.module_from_spec(spec)


class PackageTest(unittest.TestCase):
    def setUp(self):
        spec.loader.exec_module(module)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.project = self.root / 'project'
        self.project.mkdir()
        (self.project / 'LICENSE').write_bytes(b'GPL fixture\n')
        (self.project / 'native.c').write_bytes(b'/* fixture */\n')
        self.plugin = self.project / 'plugin.dll'
        # File-copy fixture only; this executable-shaped data is never loaded.
        data = bytearray(256)
        data[:2] = b'MZ'
        struct.pack_into('<I', data, 0x3c, 64)
        data[64:68] = b'PE\0\0'
        struct.pack_into('<H', data, 68, 0x14c)
        struct.pack_into('<H', data, 86, 0x2000)
        struct.pack_into('<H', data, 88, 0x10b)
        self.plugin.write_bytes(data)
        self.sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
        self.report = {'artifact': 'plugin.dll', 'artifact_sha256': self.sha(self.plugin),
                       'sdk_header_sha256': self.sha(ROOT / 'include/cc2x.h'),
                       'source_sha256': {'native.c': self.sha(self.project / 'native.c')},
                       'game_runtime_tested': False}
        self.metadata = {'id': 'original_mod', 'name': 'Original Mod', 'author': 'Team',
                         'version': '1.1', 'api': 4, 'plugin': 'plugin.dll'}

    def package(self, resources=None, destination=None):
        return module.package(self.project, self.metadata, self.plugin, self.report,
                              resources or {'content/a.txt': b'own resource\n'},
                              destination or self.root / 'output', provenance={'synthetic': True})

    def test_complete_native_package_identity_provenance_and_no_sdk(self):
        folder = self.package()
        self.assertEqual((folder / 'content/cc2x/plugin.dll').read_bytes(), self.plugin.read_bytes())
        xml = (folder / 'mod.xml').read_text()
        self.assertIn('id="original_mod"', xml)
        self.assertIn('id="cc2x.runtime"', xml)
        self.assertFalse(list(folder.rglob('dinput8.dll')))
        report = json.loads((folder / 'BUILD_CHECK.json').read_text())
        self.assertTrue(report['complete_game_package'])
        self.assertFalse(report['game_runtime_tested'])

    def test_data_consumer_has_no_embedded_native_runtime(self):
        folder = module.package(self.project, self.metadata, None, None,
                                {'content/a.txt': b'own data\n'}, self.root / 'data-output',
                                provenance={'synthetic': True})
        self.assertFalse(list(folder.rglob('*.dll')))
        self.assertIsNone(json.loads((folder / 'BUILD_CHECK.json').read_text())['native_build'])

    def test_author_source_package_may_stage_under_its_own_dist(self):
        (self.project / 'mod.xml').write_bytes(b'<data name="source package"/>')
        (self.project / 'dist').mkdir()
        folder = self.package(destination=self.project / 'dist/output')
        self.assertTrue((folder / 'BUILD_CHECK.json').is_file())

    def test_modified_plugin_or_source_fails_before_writing(self):
        (self.project / 'native.c').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'source'):
            self.package()
        self.assertFalse((self.root / 'output').exists())

    def test_unreviewed_resource_types_and_traversal_are_rejected(self):
        for resource in ('../outside.txt', 'content/a.exe', 'content/cc2x/extra.dll'):
            with self.assertRaises(ValueError):
                self.package({resource: b'not allowed'})
            self.assertFalse((self.root / 'output').exists())

    def test_invalid_xml_fails_before_output(self):
        with self.assertRaises(ValueError):
            self.package({'content/game_objects/a.xml': b'<broken>'})
        self.assertFalse((self.root / 'output').exists())

    def test_existing_output_and_game_installation_are_preserved(self):
        output = self.package()
        with self.assertRaisesRegex(ValueError, 'Existing'):
            self.package(destination=output)
        game = self.root / 'game'
        game.mkdir()
        (game / 'carrier_command.exe').write_bytes(b'fixture')
        with self.assertRaises(ValueError):
            self.package(destination=game / 'mod')

    def test_lua_requires_a_real_syntax_checker(self):
        with self.assertRaisesRegex(ValueError, 'Lua'):
            self.package({'content/scripts/library_custom_5.lua': b'error("never execute")\n'})
        self.assertFalse((self.root / 'output').exists())


if __name__ == '__main__':
    unittest.main()
