"""Pure guard cases for the source generator; run only on the authorized Mac."""
import copy
import hashlib
from pathlib import Path
import tempfile
import unittest

import generate_encoding_decode_data as generator


def fixture():
    names = [{'encodings': [{'name': name, 'labels': ['label-' + key.lower()]}
                            for name, key in generator.NAMES]}]
    indexes = {}
    for name, _ in generator.NAMES[1:29]:
        indexes['iso-8859-8' if name == 'ISO-8859-8-I' else name.lower()] = list(range(128, 256))
    for name, _ in generator.CJK_TABLES:
        indexes[name] = [0x80, None, 0x4E00]
    indexes['gb18030-ranges'] = [[0, 0x80], [36, 0xA5]]
    return names, indexes


class GeneratorGuards(unittest.TestCase):
    def test_shared_forward_tables_and_provenance_shape(self):
        names, indexes = fixture()
        data, statistics = generator.generate(names, indexes)
        self.assertIn(b'0xFFFFFFFFu', data)
        self.assertEqual(data.count(b'static const uint32_t kDecodeISO_8859_8[]'), 1)
        self.assertNotIn(b'static const uint32_t kDecodeISO_8859_8_I[]', data)
        self.assertFalse(statistics['per_realm_tables'])
        self.assertEqual(statistics['canonical_names'], len(generator.NAMES))

    def test_duplicate_or_missing_canonical_name_is_rejected(self):
        names, indexes = fixture()
        missing = copy.deepcopy(names)
        missing[0]['encodings'].pop()
        with self.assertRaises(ValueError):
            generator.generate(missing, indexes)
        names[0]['encodings'].append(copy.deepcopy(names[0]['encodings'][0]))
        with self.assertRaises(ValueError):
            generator.generate(names, indexes)

    def test_ambiguous_or_non_ascii_label_is_rejected(self):
        names, indexes = fixture()
        names[0]['encodings'][1]['labels'] = names[0]['encodings'][0]['labels'][:]
        with self.assertRaises(ValueError):
            generator.generate(names, indexes)
        names, indexes = fixture()
        names[0]['encodings'][0]['labels'] = ['utf-8\u0000suffix']
        with self.assertRaises(ValueError):
            generator.generate(names, indexes)

    def test_null_entries_are_distinct_from_invalid_scalars(self):
        for bad in (0xD800, 0x110000, -1, True, '128'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                generator.forward_table('Bad', [bad])
        self.assertIn('0xFFFFFFFFu', generator.forward_table('Missing', [None]))

    def test_table_and_single_byte_lengths_are_bounded(self):
        with self.assertRaises(ValueError):
            generator.forward_table('TooLarge', [None] * 30001)
        names, indexes = fixture()
        indexes['windows-1252'] = [None] * 127
        with self.assertRaises(ValueError):
            generator.generate(names, indexes)

    def test_gb18030_ranges_require_coverage_and_monotonicity(self):
        for ranges in ([[1, 0x80]], [[0, 0x80], [0, 0x81]], [[0, 0x80], [1, 0x7F]],
                       [[0, 0x80], [1237576, 0x10000]]):
            names, indexes = fixture()
            indexes['gb18030-ranges'] = ranges
            with self.subTest(ranges=ranges), self.assertRaises(ValueError):
                generator.generate(names, indexes)

    def test_source_hash_size_and_symlink_guards(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'source'
            path.write_bytes(b'official bytes')
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            self.assertEqual(generator.read_checked(path, digest, 100), b'official bytes')
            with self.assertRaises(ValueError):
                generator.read_checked(path, '0' * 64, 100)
            with self.assertRaises(ValueError):
                generator.read_checked(path, digest, 3)
            link = Path(directory) / 'link'
            link.symlink_to(path.name)
            with self.assertRaises(ValueError):
                generator.read_checked(link, digest, 100)


if __name__ == '__main__':
    unittest.main()
