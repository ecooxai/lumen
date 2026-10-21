#!/usr/bin/env python3
"""Scoped encrypted Chrome clone and in-memory CDP-to-Lumen conversion.

This helper never accesses Keychain. The installed Chrome app handles its own
normal access controls. Only a new private profile is created or modified.
"""
from collections import Counter
import json
import math
import os
from pathlib import Path
import re
import sqlite3
import sys
import time
import chrome_to_lumen as m

CHROME_NAME = 'GPT6-AstraPro-Chrome-scoped-preserved-20261021'
LUME_NAME = 'GPT6-AstraPro-Chrome-migration-20261021'
EXPECTED_COLUMNS = {'creation_utc', 'host_key', 'top_frame_site_key', 'name', 'value',
                    'encrypted_value', 'path', 'expires_utc', 'is_secure', 'is_httponly',
                    'last_access_utc', 'has_expires', 'is_persistent', 'priority', 'samesite',
                    'source_scheme', 'source_port', 'last_update_utc', 'source_type',
                    'has_cross_site_ancestor'}


def private_paths():
    root = Path.home()/'Library/Application Support/lumen/Lumen-session-tests'
    return root/CHROME_NAME, root/LUME_NAME


def clone_scoped_chrome(source, destination):
    if destination.exists() or destination.is_symlink():
        raise m.MigrationError('destination-already-exists')
    if any(parent.is_symlink() for parent in destination.parents):
        raise m.MigrationError('symlink-destination-rejected')
    con = sqlite3.connect(source.resolve().as_uri()+'?mode=ro', uri=True, timeout=2)
    try:
        con.row_factory = sqlite3.Row
        con.execute('BEGIN')  # Schema, metadata and selected rows share one snapshot.
        metadata = list(con.execute("SELECT key,value FROM meta WHERE key IN ('version','last_compatible_version','mmap_status')"))
        version = int(dict(metadata).get('version', 0))
        if version != 24:
            raise m.MigrationError('scoped-clone-requires-version-24')
        columns = [row[1] for row in con.execute('PRAGMA table_info(cookies)')]
        if set(columns) != EXPECTED_COLUMNS:
            raise m.MigrationError('unsupported-cookie-schema')
        schema = list(con.execute("SELECT type,name,sql FROM sqlite_master WHERE tbl_name IN ('cookies','meta') AND sql IS NOT NULL ORDER BY type DESC,name"))
        if {row['name'] for row in schema if row['type'] == 'table'} != {'cookies', 'meta'}:
            raise m.MigrationError('unsupported-cookie-schema')
        for row in schema:
            allowed = (row['type'] == 'table' and re.match(r'^CREATE TABLE\s+(cookies|meta)\s*\(', row['sql'], re.I)) or (row['type'] == 'index' and row['name'] == 'cookies_unique_index' and re.match(r'^CREATE UNIQUE INDEX\s+cookies_unique_index\s+ON\s+cookies\s*\(', row['sql'], re.I))
            if not allowed:
                raise m.MigrationError('unsupported-cookie-schema-statement')
        clauses, values = [], []
        for domains in m.SITES.values():
            for domain in domains:
                clauses.append('(host_key=? OR host_key=? OR host_key LIKE ?)')
                values.extend([domain, '.'+domain, '%.'+domain])
        rows = [dict(row) for row in con.execute('SELECT * FROM cookies WHERE '+' OR '.join(clauses)+' LIMIT 1001', values)]
        if len(rows) > 1000:
            raise m.MigrationError('selected-cookie-count-exceeds-limit')
        selected, skipped = [], Counter()
        for row in rows:
            try:
                # Validate scope and security metadata without decoding any value.
                check = {**row, 'encrypted_value': b'', 'value': ''}
                m.convert_row(check, version, None)
                if row['encrypted_value'] and not row['encrypted_value'].startswith(b'v10'):
                    raise m.MigrationError('unsupported-encryption-format')
                selected.append(row)
            except m.MigrationError as error:
                skipped[str(error)] += 1
        if not selected:
            raise m.MigrationError('no-supported-selected-cookies')
        destination.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        destination.mkdir(mode=0o700)
        default = destination/'Default'
        default.mkdir(mode=0o700)
        database = default/'Cookies'
        fd = os.open(database, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        os.close(fd)
        out = sqlite3.connect(database)
        try:
            for row in schema:
                out.execute(row['sql'])
            out.executemany('INSERT INTO meta(key,value) VALUES (?,?)', metadata)
            query = 'INSERT INTO cookies('+','.join(columns)+') VALUES ('+','.join('?' for _ in columns)+')'
            out.executemany(query, [[row[key] for key in columns] for row in selected])
            out.commit()
        finally:
            out.close()
        os.chmod(database, 0o600)
        # Normal session restore preserves session cookies without changing expiry.
        for name, obj in [('Default/Preferences', {'session': {'restore_on_startup': 1}}),
                          ('LumePrivateProfile.json', {'version': 1, 'kind': 'scoped-cookie-comparison', 'copiedRows': len(selected)})]:
            fd = os.open(destination/name, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
            with os.fdopen(fd, 'w') as handle:
                json.dump(obj, handle)
        return {'chromeProfile': str(destination), 'copiedRows': len(selected),
                'sessionCookies': sum(not row['has_expires'] for row in selected),
                'persistentCookies': sum(bool(row['has_expires']) for row in selected),
                'skipped': dict(skipped), 'sourceModified': False, 'keychainAccessedByHelper': False}
    finally:
        con.close()


def convert_cdp(cookie):
    required = {'name', 'value', 'domain', 'path', 'secure', 'httpOnly', 'expires', 'session', 'sourceScheme', 'sourcePort'}
    if not required.issubset(cookie):
        raise m.MigrationError('missing-cdp-cookie-field')
    if any(not isinstance(cookie[k], str) for k in ('name', 'value', 'domain', 'path')):
        raise m.MigrationError('invalid-cdp-cookie-field')
    if any(type(cookie[k]) is not bool for k in ('secure', 'httpOnly', 'session')):
        raise m.MigrationError('invalid-cdp-cookie-field')
    if cookie.get('partitionKey') or cookie.get('partitionKeyOpaque'):
        raise m.MigrationError('partitioned-cookie')
    same_site = {None: -1, 'None': 0, 'Lax': 1, 'Strict': 2}
    schemes = {'Unset': 0, 'NonSecure': 1, 'Secure': 2}
    if cookie.get('sameSite') not in same_site or cookie['sourceScheme'] not in schemes:
        raise m.MigrationError('unsupported-cdp-cookie-mapping')
    expires = cookie['expires']
    if not isinstance(expires, (int, float)) or not math.isfinite(expires):
        raise m.MigrationError('invalid-cdp-expiry')
    row = {'host_key': cookie['domain'], 'name': cookie['name'], 'path': cookie['path'],
           'value': cookie['value'], 'encrypted_value': b'', 'is_secure': cookie['secure'],
           'is_httponly': cookie['httpOnly'], 'has_expires': not cookie['session'],
           'expires_utc': int((expires+m.CHROME_EPOCH)*1e6), 'top_frame_site_key': '',
           'samesite': same_site[cookie.get('sameSite')], 'source_scheme': schemes[cookie['sourceScheme']],
           'source_port': cookie['sourcePort']}
    return m.convert_row(row, 24, None)


def validate_cdp(cookies):
    if not isinstance(cookies, list) or len(cookies) > 1000:
        raise m.MigrationError('invalid-cookie-array')
    parsed, skipped = [], Counter()
    for index, cookie in enumerate(cookies):
        try:
            parsed.append((index, convert_cdp(cookie)))
        except m.MigrationError as error:
            skipped[str(error)] += 1
    accepted, duplicate_count = m.remove_ambiguous_duplicates([cookie for _, cookie in parsed])
    if duplicate_count:
        skipped['duplicate-scope'] += duplicate_count
    indices = [next(index for index, candidate in parsed if candidate == cookie) for cookie in accepted]
    if not accepted:
        raise m.MigrationError('no-supported-browser-cookies')
    return accepted, {'acceptedIndices': indices, 'cookiesAccepted': len(accepted), 'skipped': dict(skipped), 'cookieValuesInReport': False}


def main():
    mode = sys.argv[1] if len(sys.argv) == 2 else ''
    chrome_profile, lume_profile = private_paths()
    if mode == 'clone':
        source = Path.home()/'Library/Application Support/Google/Chrome/Default/Cookies'
        result = clone_scoped_chrome(source, chrome_profile)
    elif mode in ('validate', 'write'):
        raw = sys.stdin.buffer.read(4*1024*1024+1)
        if len(raw) > 4*1024*1024:
            raise m.MigrationError('cookie-array-too-large')
        document = json.loads(raw)
        cookies, result = validate_cdp(document['cookies'])
        if mode == 'write':
            m.write_private_profile(lume_profile, cookies)
            result.update({'lumeProfile': str(lume_profile), 'authenticationVerified': False})
    else:
        raise m.MigrationError('invalid-bridge-mode')
    print(json.dumps(result))


if __name__ == '__main__':
    try:
        main()
    except m.MigrationError as error:
        print(json.dumps({'status': 'pending-or-unsupported', 'reason': str(error)}))
        sys.exit(2)
    except Exception as error:
        print(json.dumps({'status': 'error', 'errorType': type(error).__name__}))
        sys.exit(3)
