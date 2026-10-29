#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "paint_profile.h"

static PaintFrame sample(void) {
    PaintFrame frame = {0};
    for (int phase = 0; phase < PAINT_PHASE_COUNT; phase++) frame.phase_ms[phase] = phase + 1;
    frame.last_ms = 121;
    return frame;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    PaintProfile profile = {0};
    PaintFrame frame = sample();
    if (!strcmp(argv[1], "disabled")) {
        assert(!paint_profile_record(&profile, &frame, 100, false));
        assert(!profile.started && profile.frames == 0);
        for (int phase = 0; phase < PAINT_PHASE_COUNT; phase++) assert(profile.phase_ms[phase] == 0);
    } else if (!strcmp(argv[1], "counts")) {
        profile.enabled = true;
        assert(!paint_profile_record(&profile, &frame, 100, false));
        frame.blit = true;
        frame.last_ms = 221;
        assert(!paint_profile_record(&profile, &frame, 200, true));
        assert(profile.frames == 2 && profile.partial == 1 && profile.blits == 1);
        for (int phase = 0; phase < PAINT_PHASE_COUNT; phase++) assert(profile.phase_ms[phase] == 2 * (phase + 1));
    } else if (!strcmp(argv[1], "interval")) {
        profile.enabled = true;
        assert(!paint_profile_record(&profile, &frame, 100, true));
        frame.last_ms = 1099;
        assert(!paint_profile_record(&profile, &frame, 1078, true));
        frame.last_ms = 1100;
        assert(paint_profile_record(&profile, &frame, 1079, true));
        assert(profile.start_ms == 100 && profile.frames == 3);
    } else if (!strcmp(argv[1], "format")) {
        profile.enabled = true;
        paint_profile_record(&profile, &frame, 100, true);
        char text[1024];
        int length = paint_profile_format(&profile, 5100, text, sizeof text);
        assert(length > 0 && length < (int)sizeof text);
        assert(strstr(text, "paint wall interval_ms=5000.0"));
        assert(strstr(text, "frames=1 partial=1 full=0 blits=0"));
        assert(strstr(text, "prepare_ms=1.000 layout_dl_ms=2.000 signatures_ms=3.000"));
        assert(strstr(text, "raster_ms=4.000 compose_ms=5.000 present_ms=6.000"));
        assert(!strstr(text, "cpu") && !strstr(text, "fps") && !strstr(text, "ms/s"));
        char tiny[8];
        memset(tiny, 'X', sizeof tiny);
        assert(paint_profile_format(&profile, 5100, tiny, sizeof tiny) == length);
        assert(tiny[sizeof tiny - 1] == 0);
    } else if (!strcmp(argv[1], "reset")) {
        profile.enabled = true;
        paint_profile_record(&profile, &frame, 100, true);
        paint_profile_reset(&profile, 1100);
        assert(profile.enabled && profile.started && profile.start_ms == 1100);
        assert(!profile.frames && !profile.partial && !profile.blits);
        for (int phase = 0; phase < PAINT_PHASE_COUNT; phase++) assert(profile.phase_ms[phase] == 0);
        frame.last_ms = 2100;
        assert(paint_profile_record(&profile, &frame, 2079, false));
        assert(profile.frames == 1 && profile.partial == 0);
    } else {
        return 2;
    }
    return 0;
}
