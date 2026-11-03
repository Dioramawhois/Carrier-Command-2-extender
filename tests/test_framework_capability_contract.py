# SPDX-License-Identifier: GPL-3.0-or-later
"""Keep new public operations visible in the parity contract; behavior is native-tested."""
import fnmatch
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CapabilityContractTest(unittest.TestCase):
    def test_all_named_c_operations_have_a_documented_lua_route(self):
        contract = json.loads((ROOT / 'sdk/schemas/framework-capabilities.json').read_text())
        header = (ROOT / 'include/cc2x_framework.h').read_text()
        body = header.split('typedef struct cc2x_framework_api {')[1].split('} cc2x_framework_api;')[0]
        operations = re.findall(r'\(\*(\w+)\)', body)
        groups = [*contract['shared'], contract['native_binding_provider']]
        classified = [name for group in groups for name in group.get('c', [])]
        self.assertEqual(set(operations), set(classified))
        self.assertEqual(len(classified), len(set(classified)), 'Duplicate capability ownership')
        for group in groups:
            self.assertTrue(group['lua'])
        self.assertEqual(contract['framework_api'], 1)
        self.assertEqual(contract['lua_api'], 2)

    def test_facade_functions_are_classified_and_remaining_gates_are_explicit(self):
        contract = json.loads((ROOT / 'sdk/schemas/framework-capabilities.json').read_text())
        facade = (ROOT / 'sdk/cc2.lua').read_text()
        functions = re.findall(r'function M\.([\w.]+)\(', facade)
        patterns = [pattern for group in contract['shared'] for pattern in group['lua']]
        self.assertEqual([], [name for name in functions
                              if not any(fnmatch.fnmatchcase(name, pattern) for pattern in patterns)])
        self.assertEqual(contract['validation'], 'real-lua-with-synthetic-engine')
        self.assertTrue(contract['remaining_migrations'])
