#!/usr/bin/env python3
"""Focused assessor/ownership regressions; not browser capability evidence."""
import copy,json,os,time,unittest
from pathlib import Path
from unittest.mock import patch
import run_html5_event_diagnostic02 as case
import html5_event_bundle_case02 as helper


def fixture():
    rows=[]
    for kind in ('png','webp','jxl','avif','heic','invalid'):
        for mode in ('attached','detached'):
            rows.append({'family':'image','id':kind+'-'+mode,'terminal':'load' if kind=='png' else 'error',
                         'naturalWidth':1 if kind=='png' else 0,'naturalHeight':1 if kind=='png' else 0})
    for kind in ('text','document','arraybuffer','blob'):
        response={'status':200,'responseType':kind,'bodyMatched':True,'textLength':len(case.BODY),
                  'documentPresent':True,'title':'&&<','arrayBuffer':True,'blob':True,'byteLength':len(case.BODY),'mimeType':'text/html'}
        rows.append({'family':'xhr','id':kind,'terminal':'readystatechange-done','upstreamWouldSettle':True,
                     'readyState4Observed':True,'response':response})
    rows.append({'family':'idb','id':'delete-open-store-cleanup','upstreamWouldSettle':True,'connectionClosed':True,'cleanupDeleteSucceeded':True})
    return {'records':rows},dict.fromkeys(('text','document','arraybuffer','blob'),1)


class Acceptance(unittest.TestCase):
    def test_unsupported_image_error_settles_but_never_decodes_or_scores(self):
        value,requests=fixture();result=case.assess(value,requests)
        self.assertTrue(result['allExpectedCasesObserved'])
        self.assertTrue(result['allUpstreamBackgroundFamiliesWouldSettle'])
        self.assertEqual(result['imageCases']['heic-detached']['terminal'],'error')
        self.assertIsNot(result['imageCases']['heic-detached']['decoderAccepted'],True)
        self.assertIsNone(result['benchmarkScore']);self.assertFalse(result['html5TargetAcceptanceClaimed'])

    def test_exact_missing_callback_is_reported_as_a_blocking_family(self):
        value,requests=fixture();next(r for r in value['records'] if r['id']=='webp-detached')['terminal']='observer-deadline'
        result=case.assess(value,requests)
        self.assertTrue(result['allExpectedCasesObserved']);self.assertEqual(result['blockingFamilies'],['detached-image-events'])
        self.assertFalse(result['allUpstreamBackgroundFamiliesWouldSettle'])

    def test_real_typed_response_and_independent_request_are_both_required(self):
        value,requests=fixture();next(r for r in value['records'] if r['id']=='document')['response']['title']='wrong'
        requests['blob']=0;result=case.assess(value,requests)
        self.assertFalse(result['xhrCases']['document']['typedResponseCorrect'])
        self.assertFalse(result['xhrCases']['blob']['typedResponseCorrect'])
        self.assertTrue(result['xhrCases']['arraybuffer']['typedResponseCorrect'])

    def test_late_process_requires_expected_uid_start_and_exact_executable(self):
        now=time.time();exe=Path('/owned/candidate/Lumen.app/Contents/MacOS/lumen')
        timestamp=time.strftime('%a %b %d %H:%M:%S %Y',time.localtime(now))
        expected=str(os.getuid())+' '+timestamp+' '+str(exe)
        for value,accepted in [(expected,True),(str(os.getuid()+1)+' '+timestamp+' '+str(exe),False),(str(os.getuid())+' '+timestamp+' /other/lumen',False)]:
            with patch.object(helper,'process_identity',return_value=value):
                self.assertEqual(helper.claimed_identity(55,exe,now) is not None,accepted)
        with patch.object(helper,'process_identity',return_value=expected):
            self.assertIsNone(helper.claimed_identity(55,exe,now+10))

    def test_ambiguous_late_processes_are_not_adopted(self):
        with patch.object(helper,'matching_pids',return_value=[55,56]):
            with self.assertRaisesRegex(RuntimeError,'Ambiguous'):
                helper.late_owned_process(Path('/owned/lumen'),time.time())


    def test_ordered_console_rows_reconstruct_the_exact_server_object(self):
        value,_=fixture();value.update(schemaVersion=1,stage='completed',nonce='abcdef012345abcdef012345',benchmarkExecuted=False)
        header={k:v for k,v in value.items() if k!='records'};header['recordCount']=17
        lines=['[js log] LUME_HTML5_EVENT_ROW '+json.dumps({'schemaVersion':1,'nonce':value['nonce'],'index':i,'record':row}) for i,row in enumerate(value['records'])]
        lines.append('[js log] LUME_HTML5_EVENTS '+json.dumps(header))
        self.assertEqual(helper.reconstruct_event_report(lines,value['nonce']),[value])
        self.assertEqual(helper.reconstruct_event_report(lines[:-2]+lines[-1:],value['nonce']),[])
        self.assertEqual(helper.reconstruct_event_report(lines,value['nonce']+'wrong'),[])

    def test_duplicate_reordered_or_truncated_console_rows_cannot_pass(self):
        value,_=fixture();nonce='abcdef012345abcdef012345'
        rows=['LUME_HTML5_EVENT_ROW '+json.dumps({'schemaVersion':1,'nonce':nonce,'index':i,'record':r}) for i,r in enumerate(value['records'])]
        header='LUME_HTML5_EVENTS '+json.dumps({'schemaVersion':1,'stage':'completed','nonce':nonce,'recordCount':17})
        self.assertEqual(helper.reconstruct_event_report([rows[1],rows[0]]+rows[2:]+[header],nonce),[])
        self.assertEqual(helper.reconstruct_event_report(rows+[rows[0],header],nonce),[])
        self.assertEqual(helper.reconstruct_event_report([rows[0][:-1]]+rows[1:]+[header],nonce),[])

if __name__=='__main__':unittest.main()
