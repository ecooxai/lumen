/* Exercise the real presentation pump without decoder threads or network I/O. */
#include "../src/media/media.c"
#include <assert.h>
static void enqueue(MediaPlayer *m, double pts, bool hidden) {
    assert(m->vqn < VQ_MAX);
    m->vq[m->vqn++] = (VFrame){pts,tiny_image(),640,360,hidden};
}
int main(void) {
    assert(!media_image_serial(NULL));
    MediaPlayer *m=mp_new(NULL);
    uint64_t state_version=media_state_version();
    media_tick();assert(media_state_version()>state_version);
    state_version=media_state_version();
    for(int i=0;i<20;i++)media_tick();
    assert(media_state_version()==state_version); /* NaN duration remains idle. */
    mp_set_duration(m,10);media_tick();assert(media_state_version()>state_version);
    state_version=media_state_version();
    mp_set_duration(m,10);media_tick();assert(media_state_version()==state_version);
    Stream stream={.m=m,.mime="video/mp4",.opened=true,.has_video=true};
    m->st[0]=&stream;m->nst=1;m->playing=true;m->base_t=.1;m->base_wall=now_ms();
    enqueue(m,0,false);enqueue(m,.01,false);enqueue(m,.02,false);
    assert(media_tick()&1);
    MpState s;mp_state(m,&s);
    assert(s.presented_frames==1 && s.dropped_frames==2 && s.frame_pts==.02);
    assert(s.frame_w==640 && s.frame_h==360 && s.frame_serial==media_frame_version());
    uint64_t serial=s.frame_serial;double wall=s.frame_wall;
    assert(serial && media_image_serial(m->cur)==serial);
    Image *previous=m->cur;
    state_version=media_state_version();
    mp_pause(m);for(int i=0;i<20;i++)media_tick();mp_state(m,&s);
    assert(media_state_version()>state_version);
    state_version=media_state_version();
    for(int i=0;i<20;i++)media_tick();assert(media_state_version()==state_version);
    assert(s.presented_frames==1 && s.dropped_frames==2 && s.frame_serial==serial && s.frame_wall==wall);
    assert(media_image_serial(m->cur)==serial);
    /* Seeking while paused submits one new frame without resetting quality. */
    mp_seek(m,2);enqueue(m,2,false);media_tick();mp_state(m,&s);
    assert(media_state_version()>state_version); /* Same-pump paused seek completes. */
    assert(!s.seeking && s.paused && s.presented_frames==2 && s.frame_pts==2 && s.frame_serial>serial);
    assert(media_image_serial(m->cur)==s.frame_serial && !media_image_serial(previous));
    serial=s.frame_serial;
    /* Low-memory placeholders are not real presented video frames. */
    mp_seek(m,3);enqueue(m,3,true);media_tick();mp_state(m,&s);
    assert(s.presented_frames==2 && s.dropped_frames==3 && s.frame_serial==0);
    assert(media_frame_version()==serial);
    assert(!media_image_serial(m->cur));
    /* The decoder has not yet restarted after replay. Its old EOF must not
       complete this new seek or immediately end the new playback. */
    stream.eof=true;stream.rd_epoch=m->epoch;
    m->ended=true;m->playing=false;
    mp_play(m);media_tick();mp_state(m,&s);
    assert(s.seeking && !s.ended && !s.paused && s.time==0);
    stream.rd_epoch=m->epoch;stream.eof=false;enqueue(m,0,false);media_tick();mp_state(m,&s);
    assert(!s.seeking && !s.ended && !s.paused);
    state_version=media_state_version();
    set_error(m,"test decode error");media_tick();assert(media_state_version()>state_version);
    m->nst=0;mp_free(m);
    m=mp_new(NULL);mp_state(m,&s);assert(!s.presented_frames && !s.dropped_frames && !s.frame_serial);
    enqueue(m,0,false);media_tick();assert(media_image_serial(m->cur)>serial);mp_free(m);
    puts("PASS presentation/drop accounting, paused idle, seek, hidden frames and reload");
}
