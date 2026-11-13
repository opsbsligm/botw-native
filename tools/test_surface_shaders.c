// SPDX-License-Identifier: MIT
// Original instruction fixtures: production translator -> shaderc -> production SPIR-V reflection.
// This verifies compilation and resource contracts, not GPU execution or game rendering.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shaderc/shaderc.h>
#include "../runtime/nvn/vulkan/shaders.h"

static uint64_t surface(bool store, int dimension, unsigned slot) {
    return (store ? 0xeb20000000000000ull : 0xeb00000000000000ull) | (1ull << 51) |
           (store ? 0 : 1ull << 52) | (uint64_t)dimension << 33 |
           (uint64_t)(store ? 15 : 4) << 20 | (uint64_t)(72 + 2 * slot) << 36 | 7ull << 16 |
           10ull << 8 | 4;
}

static ShaderInfo *translate(uint64_t instruction, bool glsl) {
    // Keep a load observable through an original STG fixture at guest offset zero.
    uint64_t output = 0xeed8000000000000ull | 4ull << 48 | 7ull << 16 | 255ull << 8 | 4;
    uint64_t code[] = {0, instruction, output, 0xe30000000007000full, 0};
    ShaderKey key = {.glsl = glsl};
    return maxwell_translate((const uint8_t *)code, sizeof code, STAGE_COMPUTE, &key);
}

static void rejected(uint64_t instruction, bool glsl) {
    ShaderInfo *info = translate(instruction, glsl);
    assert(info && !info->msl && info->error[0]);
    maxwell_free(info);
}

static uint64_t atomic_add(bool signed_word, int dimension, unsigned slot) {
    return 0xea00000000000000ull | (uint64_t)signed_word << 51 | (uint64_t)dimension << 33 |
           (uint64_t)(72 + 2 * slot) << 36 | 7ull << 16 | 12ull << 20 | 10ull << 8 | 4;
}

static void accepted(shaderc_compiler_t compiler, shaderc_compile_options_t options, uint64_t instruction,
                     bool store, bool atomic, int dimension) {
    ShaderInfo *info = translate(instruction, true);
    if (!info->msl) { fprintf(stderr, "translation: %s\n", info->error); abort(); }
    assert(info->nimages == 1 && info->ntex == 0);
    assert(info->images[0].handle_offset == 0x130 && info->images[0].raw32 == !store &&
           info->images[0].write == (store || atomic) && info->images[0].atomic == atomic);
    assert(info->images[0].type == (dimension == 0 ? TEXTYPE_1D : TEXTYPE_2D));
    shaderc_compilation_result_t result = shaderc_compile_into_spv(compiler, info->msl, strlen(info->msl),
        shaderc_compute_shader, "original-surface-fixture", "main", options);
    if (!result || shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) {
        fprintf(stderr, "%s\n%s\n", info->msl, result ? shaderc_result_get_error_message(result) : "no shaderc result");
        abort();
    }
    VkBindingInfo bindings[8];
    int count = vk_reflect((const uint32_t *)shaderc_result_get_bytes(result), shaderc_result_get_length(result) / 4,
                           bindings, 8);
    assert(count == 2);
    int images = 0, outputs = 0;
    for (int i = 0; i < count; i++) {
        assert(bindings[i].set == 0);
        images += bindings[i].binding == 64 && bindings[i].type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        outputs += bindings[i].binding == 17 && bindings[i].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    assert(images == 1 && outputs == 1);
    shaderc_result_release(result);
    maxwell_free(info);
}

int main(void) {
    maxwell_gmem_base = 0;
    maxwell_gmem_size = 4096;
    shaderc_compiler_t compiler = shaderc_compiler_initialize();
    shaderc_compile_options_t options = shaderc_compile_options_initialize();
    assert(compiler && options);
    shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    accepted(compiler, options, surface(false, 0, 2), false, false, 0);
    accepted(compiler, options, surface(false, 3, 2), false, false, 3);
    accepted(compiler, options, surface(true, 0, 2), true, false, 0);
    accepted(compiler, options, surface(true, 3, 2), true, false, 3);
    accepted(compiler, options, atomic_add(false, 0, 2), false, true, 0);
    accepted(compiler, options, atomic_add(true, 3, 2), false, true, 3);
    accepted(compiler, options, atomic_add(true, 3, 2) | 255, false, true, 3); // discarded return still modifies the image
    accepted(compiler, options, surface(false, 0, 2) | 1ull << 23, false, false, 0);
    accepted(compiler, options, atomic_add(false, 0, 2) | 1ull << 28, false, true, 0);
    rejected(surface(false, 0, 0), false);                       // MSL remains unsupported
    rejected(surface(false, 5, 0), true);                        // unimplemented 3D
    rejected(surface(false, 0, 0) & ~(1ull << 51), true);        // bindless
    rejected(surface(false, 0, 8), true);                        // outside the image handle range
    rejected(surface(false, 0, 0) ^ (1ull << 36), true);         // unaligned handle
    rejected(surface(false, 0, 0) | (1ull << 49), true);         // unsupported out-of-bounds mode
    rejected(surface(false, 0, 0) | (1ull << 24), true);         // cache semantics not yet supported
    rejected(surface(false, 0, 0) ^ (1ull << 20), true);         // raw 64-bit load
    rejected(surface(true, 3, 0) ^ (1ull << 20), true);          // partial component store
    rejected(surface(true, 3, 0) | (1ull << 52), true);          // raw store
    rejected(atomic_add(false, 0, 0) | (2ull << 51), true);      // 64-bit atomic
    rejected(atomic_add(false, 0, 0) | (1ull << 29), true);      // MIN is not ADD
    rejected(atomic_add(false, 0, 0) | 0x0060000000000000ull, true); // bindless atomic family
    shaderc_compile_options_release(options);
    shaderc_compiler_release(compiler);
    puts("surface shaders: 9 SPIR-V/resource cases and 13 rejection cases passed; GPU execution not tested");
    return 0;
}
