import json
from pathlib import Path
import unittest
import public_video_gesture_case as original
import public_video_gesture_diagnostic_case as revised
from test_public_video_gesture_evidence import evidence

class Diagnostics(unittest.TestCase):
    def test_error_phase_and_numeric_code_are_retained_without_raw_data(self):
        source={'schema':1,'phase':'navigate','failed':True,'cdpMethod':'Page.navigate','cdpErrorCode':-32000,
            'contextTransition':True,'exceptionClass':None,'errorKind':'cdp-error','observerCompleted':False,'cleanupFailed':False,
            'rawError':'private token','url':'https://private.invalid/','profile':'private identity'}
        kind,row=revised.sanitize_line('LUME_PUBLIC_VIDEO_DIAGNOSTIC '+json.dumps(source))
        self.assertEqual(kind,'diagnostic');self.assertEqual(row['phase'],'navigate');self.assertEqual(row['cdpErrorCode'],-32000)
        self.assertNotIn('private',json.dumps(row));self.assertNotIn('rawError',row)
    def test_unknown_phase_or_exception_text_is_not_retained(self):
        _,row=revised.sanitize_line('LUME_PUBLIC_VIDEO_DIAGNOSTIC '+json.dumps({'schema':1,'phase':'private token','exceptionClass':'private user','errorKind':'private error','cdpMethod':'private URL','cdpErrorCode':True}))
        self.assertEqual(row,{'schema':1});self.assertIsNone(revised.sanitize_line('raw private error'))
    def test_acceptance_input_dispatch_and_original_sanitizing_are_unchanged(self):
        row,item=evidence()
        for marker,value in [('LUME_PUBLIC_VIDEO_GESTURE ',row),('LUME_PUBLIC_VIDEO_INPUT ',item)]:
            self.assertEqual(original.sanitize_line(marker+json.dumps(value)),revised.sanitize_line(marker+json.dumps(value)))
        self.assertEqual(original.assess([row],[item]),revised.assess([row],[item]))
        row['totalFrameCountDelta']=0
        self.assertEqual(original.assess([row],[item]),revised.assess([row],[item]));self.assertFalse(revised.assess([row],[item])['videoFrameProgressVerified'])
        base=Path(__file__).parent
        def dispatch(name):
            text=(base/name).read_text();return text[text.index('export async function dispatchSinglePlayClick'):text.index('\nasync function main()')]
        self.assertEqual(dispatch('public_video_gesture_chrome.mjs'),dispatch('public_video_gesture_chrome_diagnostic.mjs'))

if __name__=='__main__':unittest.main()
