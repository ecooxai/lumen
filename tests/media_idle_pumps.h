/* Standalone media lock/presentation fixtures do not start camera, screen,
 * recording, WebGL or WebGPU providers. Supply their idle pump boundary here;
 * these fixtures test real media.c state/locks, not provider implementations.
 * Network cancellation is forbidden: these fixtures never open a URL. */
#ifndef LUMEN_TEST_MEDIA_IDLE_PUMPS_H
#define LUMEN_TEST_MEDIA_IDLE_PUMPS_H
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
static unsigned media_test_idle_pumps[5];
void cam_tick(void) { ++media_test_idle_pumps[0]; }
void screen_tick(void) { ++media_test_idle_pumps[1]; }
void rec_tick(void) { ++media_test_idle_pumps[2]; }
void gl_tick(void) { ++media_test_idle_pumps[3]; }
void webgpu_tick(void) { ++media_test_idle_pumps[4]; }
void net_cancel(uint64_t id) { (void)id; abort(); }
static void media_test_check_idle_pumps(void) {
    for (unsigned i = 0; i < 5; ++i) {
        assert(media_test_idle_pumps[i] > 0);
        assert(media_test_idle_pumps[i] == media_test_idle_pumps[0]);
    }
}
#endif
