# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only checks of the production artifact; never loads the DLL."""
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]

class DistributionTest(unittest.TestCase):
    def test_x86_dll_exports_proxy_and_named_session_extension(self):
        binary = (ROOT / 'dist/dinput8.dll').read_bytes()
        pe = struct.unpack_from('<I', binary, 0x3c)[0]
        self.assertEqual(binary[pe:pe + 4], b'PE\0\0')
        machine, count, _, _, _, optional_size, flags = struct.unpack_from('<HHIIIHH', binary, pe + 4)
        self.assertEqual(machine, 0x14c)
        self.assertTrue(flags & 0x2000)
        optional = pe + 24
        self.assertEqual(struct.unpack_from('<H', binary, optional)[0], 0x10b)
        sections = []
        for index in range(count):
            offset = optional + optional_size + index * 40
            virtual_size, rva, raw_size, raw = struct.unpack_from('<IIII', binary, offset + 8)
            sections.append((rva, max(virtual_size, raw_size), raw))
        def raw_offset(address):
            for rva, size, raw in sections:
                if rva <= address < rva + size:
                    return raw + address - rva
            raise AssertionError('unmapped PE RVA')
        export_rva = struct.unpack_from('<I', binary, optional + 96)[0]
        table = raw_offset(export_rva)
        names_count, _, names_rva = struct.unpack_from('<III', binary, table + 24)
        names = set()
        for index in range(names_count):
            address = struct.unpack_from('<I', binary, raw_offset(names_rva) + index * 4)[0]
            start = raw_offset(address)
            names.add(binary[start:binary.index(b'\0', start)].decode('ascii'))
        self.assertTrue({'DirectInput8Create', 'DllCanUnloadNow', 'DllGetClassObject',
                         'DllRegisterServer', 'DllUnregisterServer',
                         'cc2x_query_session_v1'} <= names)

    def test_runtime_does_not_embed_aircraft_scenarios_or_research_activation(self):
        binary = (ROOT / 'dist/dinput8.dll').read_bytes()
        for value in (b'cc2x_f16', b'cc2x_f15', b'custom_entity_registration_probe.enabled',
                      b'MOD_COMPATIBILITY_AUTO_DISABLE_BYPASSED'):
            self.assertFalse(value in binary, value.decode())

if __name__ == '__main__':
    unittest.main()
