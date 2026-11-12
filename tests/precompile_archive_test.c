// SPDX-License-Identifier: MIT
// Parse the synthetic archive with the production reader, then compile every stage with shaderc.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <shaderc/shaderc.h>
#include "../runtime/nvn/shader_archive.h"
#include "../runtime/nvn/maxwell/shader.h"
int main(int argc, char **argv) {
    assert(argc == 2);
    SaSet set; char why[512]; assert(shader_archive_scan(argv[1], &set, NULL, NULL, why, sizeof why));
    assert(set.nshaders == 3 && set.nprograms == 2 && set.nsources == 1);
    int stages[6] = {0};
    shaderc_compiler_t compiler = shaderc_compiler_initialize(); assert(compiler);
    shaderc_compile_options_t options = shaderc_compile_options_initialize(); assert(options);
    shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    for (int i = 0; i < set.nshaders; i++) {
        const SaShader *s = &set.shaders[i]; stages[s->stage]++;
        ShaderKey key = {.glsl = 1, .vs_out_mask = UINT32_MAX};
        ShaderInfo *info = maxwell_translate(s->code, s->max_size, s->stage, &key);
        assert(info->msl);
        shaderc_shader_kind kind = s->stage == 0 ? shaderc_vertex_shader : s->stage == 1 ? shaderc_fragment_shader : shaderc_compute_shader;
        shaderc_compilation_result_t result = shaderc_compile_into_spv(compiler, info->msl, strlen(info->msl), kind,
            "synthetic-archive", "main", options); assert(result);
        if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success)
            fprintf(stderr, "%s\n", shaderc_result_get_error_message(result));
        assert(shaderc_result_get_compilation_status(result) == shaderc_compilation_status_success);
        shaderc_result_release(result); maxwell_free(info);
    }
    assert(stages[0] == 1 && stages[1] == 1 && stages[5] == 1);
    shaderc_compile_options_release(options); shaderc_compiler_release(compiler); shader_archive_free(&set);
    puts("Synthetic production archive: vertex/fragment/compute enumeration, translation and SPIR-V compilation passed");
    return 0;
}
