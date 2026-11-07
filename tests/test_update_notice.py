import importlib.util
import json
import os
from pathlib import Path
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('update_notice',ROOT/'tools/update_notice.py')
notice=importlib.util.module_from_spec(spec)
spec.loader.exec_module(notice)


class UpdateNoticeTests(unittest.TestCase):
    def test_payload_is_own_additive_screen_extension(self):
        files=notice.payload('0.11.0-dev')
        self.assertEqual(len(files),2)
        self.assertEqual(json.loads(files['content/cc2x/screen_extensions.json'])['schema'],1)
        self.assertNotIn('content/scripts/screen_menu_main.lua',files)

    def test_version_cannot_inject_lua(self):
        for version in ('1.0.0";os.execute()', '', '../bad'):
            with self.assertRaises(ValueError):notice.payload(version)

    def test_real_lua_old_current_newer_unknown_and_dismissal(self):
        runner=os.environ.get('CC2X_LUA_POLICY_RUNNER') or ROOT/'build/unified/tests/lua_policy_fixture.exe'
        if not Path(runner).is_file():self.skipTest('Build the owned native Lua policy runner first')
        result=subprocess.run([str(runner),str(ROOT/'sdk/ui/update_notice.lua'),str(ROOT/'tests/bridge/update_notice.lua')],
                              capture_output=True,text=True,timeout=20)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('6 real-Lua scenarios',result.stdout)
