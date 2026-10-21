import hashlib
import json
from pathlib import Path
import sqlite3
import sys
import tempfile
import time
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import browser_bridge as b
import chrome_to_lumen as m

SCHEMA = 'CREATE TABLE cookies(creation_utc INTEGER NOT NULL,host_key TEXT NOT NULL,top_frame_site_key TEXT NOT NULL,name TEXT NOT NULL,value TEXT NOT NULL,encrypted_value BLOB NOT NULL,path TEXT NOT NULL,expires_utc INTEGER NOT NULL,is_secure INTEGER NOT NULL,is_httponly INTEGER NOT NULL,last_access_utc INTEGER NOT NULL,has_expires INTEGER NOT NULL,is_persistent INTEGER NOT NULL,priority INTEGER NOT NULL,samesite INTEGER NOT NULL,source_scheme INTEGER NOT NULL,source_port INTEGER NOT NULL,last_update_utc INTEGER NOT NULL,source_type INTEGER NOT NULL,has_cross_site_ancestor INTEGER NOT NULL)'


class BridgeTests(unittest.TestCase):
    def row(self, **changes):
        row=dict(creation_utc=1,host_key='.google.com',top_frame_site_key='',name='fixture',value='',encrypted_value=b'v10synthetic-encrypted-fixture',path='/',expires_utc=int((time.time()+3600+m.CHROME_EPOCH)*1e6),is_secure=1,is_httponly=1,last_access_utc=1,has_expires=1,is_persistent=1,priority=1,samesite=1,source_scheme=2,source_port=443,last_update_utc=1,source_type=1,has_cross_site_ancestor=0)
        row.update(changes);return row

    def cdp(self,**changes):
        row=dict(name='fixture',value='synthetic-only',domain='.google.com',path='/',secure=True,httpOnly=True,expires=time.time()+3600,session=False,sameSite='Lax',sourceScheme='Secure',sourcePort=443)
        row.update(changes);return row

    def test_scoped_snapshot_keeps_ciphertext_schema_and_source_unchanged(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp).resolve();source=root/'Cookies';dest=root/'private-chrome'
            con=sqlite3.connect(source);con.execute(SCHEMA);con.execute('CREATE TABLE meta(key LONGVARCHAR NOT NULL UNIQUE PRIMARY KEY, value LONGVARCHAR)')
            con.executemany('INSERT INTO meta VALUES (?,?)',[('version','24'),('last_compatible_version','24')])
            con.execute('CREATE UNIQUE INDEX cookies_unique_index ON cookies(host_key, top_frame_site_key, has_cross_site_ancestor, name, path, source_scheme, source_port)')
            rows=[self.row(),self.row(name='session',has_expires=0,is_persistent=0,expires_utc=0),self.row(host_key='.unrelated.test'),self.row(name='partitioned',top_frame_site_key='https://example.test'),self.row(name='expired',expires_utc=1)]
            for row in rows:con.execute('INSERT INTO cookies VALUES ('+','.join('?' for _ in row)+')',list(row.values()))
            con.commit();con.close();before=hashlib.sha256(source.read_bytes()).digest()
            report=b.clone_scoped_chrome(source,dest)
            self.assertEqual(hashlib.sha256(source.read_bytes()).digest(),before)
            self.assertEqual(report['copiedRows'],2);self.assertEqual(report['sessionCookies'],1);self.assertEqual(report['persistentCookies'],1)
            self.assertNotIn('synthetic',json.dumps(report))
            out=sqlite3.connect(dest/'Default/Cookies');copied=out.execute('SELECT encrypted_value,has_expires,expires_utc FROM cookies ORDER BY name').fetchall()
            self.assertEqual(copied[0][0],rows[0]['encrypted_value']);self.assertEqual(copied[1][1:],(0,0))
            self.assertEqual(dict(out.execute('SELECT key,value FROM meta')),{'version':'24','last_compatible_version':'24'});out.close()
            self.assertEqual(dest.stat().st_mode&0o777,0o700);self.assertEqual((dest/'Default/Cookies').stat().st_mode&0o777,0o600)
            with self.assertRaisesRegex(m.MigrationError,'destination-already-exists'):b.clone_scoped_chrome(source,dest)

    def test_cdp_conversion_preserves_host_scope_flags_and_session_semantics(self):
        cookie=b.convert_cdp(self.cdp(domain='accounts.google.com',name='__Host-fixture',session=True,expires=-1,sameSite='Strict'))
        self.assertTrue(cookie.host_only);self.assertTrue(cookie.secure);self.assertTrue(cookie.http_only);self.assertEqual(cookie.expires,-1);self.assertEqual(cookie.same_site,3)
        source=self.cdp(sameSite='None');persistent=b.convert_cdp(source)
        self.assertFalse(persistent.host_only);self.assertEqual(persistent.same_site,1);self.assertLess(abs(persistent.expires-source['expires']),0.001)

    def test_missing_partitioned_or_unknown_cdp_data_is_rejected(self):
        cases=[self.cdp(partitionKey={'topLevelSite':'https://example.test'}),self.cdp(sameSite='Unknown'),self.cdp(sourceScheme='Unknown'),self.cdp(expires=float('nan')),self.cdp(httpOnly='true')]
        missing=self.cdp();del missing['path'];cases.append(missing)
        for cookie in cases:
            with self.assertRaises(m.MigrationError):b.convert_cdp(cookie)

    def test_validation_returns_only_indices_counts_and_skip_codes(self):
        cookies,report=b.validate_cdp([self.cdp(),self.cdp(domain='.unrelated.test'),self.cdp(name='old',expires=1)])
        self.assertEqual(len(cookies),1);self.assertEqual(report['acceptedIndices'],[0]);self.assertNotIn('synthetic-only',json.dumps(report));self.assertNotIn('google.com',json.dumps(report))


if __name__=='__main__':unittest.main()
