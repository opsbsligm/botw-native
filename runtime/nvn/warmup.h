// In-play background shader warm-up (runtime/nvn/warmup.c): the setup's full precompilation and the boot's
// pipeline pre-warm (exec.c) both cover what is known, but a game set up with precompilation off, a new driver
// (an empty pipeline cache) or the first visit of a place never walked before still meets shaders that compile
// on the spot. The warm-up keeps filling those caches in the background of a running game: every translation and
// pipeline library the game could need (runtime/nvn/precompile.h), in its own thread with the setup's worker
// count and RAM budget, the game's own caches updated as it goes. Off with SAVAGEWIND_SHADER_WARMUP=0; =2 makes
// the boot's first frame that looks like gameplay wait for it (tests). It starts once per boot at the first
// presented frame, when its record (cache/precompiled.txt) is not complete, and it stops on its own when it is.
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// The Vulkan renderer only (Metal compiles its own shaders): false on macOS.
bool sw_warmup_available(void);

// Every presented frame (device.c present_locked, before the pre-warm's first-frame wait): the boot's first
// frame starts the warm-up, once per boot, only when its record is not complete.
void sw_warmup_frame(void);

// The pre-warm's first frame that looks like gameplay (device.c pso_wait): whether this frame waits for the
// warm-up (only with SAVAGEWIND_SHADER_WARMUP=2), and the wait itself (done: 0..1000; the call returns true when
// it may go on).
bool sw_warmup_wait_needed(void);
bool sw_warmup_wait(int ms, float *done);

// The control bar (runtime/ui/control_bar.mm): SP_* of runtime/nvn/precompile.h, or SP_IDLE when it never ran;
// the second: its progress 0..1000, -1 when there is no count.
int sw_warmup_state(void);
int sw_warmup_share(void);

// Stop it from the control bar (what is done stays in the caches); it does not restart while this boot plays.
void sw_warmup_cancel(void);

#ifdef __cplusplus
}
#endif
