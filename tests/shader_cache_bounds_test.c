// SPDX-License-Identifier: MIT
// Real cache writer/reader, with corrupted serialized metadata and intact compressed source. No GPU is used.
#include <assert.h>
#include <stddef.h>
#include "../runtime/nvn/vulkan/shaders.c"

const char *maxwell_translator_id(void) { return "0123456789abcdef"; }
static const uint64_t cache_id = 0x12345678;
static const char source[] = "#version 460\nlayout(local_size_x=1) in; void main() {}\n";
static unsigned cases;

static XlateHeader store(int textures, int images, bool empty) {
    ShaderInfo info = {.stage = STAGE_COMPUTE, .msl = (char *)source, .ntex = textures, .nimages = images};
    for (int i = 0; i < images; i++) {
        info.images[i] = (ShaderImage){.handle_offset = 0x120 + (uint32_t)(i % 8) * 8,
            .type = TEXTYPE_2D, .raw32 = i < 8, .write = i >= 8};
    }
    XlateHeader header = {.magic = {'S', 'W', 'X', '2'}, .info_size = sizeof info,
        .msl_len = empty ? 0 : sizeof source - 1, .bin_size = 16,
        .key = {.glsl = 1}, .stage = STAGE_COMPUTE};
    assert(xlate_store(cache_id, &header, &info));
    return header;
}

static void corrupt_count(size_t field, int value) {
    char path[512]; xlate_path(path, sizeof path, cache_id);
    FILE *file = fopen(path, "r+b"); assert(file);
    assert(fseek(file, (long)(sizeof(XlateHeader) + field), SEEK_SET) == 0);
    assert(fwrite(&value, sizeof value, 1, file) == 1);
    assert(fclose(file) == 0);
}

static void checked(const XlateHeader *header, bool by_key, bool accept, int textures, int images) {
    ShaderInfo *loaded = xlate_load(cache_id, by_key ? header : NULL, STAGE_COMPUTE);
    assert((loaded != NULL) == accept);
    if (loaded) {
        assert(loaded->ntex == textures && loaded->nimages == images);
        assert(memcmp(loaded->msl, source, sizeof source) == 0);
        free(loaded->msl); free(loaded);
    }
    cases++;
}

int main(int argc, char **argv) {
    assert(argc == 2 && setenv("SAVAGEWIND_CACHE_DIR", argv[1], 1) == 0);
    for (int by_key = 0; by_key < 2; by_key++) {
        XlateHeader header = store(0, 0, false); checked(&header, by_key, true, 0, 0);
        header = store(64, 0, false); checked(&header, by_key, true, 64, 0);
        header = store(0, 16, false); checked(&header, by_key, true, 0, 16);
        const struct { size_t field; int value; } bad[] = {
            {offsetof(ShaderInfo, ntex), -1}, {offsetof(ShaderInfo, ntex), 65},
            {offsetof(ShaderInfo, nimages), -1}, {offsetof(ShaderInfo, nimages), 17}};
        for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            header = store(0, 0, false); corrupt_count(bad[i].field, bad[i].value);
            checked(&header, by_key, false, 0, 0);
        }
        header = store(0, 0, true); checked(&header, by_key, false, 0, 0);
        // The same path remains usable after rejecting a corrupt cache.
        header = store(0, 0, false); checked(&header, by_key, true, 0, 0);
    }
    assert(cases == 18);
    puts("Production translation cache: 18 keyed/id cases passed; corrupted counts and empty source rejected");
    return 0;
}
