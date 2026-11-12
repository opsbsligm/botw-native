// SPDX-License-Identifier: MIT
// Execute the production precompile engine with a synthetic one-shader archive.
// Shader/GPU operations are injected; task accounting and filesystem publication are real.
#define _GNU_SOURCE
#include <assert.h>
#include <time.h>
#include "../runtime/nvn/vulkan/precompile.c"

VkG g_vk;
uint64_t maxwell_gmem_base, maxwell_gmem_size;
static bool fail_translation, fail_pipeline, fail_scan, fail_cache_save, device_cache_valid = true;
static const char *cache_directory = ".";
static const char *shader_version = "synthetic-translator/spirv";

Uint64 SDL_GetTicksNS(void) {
    struct timespec t;
    assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (Uint64)t.tv_sec * 1000000000ull + (Uint64)t.tv_nsec;
}
int SDL_GetNumLogicalCPUCores(void) { return 1; }
int SDL_GetSystemRAM(void) { return 1024; }
void vk_cache_path(char *out, size_t size, const char *name) { snprintf(out, size, "%s/%s", cache_directory, name); }
const char *vk_shader_version(void) { return shader_version; }
bool vk_precompiled_cache_ok(void) { return device_cache_valid; }
bool vk_precompiled_cache_save(void) { return !fail_cache_save; }
void vk_pipeline_cache_save(void) {}
int vk_keylog_read(VkKeyLogEntry **out) { *out = NULL; return 0; }
void vk_keylog_flush(void) {}
void vk_key_normalize(const uint8_t *code, int stage, ShaderKey *key) { (void)code; (void)stage; (void)key; }
uint32_t vk_sph_inputs(const uint8_t *code, int stage) { (void)code; (void)stage; return 0; }
int shader_archive_find(const SaSet *set, uint64_t hash) { (void)set; return hash == 1 ? 0 : -1; }
bool shader_archive_scan(const char *romfs, SaSet *set, void (*progress)(float, void *), void *user, char *why, size_t size) {
    (void)romfs;
    memset(set, 0, sizeof *set);
    if (fail_scan) { snprintf(why, size, "synthetic scan failure"); return false; }
    set->sources = calloc(1, sizeof *set->sources);
    set->sources[0] = strdup("synthetic.sharcb");
    set->nsources = set->nshaders = set->nprograms = 1;
    set->shaders = calloc(1, sizeof *set->shaders);
    set->shaders[0] = (SaShader){.stage = STAGE_COMPUTE, .size = 16, .max_size = 16,
        .code = calloc(1, 16), .hash = 1, .local = {1, 1, 1}};
    set->programs = calloc(1, sizeof *set->programs);
    set->programs[0] = (SaProgram){.vs = -1, .fs = -1, .cs = 0};
    set->files = 1; set->bytes = 16;
    if (progress) progress(1, user);
    return true;
}
void shader_archive_free(SaSet *set) {
    free(set->shaders[0].code); free(set->shaders); free(set->programs);
    free(set->sources[0]); free(set->sources);
    memset(set, 0, sizeof *set);
}
int vk_precompile_one(const uint8_t *code, size_t size, int stage, const ShaderKey *key, char *why, size_t why_size) {
    (void)code; (void)size; (void)stage; (void)key;
    if (fail_translation) { snprintf(why, why_size, "injected translation failure"); return -1; }
    return 1;
}
ShaderInfo *vk_xlate_read(const uint8_t *code, size_t size, int stage, const ShaderKey *key) {
    (void)code; (void)size; (void)stage; (void)key;
    return fail_translation ? NULL : calloc(1, sizeof(ShaderInfo));
}
void maxwell_free(ShaderInfo *info) { free(info); }
bool vk_pso_warm(const ShaderInfo *v, const ShaderInfo *f, const uint8_t kinds[32]) {
    (void)v; (void)f; (void)kinds; return !fail_pipeline;
}
bool vk_pso_warm_compute(const ShaderInfo *info, const uint32_t local[3]) { (void)info; (void)local; return !fail_pipeline; }
bool vk_pso_warm_stage(const ShaderInfo *info, int stage) { (void)info; (void)stage; return !fail_pipeline; }

static void write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "w"); assert(f);
    assert(fputs(text, f) >= 0); assert(fclose(f) == 0);
}
static void retained_cache(void) {
    char data[16] = {0}; FILE *f = fopen("retained-cache.bin", "r"); assert(f);
    assert(fread(data, 1, 8, f) == 8 && !memcmp(data, "retained", 8)); fclose(f);
}
static bool read_record_text(const char *text) {
    FILE *f = tmpfile(); assert(f); assert(fputs(text, f) >= 0); rewind(f);
    SwPrecompileRecord record; bool ok = sw_precompile_record_read(f, &record); fclose(f); return ok;
}
int main(void) {
    g_vk.dev = (VkDevice)(uintptr_t)1;
    g_vk.gpl = true;
    write_text("retained-cache.bin", "retained");
    // Old completion records cannot establish zero failures.
    write_text("precompiled.txt", "synthetic-translator/spirv\n1 2026-11-12\n");
    assert(shader_precompile_status(NULL, NULL, 0) == 2);
    ShaderPrecompile success = {0};
    assert(shader_precompile_run(&success, "synthetic"));
    assert(atomic_load(&success.state) == SP_DONE && success.failed == 0 && success.pipelines == 1);
    int shaders = 0; char day[32];
    assert(shader_precompile_status(&shaders, day, sizeof day) == 1 && shaders == 1 && *day);
    shader_version = "new-translator/spirv";
    assert(shader_precompile_status(NULL, NULL, 0) == 2);
    shader_version = "synthetic-translator/spirv";
    device_cache_valid = false;
    assert(shader_precompile_status(NULL, NULL, 0) == 2);
    device_cache_valid = true;
    // A real translation failure must remove a prior completion hint and retain cached work.
    fail_translation = true;
    ShaderPrecompile failed = {0};
    assert(!shader_precompile_run(&failed, "synthetic"));
    assert(atomic_load(&failed.state) == SP_FAILED && failed.failed == 1);
    assert(shader_precompile_status(NULL, NULL, 0) == 0);
    retained_cache(); fail_translation = false;
    // Translation success is insufficient when a requested pipeline fails.
    fail_pipeline = true;
    ShaderPrecompile pipeline = {0};
    assert(!shader_precompile_run(&pipeline, "synthetic"));
    assert(atomic_load(&pipeline.state) == SP_FAILED && pipeline.failed == 0 && pipeline.pipelines == 0);
    assert(shader_precompile_status(NULL, NULL, 0) == 0);
    fail_pipeline = false;
    fail_cache_save = true;
    ShaderPrecompile unsaved = {0}; assert(!shader_precompile_run(&unsaved, "synthetic"));
    assert(atomic_load(&unsaved.state) == SP_FAILED && unsaved.pipelines == 1);
    assert(shader_precompile_status(NULL, NULL, 0) == 0);
    fail_cache_save = false;
    // Cancellation and skipped pipeline warmup cannot create a complete marker.
    ShaderPrecompile cancelled = {0}; atomic_store(&cancelled.cancel, true);
    assert(!shader_precompile_run(&cancelled, "synthetic"));
    assert(atomic_load(&cancelled.state) == SP_CANCELLED && shader_precompile_status(NULL, NULL, 0) == 0);
    assert(setenv("SAVAGEWIND_PRECOMPILE_NO_PIPELINES", "1", 1) == 0);
    ShaderPrecompile skipped = {0}; assert(!shader_precompile_run(&skipped, "synthetic"));
    assert(atomic_load(&skipped.state) == SP_FAILED && shader_precompile_status(NULL, NULL, 0) == 0);
    unsetenv("SAVAGEWIND_PRECOMPILE_NO_PIPELINES");
    // Publication errors and read failures leave the cache retryable.
    cache_directory = "missing/directory";
    ShaderPrecompile publish = {0}; assert(!shader_precompile_run(&publish, "synthetic"));
    assert(atomic_load(&publish.state) == SP_FAILED);
    cache_directory = "."; fail_scan = true;
    ShaderPrecompile scan = {0}; assert(!shader_precompile_run(&scan, "synthetic"));
    assert(atomic_load(&scan.state) == SP_FAILED); fail_scan = false;
    retained_cache();
    // A subsequent successful retry publishes a strict, fully readable record.
    ShaderPrecompile retry = {0}; assert(shader_precompile_run(&retry, "synthetic"));
    assert(shader_precompile_status(NULL, NULL, 0) == 1);
    SwPrecompileRecord record;
    FILE *f = fopen("precompiled.txt", "r"); assert(f && sw_precompile_record_read(f, &record)); fclose(f);
    record.failed = 24;
    assert(!sw_precompile_record_write("precompiled.txt", &record, 12));
    assert(shader_precompile_status(NULL, NULL, 0) == 1); // rejected before replacing anything
    assert(!read_record_text("SWPC2\nv\nd\n1 1 1 0 24 1 1 1\nEND\n"));
    assert(!read_record_text("SWPC2\nv\nd\n1 1 1 0 0 2 1 1\nEND\n"));
    assert(!read_record_text("SWPC2\nv\nd\n1 2 1 0 0 1 1 1\nEND\n"));
    assert(!read_record_text("SWPC2\nv\nd\n2 1 1 0 0 1 1 1\nEND\n"));
    assert(!read_record_text("SWPC2\nv\nd\n1 1 1 0 0 1 1 0\nEND\n"));
    assert(!read_record_text("SWPC2\nv\nd\n1 1 1 0 0 1 1 1\n"));
    assert(!read_record_text("SWPC2\nv\nd\n1 1 1 0 0 1 1 1\nEND\nextra"));
    assert(!read_record_text("SWPC2\nv\nd\n1 9999999999999999999999999999999999999 1 0 0 1 1 1\nEND\n"));
    assert(!read_record_text("SWPC2\nv\nd\n-1 1 1 0 0 1 1 1\nEND\n"));
    puts("Production precompile completion: failures/cancel/skip/legacy/IO/malformed records rejected; retry and cache retention passed");
    return 0;
}
