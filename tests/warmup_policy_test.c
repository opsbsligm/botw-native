// SPDX-License-Identifier: MIT
// The in-play shader warm-up (runtime/nvn/warmup.c): once per boot at the first presented frame, only when the
// precompilation's record is not complete; a new renderer generation (an in-place reboot) starts it again; a
// stopped run leaves the caches as they are. The production file is compiled with its Vulkan engine stubbed.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>

#include "../runtime/nvn/warmup.c"  // production translation unit; the stubs below keep its engine out of the link

// ---- stubs: the engine (runtime/nvn/vulkan/precompile.c) and what it shares with the game ----
_Atomic uint64_t sw_renderer_generation = 1;
const char *g_romfs_dir = "/tmp/romfs";
uint64_t maxwell_gmem_base, maxwell_gmem_size;
static _Atomic int test_pso_polls, test_pso_progress, test_pso_done;
static _Atomic uint64_t test_ticks;
static uint64_t total_frames;
Uint64 SDL_GetTicksNS(void) { return atomic_fetch_add(&test_ticks, 200000000ULL) + 200000000ULL; }
static bool vk_prewarm(void) { return false; }
static bool vk_pso_wait(int ms, float *done) { (void)ms; *done=1; return true; }
static void present_progress(float done) {
    assert(done >= 0 && done <= 1);  // the real renderer uses this directly as a fraction of the bar width
    atomic_fetch_add(&test_pso_progress, 1);
}
// Appended from device.c by the test launcher, with the GPU calls above stubbed.
static uint64_t pso_wait(void);
static void *test_renderer_wait(void *unused) {
    (void)unused;
    pso_wait();
    atomic_store(&test_pso_done, 1);
    return NULL;
}
void SDL_Delay(Uint32 ms) {  // (a real block: the no-op stub turns the poll loops below into CPU spins)
    if (ms == 100) atomic_fetch_add(&test_pso_polls, 1);
    struct timespec t = { 0, 1000000L };  // 1ms
    nanosleep(&t, NULL);
}
static _Atomic int test_status;
static _Atomic int test_calls, test_running, test_release, test_parking;
static char test_romfs[1100];  // (the romfs path the engine received: the worker must pass g_romfs_dir itself)
int shader_precompile_status(int *shaders, char *date, int n) { (void)shaders; (void)date; (void)n; return atomic_load(&test_status); }
bool shader_precompile_available(void) { return true; }
float shader_precompile_progress(const ShaderPrecompile *p) {
    int st = atomic_load(&p->state), d = atomic_load(&p->done), t = atomic_load(&p->total);
    if (st == SP_DONE) return 1;
    return t > 0 ? (float)d / (float)t : 0;
}
bool shader_precompile_run(ShaderPrecompile *p, const char *romfs) {
    snprintf(test_romfs, sizeof test_romfs, "%s", romfs);
    atomic_fetch_add(&test_calls, 1);
    atomic_store(&test_running, 1);
    atomic_store(&p->state, SP_COMPILING);
    atomic_store(&p->total, 100);
    atomic_store(&p->done, 50);  // force an in-progress fraction, so an erroneous 0..1000 scale cannot pass
    atomic_store(&test_parking, 1);  // (the test holds this run: it waits here until it says otherwise)
    while (!atomic_load(&test_release) && !atomic_load(&p->cancel)) SDL_Delay(1);  // (the engine sees a cancel in its compile)
    atomic_store(&test_parking, 0);
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
    // the worker sets g_share last in the success path (after the engine already reached SP_DONE), so
    // the share - not the state - is the synchronization point a finished run leaves behind
    for (int i = 0; i < 200000 && sw_warmup_share() != 1000; i++) SDL_Delay(1);
    assert(sw_warmup_state() == SP_DONE && sw_warmup_share() == 1000);
}


// the worker entered its release loop for the run N: it incremented the counter and is now parked until the
// test releases it (a run that had already finished is not one the test can hold in SP_COMPILING)
static void wait_running(int n) {
    for (int i = 0; i < 200000 && (atomic_load(&test_calls) < n || !atomic_load(&test_running)); i++) SDL_Delay(1);
    assert(atomic_load(&test_calls) >= n && atomic_load(&test_running));
    for (int i = 0; i < 200000 && !atomic_load(&test_parking); i++) SDL_Delay(1);
    assert(atomic_load(&test_parking));
}

int main(int argc, char **argv) {
    (void)argc;
    // the runs the test holds in SP_COMPILING need the release flag down, so the run parks in its loop instead
    // of finishing; a flag left up by an earlier run races every such assertion.
    atomic_store(&test_release, 0);
    // 1. the record is not complete: the first presented frame starts the warm-up, and it plays on in the background
    atomic_store(&test_status, 0);
    assert(sw_warmup_available());
    sw_warmup_frame();
    wait_calls(1);
    // 1b. the engine receives the romfs folder itself (main.c sets g_romfs_dir to <root>/romfs): a second
    // /romfs on it makes the scan open a missing folder ("no game data folder")
    assert(strcmp(test_romfs, g_romfs_dir) == 0);
    wait_running(1);
    atomic_store(&test_release, 1);
    wait_done();
    float complete = -1;
    assert(sw_warmup_wait(0, &complete) && complete == 1);
    // 2. ... and it does not start again in the same boot, frame after frame
    for (int i = 0; i < 10; i++) sw_warmup_frame();
    wait_calls(1);
    // 3. an in-place reboot (a new renderer generation) starts it again
    atomic_store(&test_release, 0);  // (the previous run left it up: this run must park in its loop, not race past it)
    sw_renderer_generation++;
    sw_warmup_frame();
    wait_calls(2);
    wait_running(2);
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
    atomic_store(&test_release, 0);  // (a run parked in its loop, not raced past by a flag left up)
    sw_warmup_frame();
    wait_calls(3);
    wait_running(3);
    atomic_store(&test_release, 0);
    assert(sw_warmup_state() == SP_COMPILING);
    sw_warmup_cancel();
    for (int i = 0; i < 200000 && sw_warmup_state() != SP_CANCELLED; i++) SDL_Delay(1);
    assert(sw_warmup_state() == SP_CANCELLED);
    atomic_store(&test_release, 1);
    wait_calls(3);
    assert(sw_warmup_share() == -1);
    // 6. the wait (SAVAGEWIND_SHADER_WARMUP=2) is on only then
    int forced = getenv("SAVAGEWIND_SHADER_WARMUP") ? atoi(getenv("SAVAGEWIND_SHADER_WARMUP")) : -1;
    sw_renderer_generation++;
    atomic_store(&test_status, 0);
    atomic_store(&test_release, 0);
    sw_warmup_frame();
    wait_calls(4);
    wait_running(4);
    atomic_store(&test_release, 0);
    if (forced == 2) {
        assert(sw_warmup_wait_needed());
        pthread_t renderer;
        assert(pthread_create(&renderer, NULL, test_renderer_wait, NULL) == 0);
        // The engine remains parked. The actual pso_wait caller must keep polling and draw a valid progress
        // bar; a premature 'completed' return instead sets test_pso_done and fails this handshake.
        for (int i=0; i<2000 && !atomic_load(&test_pso_done) && !atomic_load(&test_pso_progress); i++) SDL_Delay(1);
        assert(!atomic_load(&test_pso_done));
        assert(atomic_load(&test_pso_polls) >= 2 && atomic_load(&test_pso_progress) > 0);
        atomic_store(&test_release, 1);
        assert(pthread_join(renderer, NULL) == 0);
        assert(atomic_load(&test_pso_done));
    } else {
        assert(!sw_warmup_wait_needed());
        assert(pso_wait() == 0);  // the normal background mode never blocks the rendering caller
        atomic_store(&test_release, 1);
    }
    wait_done();
    assert(sw_warmup_state() == SP_DONE);
    assert(!sw_warmup_wait_needed());
    // 7. a reboot in the middle of a run: the old run's caches are not this renderer's; it drops them, and the
    // next boot's first frame starts again (its early exit must not leave the "running" flag behind)
    sw_renderer_generation++;
    atomic_store(&test_status, 0);
    atomic_store(&test_release, 0);
    sw_warmup_frame();
    wait_calls(5);
    wait_running(5);
    atomic_store(&test_release, 0);
    sw_renderer_generation++;  // (the reboot, while the warm-up is still running)
    atomic_store(&test_release, 1);
    // the old run's worker finishes and drops out (its parking loop wakes on the millisecond tick the stub sleeps
    // in); the next boot's frames keep calling sw_warmup_frame, at the same tick, until the old run's "running"
    // flag has been handed over, then the warm-up starts again
    for (int i = 0; i < 200000 && atomic_load(&test_calls) < 6; i++) { sw_warmup_frame(); SDL_Delay(1); }
    wait_calls(6);
    wait_done();
    fprintf(stderr, "[warmup-policy] ok (forced=%d)\n", forced);
    return 0;
}
