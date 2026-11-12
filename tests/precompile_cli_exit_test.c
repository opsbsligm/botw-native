// SPDX-License-Identifier: MIT
// Exercise the production CLI outcome and Linux termination, independent of GPU availability.
#include <assert.h>
#include <sys/wait.h>
#include <unistd.h>
#define main runtime_application_main
#include "../runtime/main.c"
#undef main

static bool succeeds, initialized, no_pipelines;
void ui_window_init(void) { initialized = true; }
void ui_window_exit(int status) {
    assert(status == (succeeds ? 0 : 1));
    assert(initialized == !no_pipelines);
    plat_terminate(status);
    abort();
}
void SDL_Delay(Uint32 ms) { (void)ms; usleep(1000); }
float shader_precompile_progress(const ShaderPrecompile *p) { (void)p; return 1; }
bool shader_precompile_run(ShaderPrecompile *p, const char *romfs) {
    assert(!strcmp(romfs, "synthetic/romfs"));
    p->shaders = p->keys = 3;
    p->made = succeeds ? 3 : 2;
    p->failed = succeeds ? 0 : 1;
    atomic_store(&p->state, succeeds ? SP_DONE : SP_FAILED);
    return succeeds;
}
int main(void) {
    for (int success = 0; success <= 1; success++)
        for (int skip = 0; skip <= 1; skip++) {
            pid_t child = fork(); assert(child >= 0);
            if (!child) {
                succeeds = success; no_pipelines = skip;
                if (skip) setenv("SAVAGEWIND_PRECOMPILE_NO_PIPELINES", "1", 1);
                else unsetenv("SAVAGEWIND_PRECOMPILE_NO_PIPELINES");
                (void)precompile_shaders("synthetic");
                _exit(99); // The CLI must use explicit GPU teardown with its status, not the status-0 atexit hook.
            }
            int status;
            assert(waitpid(child, &status, 0) == child);
            assert(WIFEXITED(status) && WEXITSTATUS(status) == (success ? 0 : 1));
        }
    puts("Production precompile CLI: explicit success/failure termination with and without pipelines passed");
    return 0;
}
