import base64
import copy
import hashlib
import unittest

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

from tools.sign_runtime_release import DOMAIN, canonical, sign_release


class RuntimeSigningTests(unittest.TestCase):
    def fixture(self):
        binary = b'owned publisher fixture, never loaded' * 16
        release = {'runtime_sha256': hashlib.sha256(binary).hexdigest(),
            'runtime_bytes': len(binary), 'runtime_version': '0.11.0-dev', 'sdk_api': 8,
            'lua_api': 2, 'framework_api': 1, 'capabilities': ['lua-mod-api-v2'],
            'exact_source_commit': 'a' * 40}
        return binary, release, Ed25519PrivateKey.generate()

    def test_manifest_signature_binds_exact_release_bytes_and_game(self):
        binary, release, key = self.fixture()
        value = sign_release(release, binary, key, 'b' * 64, 10, [])
        key.public_key().verify(base64.b64decode(value['signature']), DOMAIN + canonical(value['payload']))
        self.assertEqual(value['payload']['runtime_sha256'], release['runtime_sha256'])
        self.assertEqual(value['payload']['game_sha256'], 'b' * 64)

    def test_changed_release_claim_is_not_authentic(self):
        binary, release, key = self.fixture()
        value = sign_release(release, binary, key, 'b' * 64, 10, [])
        value['payload']['game_sha256'] = 'c' * 64
        with self.assertRaises(InvalidSignature):
            key.public_key().verify(base64.b64decode(value['signature']), DOMAIN + canonical(value['payload']))

    def test_wrong_binary_or_size_is_never_signed(self):
        binary, release, key = self.fixture()
        for field in ('runtime_sha256', 'runtime_bytes'):
            changed = copy.deepcopy(release)
            changed[field] = '0' * 64 if field == 'runtime_sha256' else 1
            with self.subTest(field=field), self.assertRaises(ValueError):
                sign_release(changed, binary, key, 'b' * 64, 10, [])

    def test_invalid_game_sequence_and_predecessor_are_rejected(self):
        binary, release, key = self.fixture()
        for game, sequence, previous in [('bad', 1, []), ('b'*64, 0, []), ('b'*64, 1, ['bad'])]:
            with self.subTest(game=game, sequence=sequence), self.assertRaises(ValueError):
                sign_release(release, binary, key, game, sequence, previous)
