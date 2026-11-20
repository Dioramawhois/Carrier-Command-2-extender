"""Sign standalone runtime update metadata. Never installs or rebuilds the DLL."""

import argparse
import base64
import hashlib
import json
from pathlib import Path
import re

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

DOMAIN = b'CC2X runtime provider v1\n'


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'),
                      ensure_ascii=True, allow_nan=False).encode('ascii')


def sign_release(release, binary, key, game_sha256, sequence, previous):
    if not isinstance(key, Ed25519PrivateKey):
        raise ValueError('Ed25519 publisher key required')
    digest = hashlib.sha256(binary).hexdigest()
    if (digest != release['runtime_sha256'] or len(binary) != release['runtime_bytes']
            or not re.fullmatch('[0-9a-f]{64}', game_sha256)
            or type(sequence) is not int or sequence < 1
            or any(not re.fullmatch('[0-9a-f]{64}', value) for value in previous)):
        raise ValueError('Runtime update input differs from the verified release')
    payload = {'publisher': 'cc2x', 'package_id': 'cc2x.runtime', 'sequence': sequence,
        'version': release['runtime_version'], 'game_sha256': game_sha256,
        'runtime_sha256': digest, 'runtime_bytes': len(binary), 'api': release['sdk_api'],
        'lua_api': release['lua_api'], 'framework_api': release['framework_api'],
        'capabilities': release['capabilities'], 'previous_runtime_sha256': sorted(set(previous)),
        'source_commit': release['exact_source_commit']}
    public = key.public_key().public_bytes_raw()
    key_id = 'cc2x-' + hashlib.sha256(public).hexdigest()[:16]
    return {'schema': 1, 'key_id': key_id, 'payload': payload,
            'signature': base64.b64encode(key.sign(DOMAIN + canonical(payload))).decode('ascii')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--release', type=Path, required=True)
    parser.add_argument('--key', type=Path, required=True)
    parser.add_argument('--game-sha256', required=True)
    parser.add_argument('--sequence', type=int, required=True)
    parser.add_argument('--previous-profile', type=Path,
                        default=Path(__file__).resolve().parents[1] / 'sdk/runtime-update-policy.json')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError('A frozen signed manifest is never overwritten')
    key = serialization.load_pem_private_key(args.key.read_bytes(), password=None)
    if not isinstance(key, Ed25519PrivateKey):
        raise ValueError('Ed25519 publisher key required')
    profile = json.loads(args.previous_profile.read_text())
    manifest = sign_release(json.loads((args.release / 'RELEASE.json').read_text()),
        (args.release / 'dinput8.dll').read_bytes(), key, args.game_sha256, args.sequence,
        [*([profile['runtime_sha256']] if profile.get('runtime_sha256') else []),
         *profile.get('previous_runtime_sha256', [])])
    policy = json.loads((Path(__file__).resolve().parents[1] / 'sdk/runtime-update-policy.json').read_text())
    if manifest['key_id'] != policy['publisher_key_id']:
        raise ValueError('SDK publisher key differs from the trusted release policy')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes((json.dumps(manifest, indent=2) + '\n').encode())
    print(json.dumps({'key_id': manifest['key_id'], 'sequence': args.sequence,
                     'runtime_sha256': manifest['payload']['runtime_sha256']}))


if __name__ == '__main__':
    main()
