#!/usr/bin/env python3
"""Generate compact encoder lookup order from the pinned official indexes.

This neither downloads nor installs production code. The decoder's large
forward tables remain single-copy; uint16 pointer indexes accelerate reverse
lookup, and the one additional 63-entry ISO-2022-JP mapping is official data.
Run generation and its tests only on the authorized Mac through devinmac3d.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import stat

ROOT = Path('/Users/devin/repos/lumen')
COMMIT = 'be17ac5c49881336538acfc7b40dba3c8009144b'
INDEX_SHA = 'b7c8961095f0b4cae8f4a16a5011e7c84047a60c1277e28ee8df9e56074ee509'
LICENSE_SHA = '85dc6f5ccb57a6fe8c33d158f9fc8fc7ee5655a5d3db2cdd131c6a3d0f48a864'
DECODER_TABLE_SHA = '759a6b5cb49e450b6aab966e176a7f410c7cdcb0e140abee88ea009a2d8d9a65'
TABLES = [('big5', 'kEncodeBig5Order'), ('euc-kr', 'kEncodeEucKrOrder'),
          ('gb18030', 'kEncodeGb18030Order'), ('jis0208', 'kEncodeJis0208Order')]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def identity(path):
    info = path.stat()
    return [info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns]


def read_checked(path, expected, maximum):
    require(not path.is_symlink() and stat.S_ISREG(path.stat().st_mode), 'Expected an ordinary file')
    before = identity(path)
    require(0 < before[2] <= maximum, 'Input exceeds its byte bound')
    with path.open('rb') as source:
        data = source.read(maximum + 1)
    require(identity(path) == before and len(data) == before[2] and len(data) <= maximum,
            'Input changed during the bounded read')
    require(expected is None or hashlib.sha256(data).hexdigest() == expected,
            'Input differs from the pinned official bytes')
    return data


def scalar(value):
    return (isinstance(value, int) and not isinstance(value, bool) and
            0 <= value <= 0x10FFFF and not 0xD800 <= value <= 0xDFFF)


def ordered_pointers(values):
    require(isinstance(values, list) and 0 < len(values) <= 65536,
            'Reverse lookup requires a bounded uint16 pointer index')
    require(all(value is None or scalar(value) for value in values),
            'The forward index contains a non-scalar value')
    order = sorted((pointer for pointer, value in enumerate(values) if value is not None),
                   key=lambda pointer: (values[pointer], pointer))
    require(order, 'The reverse index cannot be empty')
    return order


def generate(indexes):
    require(isinstance(indexes, dict), 'Expected the official index mapping')
    output = ['/* Generated from WHATWG Encoding commit ' + COMMIT + '.\n'
              ' * Copyright WHATWG contributors; see the retained upstream LICENSE.\n'
              ' * Compact pointer order into the original decoder tables. No WPT inputs.\n'
              ' */\n\n']
    counts = {}
    for name, symbol in TABLES:
        order = ordered_pointers(indexes.get(name))
        counts[name] = {'forward_entries': len(indexes[name]), 'reverse_entries': len(order)}
        output.append('static const uint16_t ' + symbol + '[] = {\n')
        for begin in range(0, len(order), 16):
            output.append('    ' + ', '.join(str(pointer) for pointer in order[begin:begin + 16]) + ',\n')
        output.append('};\n\n')
    katakana = indexes.get('iso-2022-jp-katakana')
    require(isinstance(katakana, list) and len(katakana) == 63 and all(map(scalar, katakana)),
            'Expected the complete official ISO-2022-JP katakana mapping')
    output.append('static const uint32_t kEncodeIso2022JPKatakana[63] = {\n')
    for begin in range(0, len(katakana), 12):
        output.append('    ' + ', '.join('0x%Xu' % point for point in katakana[begin:begin + 12]) + ',\n')
    output.append('};\n')
    data = ''.join(output).encode('ascii')
    require(len(data) <= 2 * 1024 * 1024, 'Generated output exceeds its bound')
    return data, {'tables': counts, 'katakana_entries': 63,
                  'numeric_readonly_bytes': 2 * sum(value['reverse_entries'] for value in counts.values()) + 4 * 63,
                  'duplicates_ordered_by_pointer': True, 'forward_tables_duplicated': False,
                  'per_realm_tables': False, 'runtime_lookup': 'binary search and bounded duplicate selection'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sources', type=Path, required=True)
    parser.add_argument('--decoder-table', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    require(platform.system() == 'Darwin' and ROOT.resolve(strict=True) == ROOT,
            'Generate only in the authorized Mac checkout')
    sources = (ROOT / args.sources).resolve(strict=True)
    decoder_table = (ROOT / args.decoder_table).resolve(strict=True)
    output = (ROOT / args.output).resolve()
    require(sources.is_relative_to(ROOT / 'output/wpt') and
            decoder_table.is_relative_to(ROOT / 'output/wpt') and
            output.is_relative_to(ROOT / 'output/wpt') and not output.exists(),
            'Use recorded project-local inputs and fresh generated output')
    receipt_path = sources / 'source-provenance.json'
    receipt_bytes = read_checked(receipt_path, None, 128 * 1024)
    receipt = json.loads(receipt_bytes)
    require(receipt.get('status') == 'public_https_encoding_sources_acquired' and
            receipt.get('commit_url_revision') == COMMIT and receipt.get('git_object_chain_verified') is False,
            'The successful exact-commit HTTPS acquisition record is required')
    data_by_name, guards = {}, {}
    for name, expected in (('indexes.json', INDEX_SHA), ('LICENSE', LICENSE_SHA)):
        record = (receipt.get('sources') or {}).get(name) or {}
        require(record.get('sha256') == expected and record.get('http_status') == 200 and
                record.get('url') == 'https://raw.githubusercontent.com/whatwg/encoding/' + COMMIT + '/' + name,
                'Source acquisition differs from the reviewed identity')
        path = sources / name
        data_by_name[name] = read_checked(path, expected, 8 * 1024 * 1024)
        require(record.get('bytes') == len(data_by_name[name]), 'Source byte count changed')
        guards[str(path)] = identity(path)
    read_checked(decoder_table, DECODER_TABLE_SHA, 2 * 1024 * 1024)
    guards[str(decoder_table)] = identity(decoder_table)
    data, statistics = generate(json.loads(data_by_name['indexes.json']))
    for name, before in guards.items():
        require(identity(Path(name)) == before, 'Input changed during generation')
    require(read_checked(receipt_path, None, 128 * 1024) == receipt_bytes,
            'Source acquisition evidence changed during generation')
    output.mkdir(parents=True, exist_ok=False)
    (output / 'encoding_encode_index.inc').write_bytes(data)
    (output / 'WHATWG-LICENSE').write_bytes(data_by_name['LICENSE'])
    record = {'status': 'immutable_encoder_index_generated_not_installed',
              'commit_url_revision': COMMIT, 'git_object_chain_verified': False,
              'source_provenance_sha256': hashlib.sha256(receipt_bytes).hexdigest(),
              'source_indexes_sha256': INDEX_SHA, 'decoder_table_sha256': DECODER_TABLE_SHA,
              'input_identities': guards, 'statistics': statistics,
              'encoding_encode_index.inc': {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()},
              'WHATWG-LICENSE': {'bytes': len(data_by_name['LICENSE']), 'sha256': LICENSE_SHA},
              'tests_executed': False, 'production_installed': False}
    (output / 'generation.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    print(json.dumps(record, sort_keys=True))


if __name__ == '__main__':
    main()
