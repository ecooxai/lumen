/* Exercise actual media locks against an SDL stream locked as it is in a callback. */
#include "../src/media/media.c"
#include <assert.h>
static SDL_Semaphore *locked, *go, *done;
static MediaPlayer *player;
static int operation;
static int callback_thread(void *unused) {
    (void)unused;
    SDL_LockAudioStream(player->dev);
    SDL_SignalSemaphore(locked);
    SDL_WaitSemaphore(go);
    /* The UI operation has an opportunity to take the player lock first. */
    SDL_Delay(30);
    audio_cb(player, player->dev, 128, 128);
    SDL_UnlockAudioStream(player->dev);
    return 0;
}
static int ui_thread(void *unused) {
    (void)unused;
    if (operation) mp_seek(player, 2);
    else { MpState state; mp_state(player, &state); assert(state.time >= 0); }
    SDL_SignalSemaphore(done);
    return 0;
}
int main(void) {
    player=mp_new(NULL);
    SDL_AudioSpec spec={SDL_AUDIO_F32,2,OUT_RATE};
    player->dev=SDL_CreateAudioStream(&spec,&spec); assert(player->dev);
    player->expect_audio=true; player->playing=true;
    player->aclock=1; player->audio_queued=.1; player->audio_wall=now_ms();
    locked=SDL_CreateSemaphore(0);go=SDL_CreateSemaphore(0);done=SDL_CreateSemaphore(0);
    for(operation=0;operation<2;operation++) {
        SDL_Thread *cb=SDL_CreateThread(callback_thread,"callback",NULL);
        assert(SDL_WaitSemaphoreTimeout(locked,1000));
        SDL_Thread *ui=SDL_CreateThread(ui_thread,"media-ui",NULL);
        SDL_SignalSemaphore(go);
        if (!SDL_WaitSemaphoreTimeout(done,1000)) { fprintf(stderr,"FAIL lock inversion operation=%d\n",operation); return 1; }
        SDL_WaitThread(ui,NULL);SDL_WaitThread(cb,NULL);
    }
    player->seeking=false;player->base_t=2;player->aclock=2.5;player->audio_queued=.2;player->audio_wall=now_ms();
    MpState before,after;mp_state(player,&before);assert(before.time>=2.3 && before.time<=2.5);
    mp_pause(player);mp_state(player,&before);SDL_Delay(30);mp_state(player,&after);
    assert(before.time==after.time && after.paused);
    SDL_DestroyAudioStream(player->dev);player->dev=NULL;
    /* Exercise real device lifecycle with SDL's dummy output backend. A paused
       player must stop callbacks, and resume must restart queued playback. */
    assert(SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy"));
    Stream stream={0};stream.m=player;stream.mime="audio/mp4";
    player->st[0]=&stream;player->nst=1;
    mp_play(player);media_tick();
    /* SourceBuffer metadata may arrive later: don't discard the audio clock. */
    assert(player->expect_audio);
    stream.has_audio=true;stream.opened=true;
    assert(player->dev && player->audio_device && !SDL_AudioStreamDevicePaused(player->dev));
    SDL_Delay(40);
    mp_pause(player);media_tick();
    assert(!player->audio_device && !SDL_GetAudioStreamDevice(player->dev));
    float samples[64]={0};assert(SDL_PutAudioStreamData(player->dev,samples,sizeof samples));
    int queued=SDL_GetAudioStreamQueued(player->dev);assert(queued>0);
    SDL_LockMutex(player->mu);double wall=player->audio_wall;SDL_UnlockMutex(player->mu);
    SDL_Delay(40);
    assert(SDL_GetAudioStreamQueued(player->dev)==queued);
    SDL_LockMutex(player->mu);assert(player->audio_wall==wall);SDL_UnlockMutex(player->mu);
    mp_play(player);media_tick();
    assert(!SDL_AudioStreamDevicePaused(player->dev));
    SDL_Delay(40);
    SDL_LockMutex(player->mu);assert(player->audio_wall>wall);SDL_UnlockMutex(player->mu);
    stream.eof=true;player->eos=true;media_tick();
    assert(player->ended && !player->audio_device && !SDL_GetAudioStreamDevice(player->dev));
    player->nst=0;
    mp_free(player);SDL_DestroySemaphore(locked);SDL_DestroySemaphore(go);SDL_DestroySemaphore(done);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    puts("PASS callback lock order, paused clock, audio device pause/resume");
}
