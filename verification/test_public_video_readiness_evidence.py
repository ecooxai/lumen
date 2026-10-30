import json
import unittest
from public_video_case import assess_video, sanitize_video


class ReadyVideoEvidence(unittest.TestCase):
    def complete(self):
        return dict(schema=1, probeRevision=3, state='video-frame-progress-observed',
                    targetPageMatched=True, videoElementChanged=False,
                    adPlaybackObserved=False, accountActionsPerformed=False,
                    playRejected=False, paused=True, muted=True,
                    currentTimeDeltaSeconds=8, totalFrameCountDelta=194,
                    droppedFrameCountDelta=2, nonDroppedFrameCountDelta=192,
                    playbackProgressSamples=8, videoWidth=640, videoHeight=360,
                    mediaErrorCode=None, videoFrameCounter='getVideoPlaybackQuality',
                    resourcesReleased=True, muteWriteFailureCount=0,
                    readyStateAtPlayRequest=4, videoSourceKind='blob')

    def test_ready_observation_requires_its_actual_readiness_and_mute_readback(self):
        self.assertTrue(assess_video([self.complete()])['videoFrameProgressVerified'])
        for key, value in [('readyStateAtPlayRequest', 0), ('muteWriteFailureCount', 1),
                           ('readyStateAtPlayRequest', None), ('muteWriteFailureCount', None)]:
            with self.subTest(key=key, value=value):
                row = self.complete(); row[key] = value
                self.assertFalse(assess_video([row])['videoFrameProgressVerified'])

    def test_boolean_counters_cannot_hide_a_missing_readback(self):
        for key in ('readyStateAtPlayRequest', 'muteWriteFailureCount'):
            row = self.complete(); row[key] = False
            safe = sanitize_video('LUME_PUBLIC_VIDEO ' + json.dumps(row))
            with self.subTest(key=key):
                self.assertFalse(assess_video([safe])['videoFrameProgressVerified'])

    def test_source_classification_does_not_allow_media_addresses(self):
        row = self.complete()
        safe = sanitize_video('LUME_PUBLIC_VIDEO ' + json.dumps(row))
        self.assertEqual(safe['videoSourceKind'], 'blob')
        row.update(videoSourceKind='https://example.invalid/PRIVATE_TOKEN',
                   currentSrc='https://example.invalid/PRIVATE_TOKEN')
        safe = sanitize_video('LUME_PUBLIC_VIDEO ' + json.dumps(row))
        self.assertNotIn('PRIVATE_TOKEN', json.dumps(safe))
        self.assertNotIn('currentSrc', safe)

    def test_real_rejection_or_zero_frames_still_fail_with_ready_metadata(self):
        for change in ({'playRejected': True}, {'nonDroppedFrameCountDelta': 0,
                                               'totalFrameCountDelta': 0, 'droppedFrameCountDelta': 0}):
            row = self.complete(); row.update(change)
            self.assertFalse(assess_video([row])['videoFrameProgressVerified'])


if __name__ == '__main__':
    unittest.main()
