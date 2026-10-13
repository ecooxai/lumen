#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <webgpu.h>
#include <wgpu.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #condition); return 1; } } while (0)
static char g_pref[1024] = "./";
static int g_pixel_scale_pct = 200;
static bool g_lowmem, g_vctl, g_tab_sleep, g_hud, g_smooth;
static int g_cpu_on, g_cpu_pct, g_cpu_secs, g_cpu_lim, g_ua;
#include "renderer_functions.h"

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "flags")) {
        CHECK(gpu_backend_flags(GPU_BACKEND_AUTO) == WGPUInstanceBackend_Primary);
        CHECK(gpu_backend_flags(GPU_BACKEND_VULKAN) == WGPUInstanceBackend_Vulkan);
        CHECK(gpu_backend_flags(GPU_BACKEND_GL) == WGPUInstanceBackend_GL);
        CHECK(gpu_backend_flags(GPU_BACKEND_CPU) == 0);
    } else if (!strcmp(argv[1], "preferences")) {
        const char *choices[] = {"auto", "vulkan", "opengl", "software"};
        for (int index = 0; index < 4; index++) {
            CHECK(renderer_parse(choices[index]) == index);
            CHECK(!strcmp(renderer_key((GpuBackend)index), choices[index]));
        }
        const char *invalid[] = {"", "1", "VULKAN", "vulkanjunk", "vulkan&x=1", " vulkan"};
        for (int index = 0; index < 6; index++) CHECK(renderer_parse(invalid[index]) == -1);
        renderer_load("renderer=opengl\n"); CHECK(g_renderer == GPU_BACKEND_GL);
        renderer_load("renderer=vulkanjunk\n"); CHECK(g_renderer == GPU_BACKEND_GL);
        renderer_load("renderer=software\r\n"); CHECK(g_renderer == GPU_BACKEND_CPU);
        renderer_load("other=auto\n"); CHECK(g_renderer == GPU_BACKEND_CPU);
    } else if (!strcmp(argv[1], "save")) {
        g_renderer = GPU_BACKEND_GL; settings_save(); g_renderer = GPU_BACKEND_VULKAN;
        FILE *saved = fopen("settings.txt", "r"); CHECK(saved);
        char line[256];
        while (fgets(line, sizeof line, saved)) renderer_load(line);
        fclose(saved); remove("settings.txt");
        CHECK(g_renderer == GPU_BACKEND_GL);
    } else CHECK(false);
    return 0;
}
