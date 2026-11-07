import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('surface_profile', ROOT/'tools/verify_surface_navigation_profile.py')
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


class SurfaceNavigationProfileTests(unittest.TestCase):
    def image(self):
        data = {a: bytes.fromhex(b) for a, b in profile.GUARDS.items()}
        data[profile.VTABLE+0x34] = struct.pack('<I', 0x708290)
        return data

    def test_matched_abi_projection_gate_and_completion(self):
        data = self.image()
        self.assertEqual(len(profile.check_branches(lambda a, n: data[a][:n])), len(profile.GUARDS))

    def test_every_guard_byte_rejects_mutation(self):
        for address, expected in profile.GUARDS.items():
            for offset in range(len(bytes.fromhex(expected))):
                with self.subTest(address=address, offset=offset):
                    data = self.image()
                    value = bytearray(data[address]); value[offset] ^= 1
                    data[address] = bytes(value)
                    with self.assertRaisesRegex(ValueError, 'instruction guard mismatch'):
                        profile.check_branches(lambda a, n: data[a][:n])

    def test_truncated_guard_is_refused(self):
        data = self.image(); address = next(iter(profile.GUARDS))
        data[address] = data[address][:-1]
        with self.assertRaisesRegex(ValueError, 'instruction guard mismatch'):
            profile.check_branches(lambda a, n: data[a][:n])

    def test_foreign_radius_vtable_is_refused(self):
        data = self.image(); data[profile.VTABLE+0x34] = bytes(4)
        with self.assertRaisesRegex(ValueError, 'vtable mismatch'):
            profile.check_branches(lambda a, n: data[a][:n])

    def test_unknown_executable_is_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            exe = Path(temporary)/'unknown.exe'; exe.write_bytes(b'unknown')
            with self.assertRaisesRegex(ValueError, 'Unknown executable SHA-256'):
                profile.verify(exe)


if __name__ == '__main__':
    unittest.main()
