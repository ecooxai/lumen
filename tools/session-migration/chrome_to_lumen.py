#!/usr/bin/env python3
"""Authorized, selected-site Chrome-to-Lumen migration into a new private profile.

Run on the same Mac as the signed-in Chrome account. Default is metadata only.
Cookie/key values never go to stdout, a report, a command argument, or this repo.
Normal Keychain access may require user interaction; denial is never bypassed.
"""
import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass, field
import hashlib
import hmac
import json
import os
from pathlib import Path
import re
import sqlite3
import subprocess
import sys
import time

CHROME_EPOCH = 11644473600
SITES = {'google': ('google.com',), 'youtube': ('youtube.com',),
         'chatgpt': ('chatgpt.com', 'openai.com'), 'x': ('x.com', 'twitter.com')}
SAME_SITE = {-1: 0, 0: 1, 1: 2, 2: 3}
FIELDS = ['host_key', 'name', 'path', 'is_secure', 'is_httponly', 'expires_utc',
          'has_expires', 'samesite', 'top_frame_site_key', 'source_scheme', 'source_port']


class MigrationError(Exception):
    """Messages are fixed, non-sensitive status codes only."""


@dataclass(frozen=True)
class Cookie:
    domain: str
    host_only: bool
    path: str
    secure: bool
    http_only: bool
    expires: float
    name: str
    value: str = field(repr=False)
    same_site: int = 0

    def line(self):
        return '\t'.join([self.domain, str(int(self.host_only)), self.path,
                          str(int(self.secure)), str(int(self.http_only)),
                          format(self.expires, '.6f'), self.name, self.value,
                          str(self.same_site)])


def site_for(host):
    bare = host.lstrip('.').lower()
    for site, domains in SITES.items():
        if any(bare == domain or bare.endswith('.'+domain) for domain in domains):
            return site
    return None


def metadata_reason(row, now=None):
    now = time.time() if now is None else now
    if not site_for(row['host_key']):
        return 'outside-site-scope'
    if row['top_frame_site_key']:
        return 'partitioned-cookie'
    if row['has_expires'] and row['expires_utc']/1e6-CHROME_EPOCH <= now:
        return 'expired-cookie'
    return None


def metadata_summary(rows):
    counts = {site: {'selectedLiveRows': 0, 'partitionedSkipped': 0,
                     'expiredSkipped': 0} for site in SITES}
    for row in rows:
        site = site_for(row['host_key'])
        if not site:
            continue
        reason = metadata_reason(row)
        key = 'partitionedSkipped' if reason == 'partitioned-cookie' else 'expiredSkipped' if reason else 'selectedLiveRows'
        counts[site][key] += 1
    return counts


def read_selected_rows(database, include_values=False):
    con = sqlite3.connect(database.resolve().as_uri()+'?mode=ro', uri=True, timeout=2)
    try:
        con.row_factory = sqlite3.Row
        version_row = con.execute("SELECT value FROM meta WHERE key='version'").fetchone()
        version = int(version_row[0]) if version_row else 0
        if version not in (23, 24):
            raise MigrationError('unsupported-database-version')
        columns = {row[1] for row in con.execute('PRAGMA table_info(cookies)')}
        if not set(FIELDS+['value', 'encrypted_value']).issubset(columns):
            raise MigrationError('unsupported-cookie-schema')
        fields = FIELDS + (['value', 'encrypted_value'] if include_values else [])
        clauses, values = [], []
        for domains in SITES.values():
            for domain in domains:
                clauses.append('(host_key=? OR host_key=? OR host_key LIKE ?)')
                values.extend([domain, '.'+domain, '%.'+domain])
        query = 'SELECT '+','.join(fields)+' FROM cookies WHERE '+' OR '.join(clauses)+' LIMIT 1001'
        rows = [dict(row) for row in con.execute(query, values)]
        if len(rows) > 1000:
            raise MigrationError('selected-cookie-count-exceeds-limit')
        return version, rows
    finally:
        con.close()


def derive_key(password):
    return hashlib.pbkdf2_hmac('sha1', password, b'saltysalt', 1003, dklen=16)


def decrypt_value(row, version, key):
    encrypted = row.get('encrypted_value') or b''
    plain = row.get('value') or ''
    if not encrypted:
        return plain
    if plain:
        raise MigrationError('ambiguous-plain-and-encrypted-values')
    if not encrypted.startswith(b'v10'):
        raise MigrationError('unsupported-encryption-format')
    if key is None:
        raise MigrationError('keychain-key-unavailable')
    try:
        from cryptography.hazmat.primitives import padding
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        decryptor = Cipher(algorithms.AES(key), modes.CBC(b' '*16)).decryptor()
        padded = decryptor.update(encrypted[3:]) + decryptor.finalize()
        unpad = padding.PKCS7(128).unpadder()
        data = unpad.update(padded) + unpad.finalize()
    except Exception:
        raise MigrationError('cookie-decryption-failed') from None
    if version >= 24:
        expected = hashlib.sha256(row['host_key'].encode('utf-8')).digest()
        if len(data) < 32 or not hmac.compare_digest(data[:32], expected):
            raise MigrationError('domain-hash-mismatch')
        data = data[32:]
    try:
        return data.decode('utf-8')
    except UnicodeError:
        raise MigrationError('invalid-cookie-text-encoding') from None


def convert_row(row, version, key):
    if version not in (23, 24):
        raise MigrationError('unsupported-database-version')
    reason = metadata_reason(row)
    if reason:
        raise MigrationError(reason)
    host = row['host_key']
    host_only = not host.startswith('.')
    domain = host[1:] if not host_only else host
    if not re.fullmatch(r'[a-z0-9-]+(?:\.[a-z0-9-]+)+', domain):
        raise MigrationError('invalid-cookie-domain')
    name, path = row['name'], row['path']
    if not re.fullmatch(r"[!#$%&'*+.^_`|~0-9A-Za-z-]+", name):
        raise MigrationError('invalid-cookie-name')
    if not path.startswith('/') or any(ord(c) < 32 or ord(c) == 127 for c in path):
        raise MigrationError('invalid-cookie-path')
    secure, http_only = bool(row['is_secure']), bool(row['is_httponly'])
    if row['samesite'] not in SAME_SITE:
        raise MigrationError('unsupported-same-site-mode')
    same_site = SAME_SITE[row['samesite']]
    if same_site == 1 and not secure:
        raise MigrationError('insecure-same-site-none')
    if name.startswith('__Secure-') and not secure:
        raise MigrationError('invalid-secure-prefix')
    if name.startswith('__Host-') and (not secure or not host_only or path != '/'):
        raise MigrationError('invalid-host-prefix')
    if name.startswith(('__Http-', '__Host-Http-')) and (not secure or not http_only):
        raise MigrationError('invalid-http-prefix')
    if row['source_scheme'] not in (0, 1, 2) or row['source_port'] not in (-1, 80, 443):
        raise MigrationError('unsupported-source-binding')
    if secure and row['source_scheme'] == 1:
        raise MigrationError('inconsistent-secure-source')
    value = decrypt_value(row, version, key)
    if any(ord(c) < 32 or ord(c) == 127 or c == ';' for c in value):
        raise MigrationError('invalid-cookie-value')
    if len(name.encode())+len(value.encode()) > 4096 or len(path.encode()) > 1024:
        raise MigrationError('cookie-size-exceeds-limit')
    expires = row['expires_utc']/1e6-CHROME_EPOCH if row['has_expires'] else -1
    return Cookie(domain, host_only, path, secure, http_only, expires, name, value, same_site)


def remove_ambiguous_duplicates(cookies):
    by_scope = defaultdict(list)
    for cookie in cookies:
        by_scope[(cookie.domain, cookie.path, cookie.name)].append(cookie)
    accepted, skipped = [], 0
    for group in by_scope.values():
        if len(set(group)) == 1:
            accepted.append(group[0])
            skipped += len(group)-1
        else:
            skipped += len(group)
    return accepted, skipped


def read_normal_keychain_password():
    if sys.platform != 'darwin':
        raise MigrationError('same-mac-keychain-required')
    try:
        response = subprocess.run(['/usr/bin/security', 'find-generic-password',
                                   '-s', 'Chrome Safe Storage', '-a', 'Chrome', '-w'],
                                  stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, timeout=25, check=False)
    except subprocess.TimeoutExpired:
        raise MigrationError('keychain-user-interaction-pending') from None
    if response.returncode or not response.stdout:
        raise MigrationError('keychain-access-pending-or-denied')
    # The CLI appends one newline; the Chrome password is base64 text.
    return response.stdout.removesuffix(b'\n')


def write_private_profile(destination, cookies):
    if destination.exists() or destination.is_symlink():
        raise MigrationError('destination-already-exists')
    if any(parent.is_symlink() for parent in destination.parents):
        raise MigrationError('symlink-destination-rejected')
    destination.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    destination.mkdir(mode=0o700)
    os.chmod(destination, 0o700)
    cookie_path = destination/'cookies.txt'
    try:
        descriptor = os.open(cookie_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_NOFOLLOW', 0), 0o600)
        with os.fdopen(descriptor, 'w', encoding='utf-8', newline='\n') as handle:
            os.fchmod(handle.fileno(), 0o600)
            for cookie in cookies:
                handle.write(cookie.line()+'\n')
            handle.flush()
            os.fsync(handle.fileno())
    except Exception:
        cookie_path.unlink(missing_ok=True)
        destination.rmdir()
        raise MigrationError('private-profile-write-failed') from None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--chrome-profile', default='Default')
    parser.add_argument('--destination-name', default='GPT6-AstraPro-Chrome-migration-20261021')
    parser.add_argument('--apply', action='store_true', help='Use normal Keychain access and create a NEW private Lumen test profile.')
    args = parser.parse_args()
    if not re.fullmatch(r'Default|Profile [0-9]+', args.chrome_profile):
        raise MigrationError('invalid-profile-selection')
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]{7,80}', args.destination_name):
        raise MigrationError('invalid-destination-name')
    base = Path.home()/'Library/Application Support'
    chrome = base/'Google/Chrome'/args.chrome_profile
    database = next((p for p in (chrome/'Network/Cookies', chrome/'Cookies') if p.is_file()), None)
    if database is None:
        raise MigrationError('selected-chrome-profile-not-found')
    destination = base/'lumen/Lumen-session-tests'/args.destination_name
    if args.apply and destination.exists():
        raise MigrationError('destination-already-exists')
    version, metadata = read_selected_rows(database, include_values=False)
    report = {'mode': 'apply' if args.apply else 'metadata-only', 'profileDirectory': args.chrome_profile,
              'databaseVersion': version, 'siteScope': list(SITES), 'sites': metadata_summary(metadata),
              'authenticationVerified': False, 'defaultProfilesModified': False, 'cookieValuesInReport': False}
    if not args.apply:
        print(json.dumps(report, indent=2))
        return 0
    print(json.dumps({'stage': 'normal-keychain-access', 'userInteractionMayBeRequired': True}), flush=True)
    password = read_normal_keychain_password()
    key = derive_key(password)
    del password
    version, rows = read_selected_rows(database, include_values=True)
    cookies, skipped = [], Counter()
    for row in rows:
        try:
            cookies.append(convert_row(row, version, key))
        except MigrationError as error:
            skipped[str(error)] += 1
    del key, rows
    cookies, duplicate_count = remove_ambiguous_duplicates(cookies)
    if duplicate_count:
        skipped['duplicate-scope'] += duplicate_count
    if not cookies:
        raise MigrationError('no-supported-selected-cookies')
    write_private_profile(destination, cookies)
    per_site = Counter(site_for(cookie.domain) for cookie in cookies)
    report.update({'privateProfile': str(destination), 'cookiesWritten': len(cookies),
                   'writtenPerSite': dict(per_site), 'skipped': dict(skipped),
                   'directoryMode': '0700', 'cookieFileMode': '0600',
                   'boundSessionKeysCopied': False, 'partitionedCookiesCopied': False})
    del cookies
    print(json.dumps(report, indent=2))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except MigrationError as error:
        print(json.dumps({'status': 'pending-or-unsupported', 'reason': str(error), 'authenticationVerified': False}))
        sys.exit(2)
    except Exception as error:
        # Never include repr(error), SQL rows, command stdout, or tracebacks.
        print(json.dumps({'status': 'error', 'errorType': type(error).__name__, 'authenticationVerified': False}))
        sys.exit(3)
