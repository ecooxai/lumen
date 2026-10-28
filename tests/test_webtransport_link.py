#!/usr/bin/env python3
"""Installed provenance checks; these fixtures do not model network transport."""
from pathlib import Path
import hashlib,json,subprocess,sys,tempfile,unittest

HELPER=Path(__file__).resolve().parents[1]/'tools/prepare-webtransport-link.py'

class LinkProvenance(unittest.TestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory();self.base=Path(self.directory.name)
        self.prefix=self.base/'dependency';self.archive=self.prefix/'lib/liblumen_webtransport_native.a'
        self.public_header=self.prefix/'include/lumen_wt.h'
        for path,content in [(self.archive,b'artifact-integrity-fixture'),(self.public_header,b'header-integrity-fixture')]:
            path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(content)
        self.manifest={'backend':'wtransport-0.7.2','version':'0.7.2','target':'aarch64-apple-darwin','archiveSHA256':hashlib.sha256(self.archive.read_bytes()).hexdigest(),'headerSHA256':hashlib.sha256(self.public_header.read_bytes()).hexdigest(),'nativeLinkArgs':['-lSystem','-framework','Security']}
        self.header=self.base/'config.h';self.response=self.base/'link.rsp';self.save()
    def tearDown(self):self.directory.cleanup()
    def save(self):(self.prefix/'manifest.json').write_text(json.dumps(self.manifest))
    def run_helper(self,enabled='1'):
        return subprocess.run([sys.executable,str(HELPER),'--prefix',str(self.prefix),'--header',str(self.header),'--response',str(self.response),'--platform','macos','--enabled',enabled],capture_output=True,text=True)
    def test_matching_provenance_is_content_stable(self):
        self.assertEqual(self.run_helper().returncode,0)
        stamps=(self.header.stat().st_mtime_ns,self.response.stat().st_mtime_ns)
        self.assertIn('LUMEN_WEBTRANSPORT_NATIVE 1',self.header.read_text())
        self.assertEqual(self.response.read_text(),'"-lSystem"\n"-framework"\n"Security"\n')
        self.assertEqual(self.run_helper().returncode,0)
        self.assertEqual(stamps,(self.header.stat().st_mtime_ns,self.response.stat().st_mtime_ns))
    def test_modified_archive_cannot_enable_a_backend(self):
        self.archive.write_bytes(b'modified-after-build')
        self.assertNotEqual(self.run_helper().returncode,0)
        self.assertFalse(self.header.exists());self.assertFalse(self.response.exists())
        self.assertEqual(self.run_helper('0').returncode,0)
        self.assertEqual(self.header.read_text(),'#define LUMEN_WEBTRANSPORT_NATIVE 0\n')
        self.assertEqual(self.response.read_text(),'')
    def test_platform_mismatch_is_rejected(self):
        self.manifest['target']='aarch64-apple-ios-sim';self.save()
        self.assertNotEqual(self.run_helper().returncode,0);self.assertFalse(self.header.exists())
    def test_native_link_arguments_cannot_inject_a_new_response_line(self):
        self.manifest['nativeLinkArgs']=['-framework\nOther'];self.save()
        self.assertNotEqual(self.run_helper().returncode,0);self.assertFalse(self.header.exists())

if __name__=='__main__':unittest.main()
