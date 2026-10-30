import copy
import json
import unittest
from public_video_gesture_case import assess, sanitize_line

def evidence():
    row={'schema':1,'probeRevision':1,'state':'video-frame-progress-observed','targetPageMatched':True,'videoPresent':True,
        'videoElementChanged':False,'adPlaybackObserved':False,'clickArmed':True,'trustedControlClickCount':1,'untrustedControlClickCount':0,
        'trustedClickElapsedMs':15000,'playingAfterTrustedClick':True,'selectedControl':'large-play','programmaticPlayCalled':False,
        'readyState':4,'mediaErrorCode':None,'paused':True,'muted':True,'videoWidth':854,'videoHeight':480,'videoSourceKind':'blob',
        'muteWriteFailureCount':0,'currentTimeDeltaSeconds':5,'playbackProgressSamples':5,'totalFrameCountDelta':124,'droppedFrameCountDelta':1,
        'nonDroppedFrameCountDelta':123,'videoFrameCounter':'getVideoPlaybackQuality','documentHidden':False,'resourcesReleased':True,
        'accountActionsPerformed':False,'physicalInputVerified':False,'eventTimeline':[
            {'type':'control-click','elapsedMs':15000,'isTrusted':True,'cleanup':False},
            {'type':'playing','elapsedMs':15001,'isTrusted':True,'cleanup':False}]}
    item={'schema':1,'method':'Input.dispatchMouseEvent','control':'large-play','clicksRequested':1,'pressRequested':True,
        'releaseRequested':True,'pressAcknowledged':True,'releaseAcknowledged':True,'programmaticPlayCalled':False,'physicalInputVerified':False}
    return row,item

class GestureEvidence(unittest.TestCase):
    def test_full_typed_control_and_progress_proof(self):
        row,item=evidence();self.assertTrue(assess([row],[item])['videoFrameProgressVerified'])
    def test_zero_frames_and_inconsistent_arithmetic_fail(self):
        for changes in ({'totalFrameCountDelta':0,'droppedFrameCountDelta':0,'nonDroppedFrameCountDelta':0},{'nonDroppedFrameCountDelta':124}):
            row,item=evidence();row.update(changes);self.assertFalse(assess([row],[item])['videoFrameProgressVerified'])
    def test_single_acknowledged_input_required(self):
        row,item=evidence()
        for changes in ({'pressAcknowledged':False},{'releaseAcknowledged':False},{'clicksRequested':2},{'method':'none'},{'control':'transport-play'}):
            bad={**item,**changes};self.assertFalse(assess([row],[bad])['videoFrameProgressVerified'])
        self.assertFalse(assess([row],[item,item])['videoFrameProgressVerified'])
    def test_trusted_click_and_postclick_playing_are_required(self):
        row,item=evidence()
        for key,value in (('trustedControlClickCount',0),('untrustedControlClickCount',1),('playingAfterTrustedClick',False),('trustedClickElapsedMs',16000)):
            bad=copy.deepcopy(row);bad[key]=value;self.assertFalse(assess([bad],[item])['videoFrameProgressVerified'])
        bad=copy.deepcopy(row);bad['eventTimeline'][1]['isTrusted']=False
        self.assertFalse(assess([bad],[item])['videoFrameProgressVerified'])
    def test_cleanup_mute_identity_and_real_thresholds_remain_required(self):
        row,item=evidence()
        for key,value in (('resourcesReleased',False),('muted',False),('muteWriteFailureCount',1),('videoElementChanged',True),('adPlaybackObserved',True),('mediaErrorCode',2),('currentTimeDeltaSeconds',.329),('playbackProgressSamples',1),('programmaticPlayCalled',True)):
            bad={**row,key:value};self.assertFalse(assess([bad],[item])['videoFrameProgressVerified'])
    def test_private_strings_and_unrecognized_fields_are_not_retained(self):
        row,item=evidence();row.update(cookie='private',pageTitle='private',currentURL='https://private.invalid/')
        row['eventTimeline'][0].update(cookie='private',url='https://private.invalid/')
        clean=sanitize_line('LUME_PUBLIC_VIDEO_GESTURE '+json.dumps(row))
        self.assertIsNotNone(clean);self.assertNotIn('private',json.dumps(clean))
        self.assertTrue(assess([clean[1]],[item])['videoFrameProgressVerified'])
    def test_malformed_or_nonfinite_or_unbounded_evidence_rejected(self):
        self.assertIsNone(sanitize_line('unmarked private text'))
        self.assertIsNone(sanitize_line('LUME_PUBLIC_VIDEO_GESTURE '+json.dumps({'schema':True})))
        self.assertIsNone(sanitize_line('LUME_PUBLIC_VIDEO_GESTURE '+('x'*8192)))
        row,item=evidence();row['currentTimeDeltaSeconds']=float('nan');row['eventTimeline']*=13
        clean=sanitize_line('LUME_PUBLIC_VIDEO_GESTURE '+json.dumps(row))[1]
        self.assertNotIn('eventTimeline',clean);self.assertNotIn('currentTimeDeltaSeconds',clean)
        self.assertFalse(assess([clean],[item])['videoFrameProgressVerified'])

if __name__=='__main__':unittest.main()
