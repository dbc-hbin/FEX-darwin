#!/usr/bin/env python3
"""A zero exit or partial/stale output must never count as guest acceptance."""
import json
import unittest

from test_wine import probe_result


class WineEvidenceTests(unittest.TestCase):
    def test_requires_one_completed_probe_for_the_requested_architecture(self):
        entry = {"guest_entry": True, "pointer_bits": 32}
        completion = {"x87_checks": "PASS", "cases": 81}
        text = json.dumps(entry, separators=(",", ":")) + "\n" + json.dumps(completion, separators=(",", ":"))
        self.assertEqual(probe_result(text, "x87_checks", 32), completion)
        invalid = ("", json.dumps(entry), text + "\n" + json.dumps(completion),
                   text.replace('"cases":81', '"cases":0'), text.replace('"cases":81', '"cases":true'),
                   text.replace('"PASS"', '"FAIL"'), text.replace('"pointer_bits":32', '"pointer_bits":64'))
        for value in invalid:
            with self.subTest(value=value), self.assertRaises(RuntimeError):
                probe_result(value, "x87_checks", 32)


if __name__ == "__main__":
    unittest.main()
