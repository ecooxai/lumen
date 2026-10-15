/* Exercise the real presentation pump without decoder threads or network I/O. */
#include "../src/media/media.c"
#include <assert.h>
static void enqueue(MediaPlayer *m, double pts, bool hidden) {
    assert(m->vqn < VQ_MAX);
    m->vq[m->vqn++] = (VFrame){pts,tiny_image(),640,360,hidden};
}
int main(void) {
    MediaPlayer *m=mp_new(NULL);
    Stream stream={.m=m,.mime="video/mp4",.opened=true,.has_video=true};
    m->st[0]=&stream;m->nst=1;m->playing=true;m->base_t=.1;m->base_wall=now_ms();
    enqueue(m,0,false);enqueue(m,.01,false);enqueue(m,.02,false);
    assert(media_tick()&1);
    MpState s;mp_state(m,&s);
    assert(s.presented_frames==1 && s.dropped_frames==2 && s.frame_pts==.02);
    assert(s.frame_w==640 && s.frame_h==360 && s.frame_serial==media_frame_version());
    uint64_t serial=s.frame_serial;double wall=s.frame_wall;
    mp_pause(m);for(int i=0;i<20;i++)media_tick();mp_state(m,&s);
    assert(s.presented_frames==1 && s.dropped_frames==2 && s.frame_serial==serial && s.frame_wall==wall);
    /* Seeking while paused submits one new frame without resetting quality. */
    mp_seek(m,2);enqueue(m,2,false);media_tick();mp_state(m,&s);
    assert(!s.seeking && s.paused && s.presented_frames==2 && s.frame_pts==2 && s.frame_serial>serial);
    serial=s.frame_serial;
    /* Low-memory placeholders are not real presented video frames. */
    mp_seek(m,3);enqueue(m,3,true);media_tick();mp_state(m,&s);
    assert(s.presented_frames==2 && s.dropped_frames==3 && s.frame_serial==0);
    assert(media_frame_version()==serial);
    m->nst=0;mp_free(m);
    m=mp_new(NULL);mp_state(m,&s);assert(!s.presented_frames && !s.dropped_frames && !s.frame_serial);mp_free(m);
    puts("PASS presentation/drop accounting, paused idle, seek, hidden frames and reload");
}
