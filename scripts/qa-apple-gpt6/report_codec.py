#!/usr/bin/env python3
"""Decode bounded Lume diagnostics, rejecting incomplete/conflicting chunks.

Transport decoding alone does not certify functional results or platform identity.
"""
import argparse
import json
from pathlib import Path

CHUNK = 'LUME_LAB_REPORT_CHUNK '
DIRECT = ('LUME_LAB_REPORT ', 'LUME_LAB_CLEANUP ', 'LUME_SITE_PROBE ')


def decode_lines(lines):
    reports, errors, groups, concise = [], [], {}, []
    for number, line in enumerate(lines, 1):
        if 'LUME_LAB_REPORT_ERROR ' in line:
            errors.append({'line': number, 'error': 'page rejected diagnostic output'})
            continue
        offset = line.find(CHUNK)
        if offset >= 0:
            identifier = None
            try:
                item = json.loads(line[offset + len(CHUNK):])
                if not isinstance(item, dict):
                    raise ValueError('chunk must be an object')
                identifier = item.get('id')
                if (type(item.get('version')) is not int or item['version'] != 1
                        or type(item.get('index')) is not int or type(item.get('total')) is not int
                        or not 0 <= item['index'] < item['total'] <= 512
                        or not isinstance(identifier, str) or not 1 <= len(identifier) <= 64
                        or not isinstance(item.get('data'), str) or len(item['data']) > 256):
                    raise ValueError('invalid diagnostic chunk metadata')
                if identifier not in groups:
                    if len(groups) >= 4:
                        raise ValueError('too many diagnostic report groups')
                    groups[identifier] = {'total': item['total'], 'parts': {}, 'invalid': False}
                group = groups[identifier]
                if group['total'] != item['total']:
                    raise ValueError('inconsistent diagnostic chunk count')
                if item['index'] in group['parts'] and group['parts'][item['index']] != item['data']:
                    raise ValueError('conflicting diagnostic chunk')
                group['parts'][item['index']] = item['data']
            except (ValueError, TypeError, KeyError) as error:
                if isinstance(identifier, str) and identifier in groups:
                    groups[identifier]['invalid'] = True
                errors.append({'line': number, 'error': str(error)})
            continue
        if 'LUME_LAB_RESULT ' in line:
            try:
                value = json.loads(line.split('LUME_LAB_RESULT ', 1)[1])
                if not isinstance(value, dict):
                    raise ValueError('concise result must be an object')
                if len(concise) >= 64:
                    raise ValueError('too many concise results')
                concise.append(value)
            except (ValueError, TypeError) as error:
                errors.append({'line': number, 'error': str(error)})
            continue
        for marker in DIRECT:
            offset = line.find(marker)
            if offset < 0:
                continue
            try:
                if len(reports) >= 16:
                    raise ValueError('too many direct reports')
                value = json.loads(line[offset + len(marker):])
                if not isinstance(value, dict):
                    raise ValueError('diagnostic report must be an object')
                reports.append({'marker': marker.strip(), 'value': value})
            except (ValueError, TypeError) as error:
                errors.append({'line': number, 'error': str(error)})
            break
    # Assemble only after EOF: a conflicting duplicate after a complete group
    # must invalidate that group, rather than leave an apparently valid report.
    for identifier, group in groups.items():
        if group['invalid']:
            continue
        if len(group['parts']) != group['total']:
            errors.append({'id': identifier, 'error': 'incomplete diagnostic report',
                           'expected': group['total'], 'received': len(group['parts'])})
            continue
        try:
            raw = ''.join(group['parts'][index] for index in range(group['total']))
            # JS slices UTF-16 units; a Unicode surrogate pair can straddle chunks.
            raw = raw.encode('utf-16-le', 'surrogatepass').decode('utf-16-le', 'surrogatepass')
            value = json.loads(raw)
            if not isinstance(value, dict):
                raise ValueError('reconstructed report must be an object')
            reports.append({'marker': 'LUME_LAB_REPORT', 'transport': 'chunked-console',
                            'id': identifier, 'chunks': group['total'], 'value': value})
        except (ValueError, TypeError, UnicodeError) as error:
            errors.append({'id': identifier, 'error': str(error)})
    return {'reports': reports, 'errors': errors, 'conciseResults': concise,
            'transportComplete': not errors,
            'note': 'Complete transport is not functional success; inspect exact native results and scope.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with args.log.open(errors='replace') as source:
        decoded = decode_lines(source)
    args.output.write_text(json.dumps(decoded, indent=2) + '\n')
    print(json.dumps({'reports': len(decoded['reports']), 'errors': len(decoded['errors']),
                      'output': str(args.output)}))
    return 0 if decoded['transportComplete'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
