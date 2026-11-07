import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('navigation_profile', ROOT/'tools/verify_navigation_profile.py')
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


class NavigationProfileTests(unittest.TestCase):
    def image(self):
        data = {address: bytes.fromhex(signature) for address, signature in profile.GUARDS.items()}
        data[profile.VTABLE+56*4] = struct.pack('<II', 0x705BD0, profile.MOTION)
        return data

    def test_exact_reviewed_branch_fixture(self):
        data = self.image()
        self.assertEqual(len(profile.check_branches(lambda a, n: data[a][:n])), 5)

    def test_every_instruction_guard_rejects_mutation(self):
        for address in profile.GUARDS:
            with self.subTest(address=address):
                data = self.image()
                data[address] = bytes([data[address][0] ^ 1])+data[address][1:]
                with self.assertRaisesRegex(ValueError, 'instruction guard mismatch'):
                    profile.check_branches(lambda a, n: data[a][:n])

    def test_different_vtable_is_refused(self):
        data = self.image(); data[profile.VTABLE+56*4] = bytes(8)
        with self.assertRaisesRegex(ValueError, 'vtable mismatch'):
            profile.check_branches(lambda a, n: data[a][:n])

    def test_unknown_executable_is_refused_before_pe_parsing(self):
        with tempfile.TemporaryDirectory() as temporary:
            exe = Path(temporary)/'unknown.exe'; exe.write_bytes(b'unknown')
            with self.assertRaisesRegex(ValueError, 'Unknown executable SHA-256'):
                profile.verify(exe)


if __name__ == '__main__':
    unittest.main()
