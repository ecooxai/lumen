"""Decode console-framed WPT reports using the transport's LF delimiter.

JSON.stringify leaves Unicode line/paragraph separators unescaped. Python's
str.splitlines() therefore corrupts valid JSON records; only LF frames them.
"""
import json
import re

MARKER = re.compile(r'ASTRAWPT-([HSE])(?: |$)')


def parse_console_report(text):
    header = None
    subtests = []
    errors = []
    headers = 0
    complete = False
    for line_number, line in enumerate(text.split('\n'), 1):
        match = MARKER.search(line)
        if match is None:
            continue
        kind = match.group(1)
        payload = line[match.end():]
        if kind == 'E':
            complete = True
            break
        try:
            value = json.loads(payload)
            if kind == 'H':
                if not isinstance(value, dict) or 'status' not in value:
                    raise ValueError('header lacks harness status')
                headers += 1
                if header is None:
                    header = value
            else:
                if not isinstance(value, list) or len(value) != 3:
                    raise ValueError('subtest record is not a three-item array')
                subtests.append(value)
        except (ValueError, TypeError) as error:
            errors.append({'line': line_number, 'kind': kind, 'error': str(error)})
    if not complete:
        return None
    if headers != 1:
        errors.append({'error': 'expected one harness header', 'headers': headers})
    result = dict(header or {})
    expected = result.get('reported_subtests_expected')
    if expected is not None and (type(expected) is not int or expected != len(subtests)):
        errors.append({'error': 'completion-vector count differs from parsed frames',
                       'expected': expected, 'actual': len(subtests)})
    result['subtests'] = subtests
    result['collector'] = {'version': 'lf-json-frames-v1', 'completion_marker': True,
                           'header_count': headers, 'parsed_subtests': len(subtests),
                           'completion_vector_count': expected,
                           'completion_vector_count_available': expected is not None,
                           'malformed_records': errors}
    if errors:
        result['harness_status_before_collection_error'] = result.get('status')
        result['status'] = 'COLLECTOR_ERROR'
        result['message'] = 'Console report framing/integrity failed; see collector metadata.'
    return result
