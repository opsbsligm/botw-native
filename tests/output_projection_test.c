// SPDX-License-Identifier: MIT
// Exercise the production capability gates and camera math with original data.
#include <assert.h>
#include "../runtime/nvn/output_scale.c"

Settings g_settings;
static bool legacy = true, event;
static const GameLibTables *loaded;
static uint64_t camera_event_site;
const GameLibTables *gamelib_tables(void) { return loaded; }
bool gamelib_legacy_sdk7_profile(void) { return legacy; }
uint64_t gamelib_sdk7_camera_event_site(void) { return camera_event_site; }
bool framepace_title_ui(void) { return false; }
bool framepace_loading_ui(void) { return false; }
bool framepace_menu_ui(void) { return false; }
bool framepace_in_event(void) { return event; }
uint64_t framepace_shown_frame(void) { return 0; }
uint64_t renderer_present_count(void) { return 0; }
uint64_t plat_now_ns(void) { static uint64_t now; return now += 1000000000; }
bool fsr3_available(void) { return true; }

int main(void) {
    setenv("SAVAGEWIND_FSR3", "1", 1);
    setenv("SAVAGEWIND_JITTER", "1", 1);
    unsetenv("SAVAGEWIND_SDK7_FOV");
    g_settings.fov = 140;
    g_settings.antialias = AA_TAA;
    assert(!output_projection_available() && !output_fov_available());
    assert(!upscaler_available(UPSCALE_FSR3) && !rt_jitter_on());
    assert(rt_antialias() == AA_SMAA);

    uint32_t nmodules = 1, nsites = 9;
    GameLibModule main = {.base = 0x7000000000, .size = 0x1000000};
    GameLibSite sites[] = {{"persp_matrix", main.base + 4}, {"persp_matrix_offset", main.base + 8},
        {"persp_unproject_x", main.base + 12}, {"persp_unproject_y", main.base + 16},
        {"proj_update", main.base + 20}, {"frustum_lr0", main.base + 24},
        {"frustum_lr1", main.base + 28}, {"frustum_tb0", main.base + 32}, {"frustum_tb1", main.base + 36}};
    GameLibTables tables = {.modules = &main, .nmodules = &nmodules, .sites = sites, .nsites = &nsites};
    loaded = &tables;
    assert(output_projection_available());
    assert(!output_fov_available() && !output_wide_available());
    assert(!antialias_available(AA_TAA) && !upscaler_available(UPSCALE_FSR3));
    OutScale out;
    output_scale(1280, 800, true, &out);
    assert(out.shape == 1 && out.vw == 1280 && out.vh == 720 && out.vy == 40);

    unsigned char camera[256] = {0};
    float near = 0.1f, far = 25000, tan_half = 0.5f, aspect = 16.0f / 9;
    memcpy(camera + 0x98, &near, 4); memcpy(camera + 0x9c, &far, 4);
    memcpy(camera + 0xac, &tan_half, 4); memcpy(camera + 0xb0, &aspect, 4);
    uint32_t t, a;
    memcpy(&t, &tan_half, 4); memcpy(&a, &aspect, 4);
    uint32_t original_t = t, original_a = a;
    rt_persp((uint64_t)(uintptr_t)camera, &t, &a);
    assert(t == original_t && a == original_a);
    setenv("SAVAGEWIND_SDK7_FOV", "1", 1);
    assert(!output_fov_available());  // Old libraries have no guarded event slot.
    camera_event_site = 0x7002cc56d8;
    assert(output_fov_available());
    output_scale(1280, 800, true, &out);
    rt_persp((uint64_t)(uintptr_t)camera, &t, &a);
    float actual, angle;
    memcpy(&actual, &t, 4); memcpy(&angle, camera + 0xa0, 4);
    assert(fabsf(actual - tanf(atanf(0.5f) * 1.4f)) < 0.00001f);
    assert(fabsf(angle - 2 * atanf(actual)) < 0.00001f);
    uint32_t once = t;
    rt_persp((uint64_t)(uintptr_t)camera, &t, &a);
    assert(t == once && a == original_a);  // Repeated loads cannot compound FOV.
    far = 10000;
    memcpy(camera + 0x9c, &far, 4);
    t = original_t; a = original_a;
    rt_persp((uint64_t)(uintptr_t)camera, &t, &a);
    assert(t == original_t && a == original_a);  // HUD/model camera unchanged.

    event = true;
    output_scale(1280, 720, true, &out);
    assert(g_fov == 1);  // The SDK7 camera also restores framing during events.
    event = false;
    legacy = false;
    assert(output_wide_available() && antialias_available(AA_TAA));
    assert(upscaler_available(UPSCALE_FSR3) && rt_jitter_on());
    event = true;
    output_scale(1280, 720, true, &out);
    assert(g_fov == 1);  // Supported event detection restores normal framing.
    nsites = 8;
    assert(!output_projection_available() && !output_fov_available());
    assert(!output_wide_available() && !upscaler_available(UPSCALE_FSR3) && !rt_jitter_on());
    nsites = 9;
    sites[8].addr = main.base + main.size;
    assert(!output_projection_available());
    return 0;
}
