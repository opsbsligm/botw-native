// SPDX-License-Identifier: MIT
// Exercise the actual loader and the appended production camera event reader.
#include <assert.h>
#include "gamelib.c"

bool framepace_in_event(void);
static bool in_event(void);

int main(void) {
    unsigned char *image = calloc(1, 0x2d66000);
    unsigned char *manager = calloc(1, 0x1d2b8 + 8);
    assert(image && manager);
    uint64_t base = (uint64_t)(uintptr_t)image;
    uint64_t *slot = (uint64_t *)(image + 0x2cc56d8);
    uint64_t *active = (uint64_t *)(manager + 0x1d2b8);
    GameLibModule modules[3] = {
        {.name="main", .base=base, .size=0x2d66000,
         .build_id="8e9978d50bdd20b4c8395a106c27ffde00000000000000000000000000000000"},
        {.name="subsdk0"}, {.name="sdk"}};
    GameLibSite sites[2] = {{"sdk7_camera_event_mgr", base + 0x2cc56d8},
                            {"sdk7_camera_event_mgr", base + 0x2cc56d8}};
    uint32_t nmodules=3, nsites=0;
    GameLibTables tables = {.modules=modules, .nmodules=&nmodules, .version="1.6.0",
                            .sites=sites, .nsites=&nsites};
    unsetenv("SAVAGEWIND_SDK7_FOV");
    assert(take_sites(&tables) && !gamelib_sdk7_camera_event_site());
    nsites=1;
    assert(take_sites(&tables) && gamelib_sdk7_camera_event_site()==(uint64_t)(uintptr_t)slot);
    assert(!framepace_in_event());
    setenv("SAVAGEWIND_SDK7_FOV", "1", 1);
    assert(!framepace_in_event());  // Singleton not initialized.
    *slot=(uint64_t)(uintptr_t)manager;
    assert(!framepace_in_event());
    *active=1;
    assert(framepace_in_event());
    assert(!in_event() && !g_site.event_mgr);  // SDK7 FPS policy is untouched.
    setenv("SAVAGEWIND_SDK7_FOV", "0", 1);
    assert(!framepace_in_event());
    unsetenv("SAVAGEWIND_SDK7_FOV");
    assert(!framepace_in_event());
    setenv("SAVAGEWIND_SDK7_FOV", "1", 1);
    nsites=2;
    assert(take_sites(&tables) && !gamelib_sdk7_camera_event_site());
    assert(!framepace_in_event());
    nsites=1; sites[0].addr+=8;
    assert(take_sites(&tables) && !gamelib_sdk7_camera_event_site());
    sites[0].addr-=8; modules[0].size=0x2cc56df;
    assert(take_sites(&tables) && !gamelib_sdk7_camera_event_site());
    modules[0].size=0x2d66000; nsites=1025;
    assert(take_sites(&tables) && !gamelib_sdk7_camera_event_site());
    nsites=1; sites[0].name=NULL;
    assert(take_sites(&tables) && !gamelib_sdk7_camera_event_site());
    sites[0].name="sdk7_camera_event_mgr";
    assert(take_sites(&tables) && gamelib_sdk7_camera_event_site());
    modules[0].build_id="unknown";
    assert(!take_sites(&tables) && !gamelib_sdk7_camera_event_site());
    // Existing non-SDK7 event reads do not acquire a new environment gate.
    g_legacy_sdk7=false;
    g_site.event_mgr=(uint64_t)(uintptr_t)slot;
    unsetenv("SAVAGEWIND_SDK7_FOV");
    assert(framepace_in_event() && in_event());
    *active=0;
    assert(!framepace_in_event());
    free(manager); free(image);
    return 0;
}
