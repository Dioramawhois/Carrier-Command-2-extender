"""Regress the Lua-state ABI changed by the November security hotfix."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class Steam26732430Tests(unittest.TestCase):
    def test_exact_new_executable_is_authenticated(self):
        source = (ROOT / 'src/cc2x.c').read_text(encoding='utf-8')
        self.assertIn('"1.5.20-win32", 0x6afc5c17, 0x00af6000', source)
        self.assertIn('1df609c8252fc0245decfab793e3b8e1884e553539a8831cbee67663b6725920', source)

    def test_hook_replays_complete_instructions_after_security_cleanup(self):
        source = (ROOT / 'src/cc2x.c').read_text(encoding='utf-8')
        # PUSH imm8 followed by MOV [ebp-0x9c], imm32 is twelve bytes.
        self.assertIn('P->lua_site_size', source)
        self.assertRegex(source, r'0x00446633,\s*12,\s*1,')
        self.assertIn('profile->lua_state_esi ? cpu->esi : cpu->edi', source)

    def test_sdk_does_not_reintroduce_removed_game_lua_loaders(self):
        source = (ROOT / 'src/cc2x.c').read_text(encoding='utf-8')
        # The independent, sandboxed VM has its own loader. Game-owned Lua
        # must retain the hotfix's removal of these globals.
        for name in ('dofile', 'loadfile', 'loadstring'):
            self.assertNotRegex(source, rf'api_lua_add\([^;]*"{name}"')


if __name__ == '__main__':
    unittest.main()
