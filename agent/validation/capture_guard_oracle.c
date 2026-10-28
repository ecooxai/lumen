/* No real acquisition: the pre-main guard is mandatory and audio uses SDL's
 * dummy backend. Tests default/explicit/unknown IDs plus unaffected playback. */
#define _GNU_SOURCE
#include <SDL3/SDL.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    const char *(*version)(void) = dlsym(RTLD_DEFAULT, "lumen_capture_guard_version");
    assert(version && !strcmp(version(), "lumen-capture-refusal-v1"));
    assert(SDL_InitSubSystem(SDL_INIT_AUDIO));
    assert(!strcmp(SDL_GetCurrentAudioDriver(), "dummy"));
    SDL_AudioSpec spec = { .format=SDL_AUDIO_F32, .channels=1, .freq=48000 };
    unsigned checked=0;
    assert(!SDL_OpenCamera(1, NULL)); checked++;
    assert(!SDL_OpenCamera(0, NULL)); checked++;
    assert(!SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec)); checked++;
    assert(!SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, NULL, NULL)); checked++;
    assert(!SDL_OpenAudioDevice(0, &spec)); checked++;
    assert(!SDL_OpenAudioDeviceStream(0, &spec, NULL, NULL)); checked++;
    int n=0; SDL_AudioDeviceID *ids=SDL_GetAudioRecordingDevices(&n);
    assert(ids && n>0);
    for(int i=0;i<n;i++) {
        assert(!SDL_IsAudioDevicePlayback(ids[i]));
        assert(!SDL_OpenAudioDevice(ids[i], &spec)); checked++;
        assert(!SDL_OpenAudioDeviceStream(ids[i], &spec, NULL, NULL)); checked++;
    }
    SDL_free(ids);
    SDL_AudioDeviceID dev=SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    assert(dev); checked++; SDL_CloseAudioDevice(dev);
    SDL_AudioStream *stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    assert(stream); checked++; SDL_DestroyAudioStream(stream);
    SDL_Quit();
    printf("capture guard oracle: %u checks passed; dummy playback only\n",checked);
    return 0;
}
