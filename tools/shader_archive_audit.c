// SPDX-License-Identifier: MIT
// Audit private RomFS shaders without creating dumps, caches or a GPU context.
// Optional hexadecimal hashes restrict translation to a previously observed failure set.
// This checks translation/SPIR-V only, not GPU pipelines, rendering or every runtime key variant.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shaderc/shaderc.h>
#include "../runtime/kernel.h"
#include "../runtime/nvn/shader_archive.h"
#include "../runtime/nvn/maxwell/shader.h"

int main(int argc, char **argv) {
    if (argc < 2 || argc > 66) { fprintf(stderr, "usage: shader-archive-audit <romfs> [hex-hash ...]\n"); return 2; }
    uint64_t hashes[64] = {0}; bool found[64] = {0};
    for (int i = 2; i < argc; i++) {
        char *end; errno = 0;
        hashes[i - 2] = strtoull(argv[i], &end, 16);
        if (errno || !argv[i][0] || argv[i][0] == '-' || *end) return 2;
    }
    SaSet set = {0}; char why[512] = "";
    if (!shader_archive_scan(argv[1], &set, NULL, NULL, why, sizeof why)) {
        fprintf(stderr, "scan failed: %s\n", why); shader_archive_free(&set); return 2;
    }
    shaderc_compiler_t compiler = shaderc_compiler_initialize();
    shaderc_compile_options_t options = shaderc_compile_options_initialize();
    if (!compiler || !options) {
        fprintf(stderr, "shaderc initialization failed\n");
        if (options) shaderc_compile_options_release(options);
        if (compiler) shaderc_compiler_release(compiler);
        shader_archive_free(&set); return 2;
    }
    shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    maxwell_gmem_base = HEAP_BASE; maxwell_gmem_size = HEAP_MAX;
    int selected = 0, translated = 0, compiled = 0;
    for (int i = 0; i < set.nshaders; i++) {
        const SaShader *s = &set.shaders[i]; bool pick = argc == 2;
        for (int k = 2; k < argc; k++)
            if (s->hash == hashes[k - 2]) pick = found[k - 2] = true;
        if (!pick) continue;
        selected++;
        ShaderKey key = {.glsl = 1, .flip_y = true, .depth_minus_one = true,
            .vs_out_mask = UINT32_MAX, .shared_size = s->shared};
        ShaderInfo *info = maxwell_translate(s->code, s->max_size, s->stage, &key);
        if (!info->msl) {
            printf("%016llx\t%d\ttranslation-failed\t%s\n", (unsigned long long)s->hash, s->stage, info->error);
        } else {
            translated++;
            shaderc_shader_kind kind = s->stage == STAGE_VERTEX ? shaderc_vertex_shader :
                s->stage == STAGE_FRAGMENT ? shaderc_fragment_shader : shaderc_compute_shader;
            shaderc_compilation_result_t result = shaderc_compile_into_spv(compiler, info->msl, strlen(info->msl),
                kind, "private-archive-shader", "main", options);
            bool ok = result && shaderc_result_get_compilation_status(result) == shaderc_compilation_status_success;
            if (ok) compiled++;
            printf("%016llx\t%d\t%s\t%s\n", (unsigned long long)s->hash, s->stage, ok ? "spirv-ok" : "spirv-failed",
                ok ? "" : result ? shaderc_result_get_error_message(result) : "no shaderc result");
            if (result) shaderc_result_release(result);
        }
        maxwell_free(info);
    }
    bool all_found = true;
    for (int k = 2; k < argc; k++)
        if (!found[k - 2]) { fprintf(stderr, "hash not found: %016llx\n", (unsigned long long)hashes[k - 2]); all_found = false; }
    printf("summary\tarchive=%d\tselected=%d\ttranslated=%d\tspirv=%d\tmissing=%d\n",
        set.nshaders, selected, translated, compiled, !all_found);
    shaderc_compile_options_release(options); shaderc_compiler_release(compiler); shader_archive_free(&set);
    return all_found && selected > 0 && compiled == selected ? 0 : 1;
}
