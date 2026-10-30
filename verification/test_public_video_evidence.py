import json
import unittest
from public_video_case import sanitize_video, assess_video


class EvidenceTest(unittest.TestCase):
    def completed(self):
        return dict(schema=1,state='video-frame-progress-observed',targetPageMatched=True,videoElementChanged=False,adPlaybackObserved=False,accountActionsPerformed=False,playRejected=False,paused=True,muted=True,currentTimeDeltaSeconds=8,totalFrameCountDelta=194,droppedFrameCountDelta=2,nonDroppedFrameCountDelta=192,playbackProgressSamples=8,videoWidth=640,videoHeight=360,mediaErrorCode=None,videoFrameCounter='getVideoPlaybackQuality',resourcesReleased=True)

    def test_observed_time_alone_cannot_pass(self):
        row=self.completed();row['nonDroppedFrameCountDelta']=None
        self.assertFalse(assess_video([row])['videoFrameProgressVerified'])

    def test_resource_release_is_required(self):
        row=self.completed();row['resourcesReleased']=False
        self.assertFalse(assess_video([row])['videoFrameProgressVerified'])

    def test_nominal_counter_gate_remains_distinct_from_compositor(self):
        result=assess_video([self.completed()])
        self.assertTrue(result['videoFrameProgressVerified'])
        self.assertFalse(result['actualCompositorFramesVerified'])

    def test_dropped_frames_and_advertisements_cannot_pass_as_movie_progress(self):
        row=self.completed();row.update(droppedFrameCountDelta=194,nonDroppedFrameCountDelta=0)
        self.assertFalse(assess_video([row])['videoFrameProgressVerified'])
        row=self.completed();row['adPlaybackObserved']=True
        self.assertFalse(assess_video([row])['videoFrameProgressVerified'])

    def test_redactor_drops_untyped_and_unknown_content(self):
        row=self.completed();row.update(secret='never retained',state='unexpected raw page string',videoWidth='not a number')
        safe=sanitize_video('LUME_PUBLIC_VIDEO '+json.dumps(row))
        self.assertNotIn('secret',safe);self.assertNotIn('state',safe);self.assertNotIn('videoWidth',safe)

    def test_redactor_refuses_raw_lines_and_invalid_json(self):
        self.assertIsNone(sanitize_video('raw output'))
        self.assertIsNone(sanitize_video('LUME_PUBLIC_VIDEO {'))

    def test_native_console_envelope_retains_the_same_typed_evidence_as_chrome(self):
        row=self.completed();row['secret']='never retained'
        raw='LUME_PUBLIC_VIDEO '+json.dumps(row)
        native=sanitize_video('[js log] '+raw)
        self.assertIsNotNone(native)
        self.assertEqual(native,sanitize_video(raw))
        self.assertNotIn('secret',native)

    def test_unrecognized_console_envelopes_cannot_smuggle_markers(self):
        raw='LUME_PUBLIC_VIDEO '+json.dumps(self.completed())
        for prefix in ('raw ', '[js error] ', '[js log] [js log] ', 'account: '):
            with self.subTest(prefix=prefix):
                self.assertIsNone(sanitize_video(prefix+raw))

    def test_full_native_line_size_bound_includes_the_console_envelope(self):
        raw='[js log] LUME_PUBLIC_VIDEO '+json.dumps(self.completed())
        self.assertIsNone(sanitize_video(raw+' '*(4097-len(raw))))

    def test_wrong_schema_types_are_rejected(self):
        for schema in (True,1.0,'1',None,2):
            row=self.completed();row['schema']=schema
            with self.subTest(schema=schema):
                self.assertIsNone(sanitize_video('LUME_PUBLIC_VIDEO '+json.dumps(row)))


if __name__=='__main__':unittest.main()
