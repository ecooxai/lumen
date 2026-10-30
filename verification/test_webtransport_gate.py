import copy,unittest
from webtransport_gate import assess,REQUIRED
class GateTest(unittest.TestCase):
    def setUp(self):
        self.summary={'workload_complete':True,'observed_browser_samples':35,'lab_results':{'webtransport':{'state':'pass','evidence':{**{key:True for key in REQUIRED},'quicVerified':True,'bytes':53,'streamBytes':51}}},'post_stop_evidence':{'resources':{'activeScopes':0,'registeredCleanups':0,'activeMediaTracks':0}}}
        self.contract={'capabilities':{'webTransport':True,'webTransportBackend':'wtransport-0.7.2','webTransportBYOB':False,'webTransportGracefulRemoteClose':False},'capabilitiesFrozen':True,'propertyReadOnly':True,'constructorPresent':True}
        self.events=[{'event':'datagram','echoBytes':53,'transport':'h3-quic','time':3},{'event':'stream','echoBytes':51,'transport':'h3-quic','time':4},{'event':'stream','echoBytes':0,'transport':'h3-quic','time':4}]
    def run_gate(self):return assess(self.summary,self.contract,self.events,2,5,True)['passed']
    def test_complete_real_evidence(self):self.assertTrue(self.run_gate())
    def test_no_server_events_cannot_pass(self):self.events=[];self.assertFalse(self.run_gate())
    def test_additional_client_traffic_cannot_be_attributed(self):self.events.append({'event':'datagram','echoBytes':1,'transport':'h3-quic','time':4});self.assertFalse(self.run_gate())
    def test_missing_eof_cannot_pass(self):self.summary['lab_results']['webtransport']['evidence']['streamFinished']=False;self.assertFalse(self.run_gate())
    def test_leaked_resource_cannot_pass(self):self.summary['post_stop_evidence']['resources']['activeScopes']=1;self.assertFalse(self.run_gate())
    def test_inactive_backend_cannot_pass(self):self.contract['capabilities']['webTransport']=False;self.assertFalse(self.run_gate())
    def test_mutable_declaration_cannot_pass(self):self.contract['capabilitiesFrozen']=False;self.assertFalse(self.run_gate())
    def test_stale_events_cannot_pass(self):self.events[0]['time']=1;self.assertFalse(self.run_gate())
    def test_changed_source_cannot_pass(self):self.assertFalse(assess(self.summary,self.contract,self.events,2,5,False)['passed'])
if __name__=='__main__':unittest.main()
