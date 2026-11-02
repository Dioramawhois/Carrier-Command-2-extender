# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('aircraft_extractor', ROOT / 'tools/extract_aircraft.py')
module = importlib.util.module_from_spec(spec)


class ExtractionTest(unittest.TestCase):
    def setUp(self):
        spec.loader.exec_module(module)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'source'
        self.source.mkdir()
        self.payload = b'<data name="original"/>\n'
        (self.source / 'mod.xml').write_bytes(self.payload)
        self.lock = {'origin': 'synthetic fixture', 'owner_id': 'original.owner',
                     'files': {'mod.xml': {'source': 'mod.xml',
                               'sha256': hashlib.sha256(self.payload).hexdigest()}}}

    def test_bytes_identity_and_no_bundled_sdk(self):
        output = self.root / 'output'
        module.extract(self.source, output, self.lock)
        self.assertEqual((output / 'mod.xml').read_bytes(), self.payload)
        self.assertFalse(list(output.rglob('*.dll')))
        self.assertFalse((output / 'sdk').exists())
        self.assertIn('original.owner', (output / 'cc2x-sdk.json').read_text())

    def test_changed_source_does_not_create_output(self):
        (self.source / 'mod.xml').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'hash mismatch'):
            module.extract(self.source, self.root / 'output', self.lock)
        self.assertFalse((self.root / 'output').exists())

    def test_traversal_and_executable_are_rejected(self):
        original = self.lock['files']['mod.xml']
        for target in ('../outside', 'content/cc2x/plugin.dll'):
            self.lock['files'] = {target: original}
            with self.assertRaises(ValueError):
                module.extract(self.source, self.root / 'output', self.lock)
            self.assertFalse((self.root / 'output').exists())

    def test_existing_destination_is_preserved(self):
        with self.assertRaisesRegex(ValueError, 'Existing output'):
            module.extract(self.source, self.source, self.lock)
        self.assertEqual((self.source / 'mod.xml').read_bytes(), self.payload)


if __name__ == '__main__':
    unittest.main()
