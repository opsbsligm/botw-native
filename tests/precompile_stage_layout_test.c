// SPDX-License-Identifier: MIT
// Call the real warm-stage entry point; capture Vulkan layout contracts without a GPU.
#include <assert.h>
#include "../runtime/nvn/vulkan/exec.c"

VkG g_vk;
static int expected_stage, expected_mvec, layouts, modules, sets, pipelines;
static bool compile_fails, pipeline_fails;
static VkPipelineCache cache = (VkPipelineCache)(uintptr_t)1;

void rt_fatal_trace(void) { abort(); }
void vk_device_fault_report(void) { abort(); }
VkPipelineCache vk_precompiled_cache(void) { return cache; }
void vk_pipeline_cache_note(bool hit) { (void)hit; }
void vk_precompiled_cache_note(bool hit) { (void)hit; }
uint32_t *vk_glsl_to_spirv(const char *source, int stage, const char *name, size_t *words) {
    assert(source && stage == expected_stage && !strcmp(name, "warm"));
    *words = 1;
    return compile_fails ? NULL : calloc(1, sizeof(uint32_t));
}
int vk_reflect(const uint32_t *spv, size_t words, VkBindingInfo *out, int max) {
    assert(spv && words == 1 && out && max > 0); return 0;
}
VkShaderModule vk_module(const uint32_t *spv, size_t words) {
    assert(spv && words == 1); modules++; return (VkShaderModule)(uintptr_t)2;
}
VkDescriptorSetLayout vk_set_layout(const VkBindingInfo *binds, int count, uint32_t set, VkShaderStageFlags stages) {
    assert(binds && count == 0 && set == (expected_stage == STAGE_VERTEX ? 0u : 1u));
    assert(stages == (expected_stage == STAGE_VERTEX ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT));
    sets++; return (VkDescriptorSetLayout)(uintptr_t)3;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice dev, const VkPipelineLayoutCreateInfo *info,
        const VkAllocationCallbacks *allocator, VkPipelineLayout *layout) {
    (void)dev; (void)allocator;
    assert(info->flags == VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT);
    assert(info->setLayoutCount == (expected_stage == STAGE_VERTEX ? 1u : 2u));
    assert(info->pSetLayouts[info->setLayoutCount - 1] == (VkDescriptorSetLayout)(uintptr_t)3);
    assert(info->pushConstantRangeCount == (uint32_t)expected_mvec);
    if (expected_mvec) {
        assert(info->pPushConstantRanges->stageFlags == VK_SHADER_STAGE_VERTEX_BIT);
        assert(info->pPushConstantRanges->offset == 0 && info->pPushConstantRanges->size == MVEC_PUSH);
    }
    layouts++; *layout = (VkPipelineLayout)(uintptr_t)4; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice dev, VkPipelineCache used_cache, uint32_t count,
        const VkGraphicsPipelineCreateInfo *info, const VkAllocationCallbacks *allocator, VkPipeline *pipeline) {
    (void)dev; (void)allocator;
    assert(used_cache == cache && count == 1 && info->stageCount == 1);
    assert(info->layout == (VkPipelineLayout)(uintptr_t)4 && info->pStages->module == (VkShaderModule)(uintptr_t)2);
    assert(info->pStages->stage == (expected_stage == STAGE_VERTEX ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT));
    assert(info->flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR);
    if (pipeline_fails) return VK_ERROR_UNKNOWN;
    pipelines++; *pipeline = (VkPipeline)(uintptr_t)5; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice dev, VkPipeline pipeline, const VkAllocationCallbacks *allocator) {
    (void)dev; (void)allocator; assert(pipeline && pipelines > 0); pipelines--;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(VkDevice dev, VkPipelineLayout layout, const VkAllocationCallbacks *allocator) {
    (void)dev; (void)allocator; assert(layout && layouts > 0); layouts--;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(VkDevice dev, VkDescriptorSetLayout layout, const VkAllocationCallbacks *allocator) {
    (void)dev; (void)allocator; assert(layout && sets > 0); sets--;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice dev, VkShaderModule module, const VkAllocationCallbacks *allocator) {
    (void)dev; (void)allocator; assert(module && modules > 0); modules--;
}
int main(void) {
    ShaderInfo info = {.msl = "synthetic"};
    for (int stage = STAGE_VERTEX; stage <= STAGE_FRAGMENT; stage++)
        for (int mvec = 0; mvec <= 1; mvec++)
            for (int clamp = 0; clamp <= 1; clamp++) {
                expected_stage = stage; expected_mvec = info.mvec = mvec; g_vk.depth_clamp = clamp;
                assert(vk_pso_warm_stage(&info, stage));
                assert(!layouts && !modules && !sets && !pipelines);
            }
    compile_fails = true;
    assert(!vk_pso_warm_stage(&info, expected_stage));
    compile_fails = false; pipeline_fails = true;
    assert(!vk_pso_warm_stage(&info, expected_stage));
    assert(!layouts && !modules && !sets && !pipelines);
    cache = VK_NULL_HANDLE;
    assert(!vk_pso_warm_stage(&info, expected_stage));
    puts("Production warm-stage layouts: vertex/fragment, motion-vector push constants and failure cleanup passed");
    return 0;
}
