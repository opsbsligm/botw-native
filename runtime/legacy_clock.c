/* BOTW 1.6.0 clock, adapted from MacNativeGames (MIT, f1d00772).
 * Generated code reads host values directly: no executable pages or guest data
 * pages are patched at runtime. Newer game versions retain their existing path. */
#include "legacy_clock.h"
#include <stdatomic.h>
static _Atomic float seconds = 1.0f / 30.0f;
static _Atomic unsigned pad_tick = 1;

float sdk7_frame_ratio(void) {
    return atomic_load_explicit(&seconds, memory_order_acquire) * 30.0f;
}
unsigned sdk7_pad_tick(void) {
    return atomic_load_explicit(&pad_tick, memory_order_acquire);
}
extern void nd_cheats_apply_pending(void) __attribute__((weak));
void sdk7_frame_presented(uint64_t now) {
    if(nd_cheats_apply_pending) nd_cheats_apply_pending();
    static uint64_t previous;
    static double samples[5], phase;
    static unsigned count, next;
    if (!previous) { previous = now; return; }
    if (now <= previous) { previous = now; count = next = 0; return; }
    double dt = (now - previous) * 1e-9;
    previous = now;
    if (dt > 0.25 || dt < 0.001) { count = next = 0; return; }
    samples[next++ % 5] = dt;
    if (count < 5) count++;
    double total = 0;
    for (unsigned i = 0; i < count; i++) total += samples[i];
    double step = total / count;
    if (step > 1.0 / 15.0) step = 1.0 / 15.0;
    atomic_store_explicit(&seconds, (float)step, memory_order_release);
    phase += step * 30.0;
    unsigned tick = 0;
    if (phase >= 1.0 - 1e-6) {
        tick = 1;
        phase -= 1.0;
        if (phase < 0 || phase >= 1.0) phase = 0;
    }
    atomic_store_explicit(&pad_tick, tick, memory_order_release);
}
