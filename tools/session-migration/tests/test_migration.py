import importlib.util
import json
import os
from pathlib import Path
import sqlite3
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('migration', ROOT / 'chrome_to_lumen.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class MigrationTests(unittest.TestCase):
    def row(self, **changes):
        row = dict(host_key='.google.com', name='SID', path='/', is_secure=1,
                   is_httponly=1, expires_utc=int((time.time()+3600+m.CHROME_EPOCH)*1e6),
                   has_expires=1, samesite=1, top_frame_site_key='', source_scheme=2,
                   source_port=443, value='synthetic-login-fixture', encrypted_value=b'')
        row.update(changes)
        return row

    def encrypt(self, host, value, password=b'synthetic-password', version=24):
        import hashlib
        from cryptography.hazmat.primitives import padding
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        plain = (hashlib.sha256(host.encode()).digest() if version >= 24 else b'') + value.encode()
        pad = padding.PKCS7(128).padder()
        padded = pad.update(plain) + pad.finalize()
        cipher = Cipher(algorithms.AES(m.derive_key(password)), modes.CBC(b' '*16)).encryptor()
        return b'v10' + cipher.update(padded) + cipher.finalize()

    def test_encrypted_v24_domain_hash_and_flag_mapping(self):
        row = self.row(value='', encrypted_value=self.encrypt('.google.com', 'synthetic-login-fixture'))
        cookie = m.convert_row(row, 24, m.derive_key(b'synthetic-password'))
        self.assertEqual(cookie.value, 'synthetic-login-fixture')
        self.assertEqual(cookie.domain, 'google.com')
        self.assertFalse(cookie.host_only)
        self.assertTrue(cookie.secure)
        self.assertTrue(cookie.http_only)
        self.assertEqual(cookie.same_site, 2)

    def test_wrong_domain_digest_is_rejected(self):
        row = self.row(value='', encrypted_value=self.encrypt('.youtube.com', 'synthetic-login-fixture'))
        with self.assertRaisesRegex(m.MigrationError, '^domain-hash-mismatch$'):
            m.convert_row(row, 24, m.derive_key(b'synthetic-password'))

    def test_scope_expiry_and_partition_are_checked_before_decryption(self):
        for change, reason in [({'host_key':'.google.com.attacker.test'}, 'outside-site-scope'),
                               ({'top_frame_site_key':'https://other.test'}, 'partitioned-cookie'),
                               ({'expires_utc':1}, 'expired-cookie')]:
            with self.assertRaisesRegex(m.MigrationError, '^'+reason+'$'):
                m.convert_row(self.row(encrypted_value=b'unknown', **change), 24, None)

    def test_prefixes_samesite_and_control_characters_fail_closed(self):
        bad = [dict(name='__Host-test'), dict(name='__Secure-test', is_secure=0),
               dict(name='__Http-test', is_httponly=0), dict(samesite=0, is_secure=0),
               dict(value='synthetic\r\nInjected: true'), dict(path='/\tunsafe'),
               dict(name='name; bad'), dict(samesite=9)]
        for change in bad:
            with self.assertRaises(m.MigrationError):
                m.convert_row(self.row(**change), 24, None)

    def test_unknown_formats_and_versions_are_not_guessed(self):
        with self.assertRaisesRegex(m.MigrationError, '^unsupported-encryption-format$'):
            m.convert_row(self.row(value='', encrypted_value=b'v20unsupported'), 24, b'x'*16)
        with self.assertRaisesRegex(m.MigrationError, '^unsupported-database-version$'):
            m.convert_row(self.row(), 99, None)

    def test_host_cookie_session_and_same_site_remain_restricted(self):
        c = m.convert_row(self.row(host_key='accounts.google.com', name='__Host-fixture', has_expires=0, samesite=2), 24, None)
        self.assertEqual(c.domain, 'accounts.google.com')
        self.assertTrue(c.host_only)
        self.assertEqual(c.expires, -1)
        self.assertEqual(c.same_site, 3)
        self.assertEqual(c.line().split('\t')[1:5], ['1','/','1','1'])

    def test_private_destination_never_overwrites_and_contains_no_repo_metadata(self):
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp).resolve()/'isolated-profile'
            cookie = m.convert_row(self.row(), 24, None)
            m.write_private_profile(dest, [cookie])
            self.assertEqual(dest.stat().st_mode & 0o777, 0o700)
            self.assertEqual((dest/'cookies.txt').stat().st_mode & 0o777, 0o600)
            self.assertEqual(sorted(p.name for p in dest.iterdir()), ['cookies.txt'])
            with self.assertRaises(m.MigrationError):
                m.write_private_profile(dest, [cookie])
            self.assertIn('synthetic-login-fixture', (dest/'cookies.txt').read_text())

    def test_selected_sql_query_does_not_read_unrelated_rows_or_values_on_dry_run(self):
        with tempfile.TemporaryDirectory() as tmp:
            db = Path(tmp)/'Cookies'
            con=sqlite3.connect(db)
            con.execute('CREATE TABLE meta (key TEXT, value TEXT)')
            con.execute("INSERT INTO meta VALUES ('version','24')")
            con.execute('CREATE TABLE cookies (host_key TEXT,name TEXT,path TEXT,is_secure INTEGER,is_httponly INTEGER,expires_utc INTEGER,has_expires INTEGER,samesite INTEGER,top_frame_site_key TEXT,source_scheme INTEGER,source_port INTEGER,value TEXT,encrypted_value BLOB)')
            for row in [self.row(), self.row(host_key='.unrelated.test')]:
                con.execute('INSERT INTO cookies VALUES ('+','.join('?'*len(row))+')', list(row.values()))
            con.commit();con.close()
            version, rows=m.read_selected_rows(db, include_values=False)
            self.assertEqual(version,24)
            self.assertEqual(len(rows),1)
            self.assertNotIn('value',rows[0])
            self.assertNotIn('encrypted_value',rows[0])
            self.assertNotIn('synthetic-login-fixture',json.dumps(m.metadata_summary(rows)))

    def test_keychain_denial_is_not_retried_or_exposed(self):
        class Process:
            returncode=1
            stdout=b''
            stderr=b'private diagnostic that must not appear'
        with patch.object(m.sys, 'platform', 'darwin'), patch.object(m.subprocess, 'run', return_value=Process()) as call:
            with self.assertRaisesRegex(m.MigrationError, '^keychain-access-pending-or-denied$'):
                m.read_normal_keychain_password()
            self.assertEqual(call.call_count,1)
            self.assertEqual(call.call_args.kwargs['timeout'],25)

    def test_duplicate_cookie_scopes_are_not_widened(self):
        first=m.convert_row(self.row(),24,None)
        second=m.convert_row(self.row(value='second-synthetic-fixture'),24,None)
        accepted, duplicate_count=m.remove_ambiguous_duplicates([first,second])
        self.assertEqual(accepted,[])
        self.assertEqual(duplicate_count,2)


if __name__ == '__main__':
    unittest.main()
