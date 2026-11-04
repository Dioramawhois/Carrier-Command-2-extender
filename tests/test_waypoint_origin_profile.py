# SPDX-License-Identifier: GPL-3.0-or-later
"""Bind the origin decoder's ABI to actual receive-path instruction operands."""
import json
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]

class OriginProfileTest(unittest.TestCase):
    def test_frame_and_roster_offsets_match_native_instruction_operands(self):
        profile = json.loads((ROOT / 'integrations/waypoint-origin-profile.json').read_text())
        guards = {g['address']: bytes.fromhex(g['bytes']) for g in profile['guards']}
        # LEA EDI,[ESI+peer_deque] supplies the same roster used by the engine lookup.
        self.assertEqual(guards[0x77779b][:2], bytes.fromhex('8dbe'))
        self.assertEqual(struct.unpack('<I', guards[0x77779b][2:6])[0], profile['server_peer_manager_offset'])
        lookup = guards[0x572240]
        # The receiver's peer lookup uses a second deque inside that manager.
        self.assertIn(bytes([0x8b, 0x7b, profile['peer_deque_offset']]), lookup)
        self.assertIn(bytes([0x39, 0x43, profile['peer_deque_offset'] + 12]), lookup)
        # The actual receive call receives &sender_id at the negative EBP displacement.
        self.assertIn(bytes.fromhex('8d8d') + struct.pack('<i', -profile['sender_stack_offset']), guards[0x777996])
        # The continuation publishes this exact vehicle ID, not a packet-supplied peer ID.
        self.assertIn(bytes.fromhex('ffb0') + struct.pack('<I', profile['vehicle_id_offset']), guards[0x7782c4])
        self.assertEqual(profile['profile'], '1.5.18-win32-r2')
        self.assertEqual(len(profile['exe_sha256']), 64)
        self.assertEqual(len(guards), 5)
        self.assertIn('unverified', profile['validation'])

if __name__ == '__main__':
    unittest.main()
