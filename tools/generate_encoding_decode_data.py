#!/usr/bin/env python3
"""Prepare immutable decoder data from the already acquired official sources.

This does not download, build, test, or install production files. The acquired
bytes were retrieved anonymously over verified HTTPS at an exact commit URL;
their observed SHA-256 pins are checked here. No Git-object-chain attestation is
implied. Runtime decoders use one shared read-only table, not per-realm maps.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import stat

ROOT = Path('/Users/devin/repos/lumen')
COMMIT = 'be17ac5c49881336538acfc7b40dba3c8009144b'
SOURCE_SHA256 = {
    'encodings.json': '078212b3697f60b81225b6671bd9da2604497abff0aa8e96e4d46605c02ac9e7',
    'indexes.json': 'b7c8961095f0b4cae8f4a16a5011e7c84047a60c1277e28ee8df9e56074ee509',
    'LICENSE': '85dc6f5ccb57a6fe8c33d158f9fc8fc7ee5655a5d3db2cdd131c6a3d0f48a864',
}
NAMES = [
    ('UTF-8', 'UTF8'), ('IBM866', 'IBM866'),
    *[('ISO-8859-' + suffix, 'ISO_8859_' + suffix.replace('-', '_'))
      for suffix in ('2', '3', '4', '5', '6', '7', '8', '8-I', '10', '13', '14', '15', '16')],
    ('KOI8-R', 'KOI8_R'), ('KOI8-U', 'KOI8_U'), ('macintosh', 'MACINTOSH'),
    ('windows-874', 'WINDOWS_874'),
    *[('windows-' + str(code), 'WINDOWS_' + str(code)) for code in range(1250, 1259)],
    ('x-mac-cyrillic', 'X_MAC_CYRILLIC'), ('GBK', 'GBK'), ('gb18030', 'GB18030'),
    ('Big5', 'BIG5'), ('EUC-JP', 'EUC_JP'), ('ISO-2022-JP', 'ISO_2022_JP'),
    ('Shift_JIS', 'SHIFT_JIS'), ('EUC-KR', 'EUC_KR'), ('replacement', 'REPLACEMENT'),
    ('UTF-16BE', 'UTF16BE'), ('UTF-16LE', 'UTF16LE'), ('x-user-defined', 'X_USER_DEFINED'),
]
CJK_TABLES = [('big5', 'kDecodeBig5'), ('euc-kr', 'kDecodeEucKr'),
              ('gb18030', 'kDecodeGb18030'), ('jis0208', 'kDecodeJis0208'),
              ('jis0212', 'kDecodeJis0212')]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def identity(path):
    info = path.stat()
    return (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns)


def read_checked(path, expected, maximum):
    require(not path.is_symlink() and stat.S_ISREG(path.stat().st_mode),
            'Expected an ordinary source file')
    before = identity(path)
    require(0 < before[2] <= maximum, 'Source exceeds its byte bound')
    with path.open('rb') as source:
        data = source.read(maximum + 1)
    require(identity(path) == before and len(data) <= maximum and
            (expected is None or hashlib.sha256(data).hexdigest() == expected),
            'Source changed or does not match the recorded SHA-256')
    return data


def scalar(value):
    return (isinstance(value, int) and not isinstance(value, bool) and
            0 <= value <= 0x10FFFF and not 0xD800 <= value <= 0xDFFF)


def forward_table(name, values, exact_length=None):
    require(isinstance(values, list) and 0 < len(values) <= 30000,
            'Invalid bounded decoder index')
    require(exact_length is None or len(values) == exact_length,
            'Unexpected decoder index length')
    require(all(value is None or scalar(value) for value in values),
            'Decoder index contains a non-scalar value')
    words = ['0xFFFFFFFFu' if value is None else '0x%Xu' % value for value in values]
    return ('static const uint32_t ' + name + '[] = {\n' +
            ''.join('    ' + ', '.join(words[start:start + 12]) + ',\n'
                    for start in range(0, len(words), 12)) + '};\n\n')


def generate(names, indexes):
    require(isinstance(names, list) and isinstance(indexes, dict), 'Invalid source roots')
    expected, by_name, labels = dict(NAMES), {}, {}
    for group in names:
        require(isinstance(group, dict) and isinstance(group.get('encodings'), list),
                'Invalid encoding group')
        for entry in group['encodings']:
            require(isinstance(entry, dict), 'Invalid encoding entry')
            name = entry.get('name')
            require(name in expected and name not in by_name, 'Unexpected or duplicate canonical name')
            require(isinstance(entry.get('labels'), list) and entry['labels'], 'Missing labels')
            by_name[name] = entry
            for label in entry['labels']:
                require(isinstance(label, str) and 0 < len(label) <= 80 and label.isascii()
                        and label == label.lower() and label not in labels and
                        all(ch.isalnum() or ch in '-_:.' for ch in label),
                        'Invalid or duplicate encoding label')
                labels[label] = expected[name]
    require(set(by_name) == set(expected) and 0 < len(labels) <= 400,
            'Canonical encoding set differs from the native API')
    output = [
        '/* Generated from WHATWG Encoding data at commit ' + COMMIT + '.\n'
        ' * Source byte identities and HTTPS-only provenance accompany this file.\n'
        ' * Copyright WHATWG contributors; see the retained upstream LICENSE.\n'
        ' * Shared immutable decoder data. Do not hand-edit or derive from WPT.\n'
        ' */\n\n',
        'static const WebEncodingLabel kEncodingLabels[] = {\n',
    ]
    for label, identifier in sorted(labels.items()):
        output.append('    { ' + json.dumps(label) + ', WEB_ENC_' + identifier + ' },\n')
    output.append('};\n\n')
    single, entries = {}, {}
    for name, identifier in NAMES[1:29]:
        index_name = 'iso-8859-8' if name == 'ISO-8859-8-I' else name.lower()
        if name == 'ISO-8859-8-I':
            single[identifier] = single['ISO_8859_8']
            continue
        require(index_name in indexes, 'Missing official single-byte index')
        table = 'kDecode' + identifier
        output.append(forward_table(table, indexes[index_name], 128))
        single[identifier] = table
        entries[table] = 128
    for index_name, table in CJK_TABLES:
        require(index_name in indexes, 'Missing official multibyte index')
        output.append(forward_table(table, indexes[index_name]))
        entries[table] = len(indexes[index_name])
    ranges = indexes.get('gb18030-ranges')
    require(isinstance(ranges, list) and 0 < len(ranges) <= 512, 'Invalid gb18030 ranges')
    last_pointer, last_codepoint = -1, -1
    output.append('static const WebEncodingRange kDecodeGb18030Ranges[] = {\n')
    for pair in ranges:
        require(isinstance(pair, list) and len(pair) == 2 and
                isinstance(pair[0], int) and not isinstance(pair[0], bool) and scalar(pair[1]),
                'Invalid gb18030 range pair')
        pointer, codepoint = pair
        require(last_pointer < pointer <= 1237575 and last_codepoint < codepoint,
                'gb18030 ranges must increase strictly')
        last_pointer, last_codepoint = pointer, codepoint
        output.append('    { %uu, 0x%Xu },\n' % (pointer, codepoint))
    require(ranges[0] == [0, 0x80], 'The first gb18030 range must cover pointer zero')
    output.append('};\n\n')
    output.append('static const WebEncodingMetadata kEncodingMetadata[WEB_ENC_COUNT] = {\n')
    for name, identifier in NAMES:
        output.append('    [WEB_ENC_' + identifier + '] = { ' + json.dumps(name) + ', ' +
                      single.get(identifier, 'NULL') + ' },\n')
    output.append('};\n')
    data = ''.join(output).encode('ascii')
    require(len(data) <= 2 * 1024 * 1024, 'Generated source exceeds its bound')
    return data, {
        'canonical_names': len(NAMES), 'labels': len(labels), 'forward_entries': entries,
        'gb18030_ranges': len(ranges),
        'numeric_readonly_bytes': 4 * sum(entries.values()) + 8 * len(ranges),
        'lookup': 'direct index; binary search for labels and 207-style gb18030 ranges',
        'per_realm_tables': False, 'shared_iso8859_8_table': True,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sources', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    require(platform.system() == 'Darwin' and ROOT.resolve(strict=True) == ROOT,
            'Generate only in the authorized Mac checkout')
    sources = (ROOT / args.sources).resolve(strict=True)
    output = (ROOT / args.output).resolve()
    require(sources.is_relative_to(ROOT / 'output/wpt') and
            output.is_relative_to(ROOT / 'output/wpt') and not output.exists(),
            'Use recorded project-local inputs and fresh generated output')
    receipt_data = read_checked(sources / 'source-provenance.json', None, 128 * 1024)
    receipt = json.loads(receipt_data)
    require(receipt.get('status') == 'public_https_encoding_sources_acquired' and
            receipt.get('commit_url_revision') == COMMIT and
            receipt.get('git_object_chain_verified') is False,
            'Exact successful HTTPS-only source provenance is required')
    inputs, guards = {}, {}
    for name, expected in SOURCE_SHA256.items():
        entry = (receipt.get('sources') or {}).get(name) or {}
        expected_url = 'https://raw.githubusercontent.com/whatwg/encoding/' + COMMIT + '/' + name
        require(entry.get('sha256') == expected and entry.get('url') == expected_url and
                entry.get('http_status') == 200, 'Acquisition entry differs from reviewed pins')
        path = sources / name
        inputs[name] = read_checked(path, expected, 8 * 1024 * 1024)
        require(entry.get('bytes') == len(inputs[name]), 'Acquisition byte count changed')
        guards[name] = identity(path)
    data, statistics = generate(json.loads(inputs['encodings.json']), json.loads(inputs['indexes.json']))
    for name in inputs:
        require(identity(sources / name) == guards[name], 'Source changed during generation')
    require(read_checked(sources / 'source-provenance.json', None, 128 * 1024) == receipt_data,
            'Acquisition receipt changed during generation')
    output.mkdir(parents=True, exist_ok=False)
    (output / 'encoding_decode_data.inc').write_bytes(data)
    (output / 'WHATWG-LICENSE').write_bytes(inputs['LICENSE'])
    record = {
        'status': 'immutable_decoder_data_generated_not_installed',
        'commit_url_revision': COMMIT, 'git_object_chain_verified': False,
        'source_provenance_sha256': hashlib.sha256(receipt_data).hexdigest(),
        'input_sha256': SOURCE_SHA256, 'statistics': statistics,
        'encoding_decode_data.inc': {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()},
        'WHATWG-LICENSE': {'bytes': len(inputs['LICENSE']), 'sha256': SOURCE_SHA256['LICENSE']},
        'tests_executed': False, 'production_installed': False,
    }
    (output / 'generation.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    print(json.dumps(record, sort_keys=True))


if __name__ == '__main__':
    main()
