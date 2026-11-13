// SPDX-License-Identifier: MIT
// Real concurrent cache writers/readers with slow directory creation; no GPU or game input.
#include <assert.h>
#include <sys/stat.h>
#include <time.h>
static int delayed_mkdir(const char *, mode_t);
#define mkdir delayed_mkdir
#include "../runtime/nvn/vulkan/shaders.c"
#undef mkdir

const char *maxwell_translator_id(void) { return "0123456789abcdef"; }
static const char source[] = "#version 460\nlayout(local_size_x=1) in; void main() {}\n";
static pthread_barrier_t start;
enum { WRITERS = 16 };

static int delayed_mkdir(const char *path, mode_t mode) {
    const struct timespec delay = {.tv_nsec = 50000000};
    nanosleep(&delay, NULL);
    return mkdir(path, mode);
}

static XlateHeader header(void) {
    return (XlateHeader){.magic = {'S', 'W', 'X', '2'}, .info_size = sizeof(ShaderInfo),
        .msl_len = sizeof source - 1, .bin_size = 16, .key = {.glsl = 1}, .stage = STAGE_COMPUTE};
}

static void *writer(void *argument) {
    uint64_t id = (uint64_t)(uintptr_t)argument;
    XlateHeader h = header();
    ShaderInfo info = {.stage = STAGE_COMPUTE, .msl = (char *)source};
    int barrier = pthread_barrier_wait(&start);
    assert(barrier == 0 || barrier == PTHREAD_BARRIER_SERIAL_THREAD);
    // Readback verifies persistence independently of the writer's returned status.
    xlate_store(id, &h, &info);
    return NULL;
}

int main(int argc, char **argv) {
    assert(argc == 3 && setenv("SAVAGEWIND_CACHE_DIR", argv[1], 1) == 0);
    assert(mkdir(argv[1], 0700) == 0);
    if (!strcmp(argv[2], "blocked")) {
        char path[512];
        assert(snprintf(path, sizeof path, "%s/xlate", argv[1]) < (int)sizeof path);
        FILE *file = fopen(path, "wb"); assert(file && fclose(file) == 0);
        XlateHeader h = header();
        ShaderInfo info = {.stage = STAGE_COMPUTE, .msl = (char *)source};
        assert(!xlate_store(1, &h, &info));
        assert(!xlate_load(1, &h, STAGE_COMPUTE));
        puts("Blocked production cache write rejected");
        return 0;
    }
    assert(!strcmp(argv[2], "parallel"));
    assert(pthread_barrier_init(&start, NULL, WRITERS) == 0);
    pthread_t threads[WRITERS];
    for (uintptr_t i = 0; i < WRITERS; i++)
        assert(pthread_create(&threads[i], NULL, writer, (void *)(i + 1)) == 0);
    for (int i = 0; i < WRITERS; i++) assert(pthread_join(threads[i], NULL) == 0);
    assert(pthread_barrier_destroy(&start) == 0);
    for (uint64_t id = 1; id <= WRITERS; id++) {
        XlateHeader h = header();
        ShaderInfo *loaded = xlate_load(id, &h, STAGE_COMPUTE);
        assert(loaded && loaded->msl && memcmp(loaded->msl, source, sizeof source) == 0);
        free(loaded->msl); free(loaded);
    }
    puts("16 concurrent first-use production cache writes persisted with delayed mkdir");
    return 0;
}
