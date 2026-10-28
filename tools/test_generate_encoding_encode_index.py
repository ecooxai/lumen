"""Source-authored generator regressions; execute only on the authorized Mac."""
import copy
import unittest
from generate_encoding_encode_index import generate, ordered_pointers


class EncoderIndexTests(unittest.TestCase):
    def indexes(self):
        return {**{name: [0x4E00, None, 0x41, 0x4E00, 0x80]
                    for name in ('big5', 'euc-kr', 'gb18030', 'jis0208')},
                'iso-2022-jp-katakana': list(range(0x3000, 0x303F))}

    def test_duplicate_pointers_preserve_first_and_last_selection(self):
        self.assertEqual(ordered_pointers([0x4E00, None, 0x41, 0x4E00, 0x80]), [2, 4, 0, 3])

    def test_missing_entries_are_never_referenced(self):
        self.assertEqual(ordered_pointers([None, 0, None, 0x10FFFF]), [1, 3])

    def test_reject_non_scalar_and_non_integer_entries(self):
        for value in (-1, 0x110000, 0xD800, 0xDFFF, True, 1.5, 'a'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                ordered_pointers([value])

    def test_reject_empty_oversized_and_wrong_shape(self):
        for value in ([], [None], None, {}, [0] * 65537):
            with self.subTest(kind=type(value).__name__), self.assertRaises(ValueError):
                ordered_pointers(value)

    def test_uint16_maximum_pointer_is_preserved(self):
        values = [None] * 65536
        values[-1] = 0x10FFFF
        self.assertEqual(ordered_pointers(values), [65535])

    def test_generated_tables_do_not_duplicate_forward_values(self):
        data, record = generate(self.indexes())
        self.assertIn(b'static const uint16_t kEncodeJis0208Order[]', data)
        self.assertNotIn(b'0x4E00', data)
        self.assertEqual(record['numeric_readonly_bytes'], 4 * 4 * 2 + 63 * 4)
        self.assertFalse(record['forward_tables_duplicated'])
        self.assertFalse(record['per_realm_tables'])

    def test_complete_katakana_mapping_is_required(self):
        for value in (None, [0x3000] * 62, [0x3000] * 64, [None] * 63):
            indexes = self.indexes()
            indexes['iso-2022-jp-katakana'] = value
            with self.subTest(kind=type(value).__name__), self.assertRaises(ValueError):
                generate(indexes)

    def test_generation_is_deterministic_and_does_not_mutate_input(self):
        indexes = self.indexes()
        original = copy.deepcopy(indexes)
        self.assertEqual(generate(indexes), generate(dict(reversed(list(indexes.items())))))
        self.assertEqual(indexes, original)


if __name__ == '__main__':
    unittest.main()
