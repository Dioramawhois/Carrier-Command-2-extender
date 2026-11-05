# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import importlib.util
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('screen_packager',ROOT/'tools/mod_package.py')
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class ScreenSourcePackageTest(unittest.TestCase):
    def setUp(self):
        self.value={'schema':2,'extensions':[{'screen':'scripts/map.lua','source':'cc2x/ui/map.lua',
            'profiles':[{'sha256':'a'*64,'edits':[{'offset':0,'remove':0,'insert':'owned()\n'}]}]}]}

    def test_own_descriptor_needs_no_original_screen_content(self):
        self.assertEqual(module.validate_screen_descriptor(self.value),['content/cc2x/ui/map.lua'])

    def test_unsafe_or_ambiguous_edit_is_rejected(self):
        for edit in [{'offset':True,'remove':0,'insert':''},
                     {'offset':-1,'remove':0,'insert':''},
                     {'offset':0,'remove':2*1024*1024+1,'insert':''},
                     {'offset':0,'remove':0,'insert':'\0'}]:
            value=copy.deepcopy(self.value)
            value['extensions'][0]['profiles'][0]['edits']=[edit]
            with self.assertRaises(ValueError):module.validate_screen_descriptor(value)
        self.value['extensions'][0]['profiles'][0]['edits']*=2
        with self.assertRaises(ValueError):module.validate_screen_descriptor(self.value)

    def test_schema_one_and_unsafe_source_cannot_enable_edits(self):
        self.value['schema']=1
        with self.assertRaises(ValueError):module.validate_screen_descriptor(self.value)
        self.value['schema']=2
        self.value['extensions'][0]['source']='cc2x/ui/../elsewhere.lua'
        with self.assertRaises(ValueError):module.validate_screen_descriptor(self.value)
