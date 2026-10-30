import json,os,stat,tempfile,time,unittest
from pathlib import Path
from auth_bundle_case import RedactedPipe
from auth_case import BOOL_FIELDS

class PrivateFIFOTests(unittest.TestCase):
    def valid(self):
        return {'schema':1,'site':'google','elapsedMs':5000,'readyState':'complete','providerLoggedInFlag':None,**{key:False for key in BOOL_FIELDS}}
    def test_private_fifo_is_not_a_disk_log_and_only_retains_allowlisted_status(self):
        with tempfile.TemporaryDirectory() as directory:
            evidence=[];pipe=RedactedPipe(directory,evidence)
            self.assertTrue(stat.S_ISFIFO(pipe.path.stat().st_mode))
            self.assertEqual(stat.S_IMODE(pipe.path.stat().st_mode),0o600)
            item=self.valid();item['cookie']='PRIVATE_SENTINEL'
            with pipe.path.open('wb',buffering=0) as writer:
                writer.write(b'raw PRIVATE_SENTINEL account log\n')
                writer.write(('LUME_AUTH_STATUS '+json.dumps(item)+'\n').encode())
            pipe.close()
            self.assertEqual(len(evidence),1);self.assertNotIn('PRIVATE_SENTINEL',json.dumps(evidence))
            self.assertEqual(list(Path(directory).iterdir()),[])
    def test_overlong_line_suffix_cannot_become_an_observation(self):
        with tempfile.TemporaryDirectory() as directory:
            evidence=[];pipe=RedactedPipe(directory,evidence)
            with pipe.path.open('wb',buffering=0) as writer:
                writer.write(b'x'*8193+('LUME_AUTH_STATUS '+json.dumps(self.valid())+'\n').encode())
                writer.write(('LUME_AUTH_STATUS '+json.dumps(self.valid())+'\n').encode())
            pipe.close();self.assertEqual(len(evidence),1)

if __name__=='__main__':unittest.main()
