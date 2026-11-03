# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('extractor', ROOT / 'tools/extract_cc2mods.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ExtractionTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'upstream'
        self.source.mkdir()
        self.payload = b'/* GPL source */\n'
        (self.source / 'plugin.c').write_bytes(self.payload)
        self.lock = {'upstream': 'fixture', 'commit': 'fixture', 'projects': [{
            'directory': 'separate-mod', 'name': 'Fixture', 'game_folder': 'original-mod',
            'files': {'native/plugin.c': {'source': 'plugin.c', 'sha256': hashlib.sha256(self.payload).hexdigest()}},
        }]}

    def test_modified_upstream_is_rejected_before_output_creation(self):
        (self.source / 'plugin.c').write_bytes(b'modified')
        with self.assertRaisesRegex(ValueError, 'hash mismatch'):
            module.extract(self.source, self.root / 'output', self.lock)
        self.assertFalse((self.root / 'output').exists())
        self.assertFalse(list(self.root.glob('.cc2mods-extract-*')))

    def test_existing_output_is_preserved(self):
        output = self.root / 'output'
        output.mkdir()
        (output / 'keep').write_text('unchanged')
        with self.assertRaisesRegex(ValueError, 'preserved'):
            module.extract(self.source, output, self.lock)
        self.assertEqual((output / 'keep').read_text(), 'unchanged')

    def test_valid_copy_preserves_bytes_and_reports_source_only(self):
        output = module.extract(self.source, self.root / 'output', self.lock)
        self.assertEqual((output / 'separate-mod/native/plugin.c').read_bytes(), self.payload)
        self.assertEqual((output / 'separate-mod/.gitattributes').read_bytes(), b'* -text\n')
        self.assertIn('source-only', (output / 'separate-mod/SOURCE_MANIFEST.json').read_text())
        self.assertEqual((self.source / 'plugin.c').read_bytes(), self.payload)

    def test_unsafe_target_is_rejected_before_writing(self):
        entry = self.lock['projects'][0]['files'].pop('native/plugin.c')
        self.lock['projects'][0]['files']['../../outside.c'] = entry
        with self.assertRaisesRegex(ValueError, 'Unsafe destination'):
            module.extract(self.source, self.root / 'output', self.lock)
        self.assertFalse((self.root / 'output').exists())


if __name__ == '__main__':
    unittest.main()
