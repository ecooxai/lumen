// Read-only device enumeration. No capture object or stream is opened.
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#include <SDL3/SDL.h>
#include <stdio.h>
int main(void) {
    @autoreleasepool {
        SDL_Init(0);
        bool cameraInit = SDL_InitSubSystem(SDL_INIT_CAMERA);
        bool audioInit = SDL_InitSubSystem(SDL_INIT_AUDIO);
        int cameras = 0, inputs = 0;
        SDL_CameraID *cameraIDs = cameraInit ? SDL_GetCameras(&cameras) : NULL;
        SDL_AudioDeviceID *audioIDs = audioInit ? SDL_GetAudioRecordingDevices(&inputs) : NULL;
        NSDictionary *result = @{
            @"sdlVersion": @(SDL_GetVersion()), @"cameraSubsystemInitialized": @(cameraInit),
            @"audioSubsystemInitialized": @(audioInit), @"cameraEnumerationSucceeded": @(cameraIDs != NULL),
            @"audioEnumerationSucceeded": @(audioIDs != NULL), @"osVisibleCameraCount": @(cameras),
            @"osVisibleRecordingDeviceCount": @(inputs),
            @"videoAuthorizationStatus": @([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]),
            @"audioAuthorizationStatus": @([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]),
            @"captureOpened": @NO, @"permissionRequestIssued": @NO,
            @"deviceNamesOrIdentifiersRecorded": @NO,
            @"scope": @"OS-visible enumeration only; physical capture and signal are unverified"
        };
        NSData *data = [NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingSortedKeys error:nil];
        if (data) printf("%.*s\n", (int)data.length, (const char *)data.bytes);
        SDL_free(cameraIDs); SDL_free(audioIDs); SDL_Quit();
        return data ? 0 : 1;
    }
}
