// SPDX-License-Identifier: MIT
// The in-play shader warm-up (runtime/nvn/warmup.c): once per boot at the first presented frame, only when the
// precompilation's record is not complete; a new renderer generation (an in-place reboot) starts it again; a
// stopped run leaves the caches as they are. The production file is compiled with its Vulkan engine stubbed.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "../runtime/nvn/warmup.c"  // production translation unit; the stubs below keep its engine out of the link

// ---- stubs: the engine (runtime/nvn/vulkan/precompile.c) and what it shares with the game ----
_Atomic uint64_t sw_renderer_generation = 1;
const char *g_romfs_dir = "/tmp/romfs";
uint64_t maxwell_gmem_base, maxwell_gmem_size;
void SDL_Delay(int ms) { (void)ms; }
static _Atomic int test_status;
static _Atomic int test_calls, test_running, test_release;
int shader_precompile_status(int *shaders, char *date, int n) { (void)shaders; (void)date; (void)n; return atomic_load(&test_status); }
bool shader_precompile_available(void) { return true; }
float shader_precompile_progress(const ShaderPrecompile *p) {
    int st = atomic_load(&p->state), d = atomic_load(&p->done), t = atomic_load(&p->total);
    if (st == SP_DONE) return 1;
    return t > 0 ? (float)d / (float)t : 0;
}
bool shader_precompile_run(ShaderPrecompile *p, const char *romfs) {
    (void)romfs;
    atomic_fetch_add(&test_calls, 1);
    atomic_store(&test_running, 1);
    atomic_store(&p->state, SP_COMPILING);
    while (!atomic_load(&test_release)) SDL_Delay(1);
    if (atomic_load(&p->cancel)) { atomic_store(&p->state, SP_CANCELLED); return false; }
    atomic_store(&p->state, SP_DONE);
    atomic_store(&test_running, 0);
    return true;
}

static void wait_calls(int want) {
    for (int i = 0; i < 200000 && atomic_load(&test_calls) < want; i++) SDL_Delay(1);
    assert(atomic_load(&test_calls) == want);
}
static void wait_done(void) {
    for (int i = 0; i < 200000 && sw_warmup_state() != SP_DONE; i++) SDL_Delay(1);
    assert(sw_warmup_state() == SP_DONE && sw_warmup_share() == 1000);
}

int main(int argc, char **argv) {
    (void)argc;
    // 1. the record is not complete: the first presented frame starts the warm-up, and it plays on in the background
    atomic_store(&test_status, 0);
    assert(sw_warmup_available());
    sw_warmup_frame();
    wait_calls(1);
    atomic_store(&test_release, 1);
    wait_done();
    // 2. ... and it does not start again in the same boot, frame after frame
    for (int i = 0; i < 10; i++) sw_warmup_frame();
    wait_calls(1);
    // 3. an in-place reboot (a new renderer generation) starts it again
    sw_renderer_generation++;
    sw_warmup_frame();
    wait_calls(2);
    atomic_store(&test_release, 1);
    wait_done();
    // 4. ... but a complete record (cache/precompiled.txt) keeps it off
    sw_renderer_generation++;
    atomic_store(&test_status, 1);
    sw_warmup_frame();
    sw_warmup_frame();
    wait_calls(2);
    // 5. stopped from the control bar: SP_CANCELLED, no record, what is done stays in the caches
    sw_renderer_generation++;
    atomic_store(&test_status, 0);
    sw_warmup_frame();
    wait_calls(3);
    assert(sw_warmup_state() == SP_COMPILING);
    sw_warmup_cancel();
    assert(sw_warmup_state() == SP_CANCELLED);
    atomic_store(&test_release, 1);
    wait_calls(3);
    assert(sw_warmup_share() == -1);
    // 6. the wait (SAVAGEWIND_SHADER_WARMUP=2) is on only then
    int forced = getenv("SAVAGEWIND_SHADER_WARMUP") ? atoi(getenv("SAVAGEWIND_SHADER_WARMUP")) : -1;
    sw_renderer_generation++;
    atomic_store(&test_status, 0);
    sw_warmup_frame();
    wait_calls(4);
    if (forced == 2) {
        assert(sw_warmup_wait_needed());
        float done = -1;
        while (sw_warmup_wait(1, &done)) assert(done >= 0 && done <= 1000);
    } else {
        assert(!sw_warmup_wait_needed());
    }
    atomic_store(&test_release, 1);
    wait_done();
    assert(sw_warmup_state() == SP_DONE);
    assert(!sw_warmup_wait_needed());
    // 7. a reboot in the middle of a run: the old run's caches are not this renderer's; it drops them, and the
    // next boot's first frame starts again (its early exit must not leave the "running" flag behind)
    sw_renderer_generation++;
    atomic_store(&test_status, 0);
    sw_warmup_frame();
    wait_calls(5);
    assert(sw_warmup_state() == SP_COMPILING);
    sw_renderer_generation++;  // (the reboot, while the warm-up is still running)
    atomic_store(&test_release, 1);
    // the old run's worker finishes and drops out; the next boot's frames keep calling sw_warmup_frame until the
    // old run's "running" flag has been handed over, then the warm-up starts again
    for (int i = 0; i < 200000 && atomic_load(&test_calls) < 6; i++) sw_warmup_frame();
    wait_calls(6);
    atomic_store(&test_release, 1);
    wait_done();
    fprintf(stderr, "[warmup-policy] ok (forced=%d)\n", forced);
    return 0;
}
