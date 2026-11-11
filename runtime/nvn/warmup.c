// In-play background shader warm-up (runtime/nvn/warmup.h). The engine is the setup's full precompilation
// (runtime/nvn/precompile.h: every shader of the game's own RomFS translated and compiled into the caches the
// game reads, then its pipeline libraries into the pipeline caches) - here it runs as one background thread of
// a running game, after the boot's pipeline pre-warm (exec.c pso_init) has done what it can from the recorded
// list: the game plays, the warm-up fills what is left.
//
// What makes it safe in play: the caches it writes are the game's own, and all of them are already shared by the
// game's own worker threads - the translation table (shaders.c xlate_lock), cache/xlate and cache/spirv (one
// file per shader, written and renamed in place), the shader key log (its lock) and the pipeline caches
// (device.c pc_save_lock) - so a translation the warm-up makes and a draw's are the same for the same key, and
// a pipeline library the two build for the same shader is a driver cache hit the second time. The warm-up's own
// RomFS scan (shader_archive_scan) reads the game's data through romfs_image (any thread) and holds the bytes in
// its own memory until it is done.
//
// Threads and memory: the engine's own worker count and memory budget (a quarter of the RAM, the setup's
// precompilation's) hold here too. It starts at the boot's first presented frame, once per boot, and only when
// its record (cache/precompiled.txt) is not complete for this GPU, driver and translator: a stopped or failed
// warm-up leaves no record and the renderer's generation (exec.c) counts the boots, so it starts again after an
// in-place reboot and not while this boot plays. SAVAGEWIND_SHADER_WARMUP=0 keeps it off; =2 makes the boot's
// first frame that looks like gameplay wait for it (tests).
#include <SDL3/SDL_timer.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "../rt.h"
#include "maxwell/shader.h"
#include "precompile.h"
#include "warmup.h"

static ShaderPrecompile g_pc;
static _Atomic int g_share = -1;
static _Atomic bool g_cancel, g_running;
static _Atomic uint64_t g_run_gen;  // (the generation that set g_running: its worker clears it)
#ifndef __APPLE__
extern _Atomic uint64_t sw_renderer_generation;  // (vulkan/exec.c: up at every in-place reboot)
static _Atomic uint64_t g_boot_gen;  // (the generation this boot was seen at: once per boot)
static _Atomic bool g_started;
#endif

#ifndef __APPLE__

static void *worker(void *arg) {
    (void)arg;
    uint64_t gen = atomic_load(&sw_renderer_generation);  // (a reboot in the middle: its caches are not ours)
    char romfs[1100];
    snprintf(romfs, sizeof romfs, "%s/romfs", g_romfs_dir ? g_romfs_dir : "");
    if (atomic_load(&sw_renderer_generation) != gen) { if (g_run_gen.load() == gen) g_running = false; return 0; }
    maxwell_gmem_base = HEAP_BASE;   // (as the game translates: global memory relative to the guest heap)
    maxwell_gmem_size = HEAP_MAX;
    bool ok = shader_precompile_run(&g_pc, romfs);
    if (atomic_load(&sw_renderer_generation) != gen) return 0;
    if (atomic_load(&g_pc.state) == SP_CANCELLED) {
        g_share = -1;
        LOG("[warmup] stopped (what is done stays in the caches)\n");
    } else if (!ok) {
        g_share = -1;
        LOG("[warmup] %s\n", g_pc.why);
    } else {
        g_share = 1000;
        LOG("[warmup] done in %.0f s: %d shaders, %d translations (%d made, %d cached, %d failed)\n", g_pc.seconds,
            g_pc.shaders, g_pc.keys, g_pc.made, g_pc.cached, g_pc.failed);
    }
    if (g_run_gen.load() == gen) g_running = false;
    return NULL;
}

// Every presented frame (the main thread, before the pre-warm's first-frame wait): the boot's first frame starts
// the warm-up, once per boot, only when the record is not complete for this GPU, driver and translator.
void sw_warmup_frame(void) {
    static int forced = -2;
    if (forced == -2) forced = getenv("SAVAGEWIND_SHADER_WARMUP") ? atoi(getenv("SAVAGEWIND_SHADER_WARMUP")) : -1;
    if (forced == 0 || g_running.load()) return;
    uint64_t gen = atomic_load(&sw_renderer_generation);
    if (g_boot_gen.load() != gen) { g_boot_gen = gen; g_started = false; }
    if (g_started.load()) return;
    g_started = true;
    if (shader_precompile_status(NULL, NULL, 0) == 1) return;
    atomic_store(&g_pc.cancel, false);
    g_cancel = false;
    g_share = -1;
    g_run_gen = gen;
    g_running = true;
    pthread_t t;
    if (pthread_create(&t, NULL, worker, NULL)) {
        g_running = false;
        LOG("[warmup] cannot start its thread\n");
        return;
    }
    pthread_detach(t);
    LOG("[warmup] started in play\n");
}

// The pre-warm's first gameplay frame (only with SAVAGEWIND_SHADER_WARMUP=2): the wait itself, until the
// warm-up has filled the caches the pre-warm's list reads.
bool sw_warmup_wait_needed(void) {
    static int forced = -2;
    if (forced == -2) forced = getenv("SAVAGEWIND_SHADER_WARMUP") ? atoi(getenv("SAVAGEWIND_SHADER_WARMUP")) : -1;
    return forced == 2 && g_running.load();
}

bool sw_warmup_wait(int ms, float *done) {
    *done = shader_precompile_progress(&g_pc) * 1000;
    if (*done < 1) SDL_Delay(ms);
    return g_running.load();
}

void sw_warmup_cancel(void) {
    if (g_running.load()) {
        g_cancel = true;
        atomic_store(&g_pc.cancel, true);
        LOG("[warmup] stopped from the control bar\n");
    }
}

#else  // (Metal compiles its own shaders: nothing to warm)
void sw_warmup_frame(void) {}
bool sw_warmup_wait_needed(void) { return false; }
bool sw_warmup_wait(int, float *done) { *done = 1000; return true; }
void sw_warmup_cancel(void) {}
#endif

int sw_warmup_state(void) {
    return atomic_load(&g_pc.state);
}

int sw_warmup_share(void) {
    return atomic_load(&g_share);
}

bool sw_warmup_available(void) {
#ifndef __APPLE__
    return shader_precompile_available();
#else
    return false;
#endif
}
