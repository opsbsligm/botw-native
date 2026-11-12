// SPDX-License-Identifier: MIT
// Synthetic Maxwell instructions only; translate and compile with the real shaderc library.
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <shaderc/shaderc.h>
#include "../runtime/nvn/maxwell/shader.h"

static uint64_t field(uint64_t w, int lo, int n, uint64_t value) {
    uint64_t mask = ((1ull << n) - 1) << lo;
    return (w & ~mask) | ((value << lo) & mask);
}
static uint64_t fetch(int info, bool half) {
    uint64_t w = 0xd200000000000000ull | (7ull << 16);
    w = field(w, 59, 1, !half);
    w = field(w, 53, 4, (uint64_t)info);
    w = field(w, 36, 13, 8);  // handle at cbuf offset 0x20
    w = field(w, 50, 3, 4);   // RG into destination 0/1
    w = field(w, 28, 8, 255);
    w = field(w, 8, 8, 4);   // x/y in R4/R5
    return field(w, 20, 8, 8); // sample in R8
}
static uint64_t query(void) {
    uint64_t w = 0xdf48000000000000ull | (7ull << 16);
    w = field(w, 36, 13, 8);
    w = field(w, 22, 6, 1);
    w = field(w, 31, 4, 15);
    return field(w, 0, 8, 12);
}
static void compile_case(int info, bool half, bool query_first, bool query_last) {
    uint64_t code[8] = {0}; size_t n = 1;
    if (query_first) code[n++] = query();
    code[n++] = fetch(info, half);
    if (query_last) { if (n % 4 == 0) n++; code[n++] = query(); }
    if (n % 4 == 0) n++;
    code[n++] = 0xe30000000007000full;
    ShaderKey key = {.glsl = 1};
    ShaderInfo *s = maxwell_translate((const uint8_t *)code, n * 8, STAGE_COMPUTE, &key);
    if (!s->msl) fprintf(stderr, "Translation failed: %s\n", s->error);
    assert(s->msl && s->ntex == 1);
    bool ms = info == 6;
    assert(s->tex[0].type == (ms ? TEXTYPE_2D_MS : TEXTYPE_2D));
    if (ms) {
        assert(strstr(s->msl, "uniform sampler2DMS t0;"));
        assert(strstr(s->msl, "ivec2(r4, r5), int(r8)"));
        assert(!strstr(s->msl, "textureQueryLevels(t0)"));
    }
    if (half) assert(strstr(s->msl, "packHalf2x16"));
    shaderc_compiler_t compiler = shaderc_compiler_initialize(); assert(compiler);
    shaderc_compile_options_t options = shaderc_compile_options_initialize(); assert(options);
    shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    shaderc_compilation_result_t result = shaderc_compile_into_spv(compiler, s->msl, strlen(s->msl),
        shaderc_compute_shader, "synthetic-tlds.comp", "main", options);
    assert(result);
    if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success)
        fprintf(stderr, "%s\n%s\n", shaderc_result_get_error_message(result), s->msl);
    assert(shaderc_result_get_compilation_status(result) == shaderc_compilation_status_success);
    const uint32_t *words = (const uint32_t *)shaderc_result_get_bytes(result);
    size_t count = shaderc_result_get_length(result) / 4;
    bool found_type = false, found_fetch = false;
    for (size_t i = 5; i < count;) {
        unsigned length = words[i] >> 16, op = words[i] & 65535;
        assert(length && i + length <= count);
        if (op == 25 && length >= 9) { // OpTypeImage: 2D, MS flag, sampled image
            assert(words[i + 3] == 1 && words[i + 6] == (unsigned)ms && words[i + 7] == 1);
            found_type = true;
        }
        if (op == 95 && length >= 7) { // OpImageFetch: explicit Sample (0x40), not Lod (0x2)
            assert((words[i + 5] & (ms ? 0x40 : 0x2)) != 0);
            assert(!ms || !(words[i + 5] & 0x2));
            found_fetch = true;
        }
        i += length;
    }
    assert(found_type && found_fetch);
    shaderc_result_release(result); shaderc_compile_options_release(options); shaderc_compiler_release(compiler);
    maxwell_free(s);
}
int main(void) {
    compile_case(6, false, false, false);
    compile_case(6, true, false, false);
    compile_case(6, false, true, false);
    compile_case(6, false, false, true);
    compile_case(5, false, true, true); // ordinary mipmapped fetch keeps its Lod operand
    puts("TLDS multisample: real translator/shaderc and SPIR-V sample operands passed");
    return 0;
}
