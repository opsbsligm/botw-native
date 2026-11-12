// SPDX-License-Identifier: MIT
// Manual headless Vulkan test. It touches no game files, settings, windows or saves.
// The caller must isolate the driver's cache with MESA_SHADER_CACHE_DIR/XDG_CACHE_HOME.
// Seed all four samples with different values, then read each through production Maxwell translation.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <vulkan/vulkan.h>
#include <shaderc/shaderc.h>
#include "../runtime/nvn/maxwell/shader.h"

#define V(call) do { VkResult r = (call); if (r != VK_SUCCESS) { \
    fprintf(stderr, "%s: Vulkan result %d\n", #call, r); exit(1); } } while (0)

static VkPhysicalDevice gpu;
static VkDevice device;
static shaderc_compiler_t compiler;
static shaderc_compile_options_t options;

static uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties p; vkGetPhysicalDeviceMemoryProperties(gpu, &p);
    for (uint32_t i = 0; i < p.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (p.memoryTypes[i].propertyFlags & flags) == flags) return i;
    fprintf(stderr, "required memory type unavailable\n"); exit(1);
}
static VkDeviceMemory memory(VkMemoryRequirements r, VkMemoryPropertyFlags flags) {
    VkMemoryAllocateInfo a = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = r.size,
        .memoryTypeIndex = memory_type(r.memoryTypeBits, flags)};
    VkDeviceMemory m; V(vkAllocateMemory(device, &a, NULL, &m)); return m;
}
static VkPipeline pipeline(const char *source, VkPipelineLayout layout) {
    shaderc_compilation_result_t r = shaderc_compile_into_spv(compiler, source, strlen(source),
        shaderc_compute_shader, "synthetic-tlds-gpu.comp", "main", options);
    assert(r);
    if (shaderc_result_get_compilation_status(r) != shaderc_compilation_status_success) {
        fprintf(stderr, "%s\n", shaderc_result_get_error_message(r)); exit(1);
    }
    VkShaderModuleCreateInfo sm = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shaderc_result_get_length(r), .pCode = (const uint32_t *)shaderc_result_get_bytes(r)};
    VkShaderModule module; V(vkCreateShaderModule(device, &sm, NULL, &module));
    VkComputePipelineCreateInfo cp = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .layout = layout,
        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = module, .pName = "main"}};
    VkPipeline p; V(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, NULL, &p));
    vkDestroyShaderModule(device, module, NULL); shaderc_result_release(r); return p;
}
static VkPipeline consumer(unsigned sample, VkPipelineLayout layout) {
    // MOV32I R8,sample; TLDS.2D.MS.F32 R0/R2,[R4,R5],R8,cbuf[0x20]; EXIT.
    uint64_t code[] = {0, 0x0100000000000008ull | (7ull << 16) | ((uint64_t)sample << 20),
        0xda00000000000000ull | (6ull << 53) | (8ull << 36) | (4ull << 50) |
            (2ull << 28) | (8ull << 20) | (7ull << 16) | (4ull << 8), 0xe30000000007000full};
    ShaderKey key = {.glsl = 1};
    ShaderInfo *s = maxwell_translate((const uint8_t *)code, sizeof code, STAGE_COMPUTE, &key);
    assert(s->msl && s->tex[0].type == TEXTYPE_2D_MS);
    const char *main = strstr(s->msl, "void main()"), *end = strrchr(s->msl, '}'); assert(main && end > main);
    char *source = malloc(strlen(s->msl) + 512); assert(source);
    // The test adds only an output sink; the translated fetch and register assignments run unchanged.
    snprintf(source, strlen(s->msl) + 512,
        "%.*slayout(set=0,binding=31,std430) buffer TestOutput { vec4 v[4]; } test_output;\n%.*s"
        "test_output.v[%u] = vec4(uintBitsToFloat(r0),uintBitsToFloat(r1),uintBitsToFloat(r2),uintBitsToFloat(r3));\n}\n",
        (int)(main - s->msl), s->msl, (int)(end - main), main, sample);
    VkPipeline p = pipeline(source, layout); free(source); maxwell_free(s); return p;
}
int main(void) {
    struct rlimit core = {0, 0}; setrlimit(RLIMIT_CORE, &core);
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "TLDS sample validation",
        .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo ic = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance instance; V(vkCreateInstance(&ic, NULL, &instance));
    uint32_t count = 16; VkPhysicalDevice devices[16]; V(vkEnumeratePhysicalDevices(instance, &count, devices));
    for (uint32_t i = 0; i < count; i++) {
        VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(devices[i], &p);
        if (p.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU && p.apiVersion >= VK_API_VERSION_1_3) { gpu = devices[i]; break; }
    }
    if (!gpu) { fprintf(stderr, "No hardware Vulkan 1.3 device\n"); vkDestroyInstance(instance, NULL); return 77; }
    VkPhysicalDeviceVulkan13Features f13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &f13};
    vkGetPhysicalDeviceFeatures2(gpu, &features);
    VkImageFormatProperties fmt;
    VkResult supported = vkGetPhysicalDeviceImageFormatProperties(gpu, VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, 0, &fmt);
    if (!features.features.shaderStorageImageMultisample || !f13.maintenance4 || supported != VK_SUCCESS ||
        !(fmt.sampleCounts & VK_SAMPLE_COUNT_4_BIT)) {
        fprintf(stderr, "Hardware cannot seed a four-sample storage image; no GPU result claimed\n");
        vkDestroyInstance(instance, NULL); return 77;
    }
    uint32_t nq = 32; VkQueueFamilyProperties queues[32]; vkGetPhysicalDeviceQueueFamilyProperties(gpu, &nq, queues);
    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < nq; i++) if (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { family = i; break; }
    assert(family != UINT32_MAX);
    float priority = 1;
    VkDeviceQueueCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family,
        .queueCount = 1, .pQueuePriorities = &priority};
    VkPhysicalDeviceFeatures enabled = {.shaderStorageImageMultisample = VK_TRUE, .shaderInt64 = features.features.shaderInt64};
    VkPhysicalDeviceVulkan13Features enabled13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .maintenance4 = VK_TRUE};
    VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &enabled13,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi, .pEnabledFeatures = &enabled};
    V(vkCreateDevice(gpu, &di, NULL, &device));
    VkQueue queue; vkGetDeviceQueue(device, family, 0, &queue);
    VkImageCreateInfo image_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT, .extent = {1, 1, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_4_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT};
    VkImage image; V(vkCreateImage(device, &image_info, NULL, &image));
    VkMemoryRequirements req; vkGetImageMemoryRequirements(device, image, &req);
    VkDeviceMemory image_memory = memory(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    V(vkBindImageMemory(device, image, image_memory, 0));
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = image_info.format,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkImageView view; V(vkCreateImageView(device, &vi, NULL, &view));
    VkBufferCreateInfo buffer_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 64, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
    VkBuffer buffer; V(vkCreateBuffer(device, &buffer_info, NULL, &buffer)); vkGetBufferMemoryRequirements(device, buffer, &req);
    VkDeviceMemory buffer_memory = memory(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    V(vkBindBufferMemory(device, buffer, buffer_memory, 0));
    float *out; V(vkMapMemory(device, buffer_memory, 0, 64, 0, (void **)&out)); memset(out, 0, 64);
    VkSamplerCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .maxLod = 0};
    VkSampler sampler; V(vkCreateSampler(device, &si, NULL, &sampler));
    VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {31, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {32, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}};
    VkDescriptorSetLayoutCreateInfo dl = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 3, .pBindings = bindings};
    VkDescriptorSetLayout set_layout; V(vkCreateDescriptorSetLayout(device, &dl, NULL, &set_layout));
    VkPipelineLayoutCreateInfo pl = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &set_layout};
    VkPipelineLayout layout; V(vkCreatePipelineLayout(device, &pl, NULL, &layout));
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}};
    VkDescriptorPoolCreateInfo dp = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 3, .pPoolSizes = sizes};
    VkDescriptorPool pool; V(vkCreateDescriptorPool(device, &dp, NULL, &pool));
    VkDescriptorSetAllocateInfo da = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool, .descriptorSetCount = 1, .pSetLayouts = &set_layout};
    VkDescriptorSet set; V(vkAllocateDescriptorSets(device, &da, &set));
    VkDescriptorImageInfo ii = {sampler, view, VK_IMAGE_LAYOUT_GENERAL}; VkDescriptorBufferInfo bi = {buffer, 0, 64};
    VkWriteDescriptorSet writes[3] = {0};
    for (int i = 0; i < 3; i++) writes[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set, .dstBinding = bindings[i].binding, .descriptorCount = 1, .descriptorType = bindings[i].descriptorType,
        .pImageInfo = i == 1 ? NULL : &ii, .pBufferInfo = i == 1 ? &bi : NULL};
    vkUpdateDescriptorSets(device, 3, writes, 0, NULL);
    compiler = shaderc_compiler_initialize(); options = shaderc_compile_options_initialize(); assert(compiler && options);
    shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    const char *seed = "#version 460\nlayout(local_size_x=1) in; layout(set=0,binding=0,rgba32f) uniform writeonly image2DMS t;"
        "void main(){ for(int i=0;i<4;i++) imageStore(t,ivec2(0),i,vec4(i+1,i+11,i+21,i+31)); }";
    VkPipeline pipes[5]; pipes[0] = pipeline(seed, layout);
    for (unsigned i = 0; i < 4; i++) pipes[i + 1] = consumer(i, layout);
    VkCommandPoolCreateInfo pc = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
    VkCommandPool command_pool; V(vkCreateCommandPool(device, &pc, NULL, &command_pool));
    VkCommandBufferAllocateInfo ca = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer cb; V(vkAllocateCommandBuffers(device, &ca, &cb));
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; V(vkBeginCommandBuffer(cb, &begin));
    VkImageMemoryBarrier initial = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = image, .subresourceRange = vi.subresourceRange};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &initial);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, NULL);
    for (int i = 0; i < 5; i++) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipes[i]); vkCmdDispatch(cb, 1, 1, 1);
        if (!i) {
            VkMemoryBarrier b = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &b, 0, NULL, 0, NULL);
        }
    }
    VkMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, NULL, 0, NULL);
    V(vkEndCommandBuffer(cb));
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb};
    V(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE)); V(vkQueueWaitIdle(queue));
    for (int i = 0; i < 4; i++) for (int c = 0; c < 4; c++)
        if (out[i * 4 + c] != (float)(i + 1 + c * 10)) {
            fprintf(stderr, "sample %d channel %d: expected %d got %g\n", i, c, i + 1 + c * 10, out[i * 4 + c]); return 1;
        }
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(gpu, &props);
    printf("GPU: %s; driver=%u; TLDS sample 0/1/2/3, all 16 float channels matched\n", props.deviceName, props.driverVersion);
    vkUnmapMemory(device, buffer_memory);
    for (int i = 0; i < 5; i++) vkDestroyPipeline(device, pipes[i], NULL);
    vkDestroyCommandPool(device, command_pool, NULL); vkDestroyDescriptorPool(device, pool, NULL);
    vkDestroyPipelineLayout(device, layout, NULL); vkDestroyDescriptorSetLayout(device, set_layout, NULL);
    vkDestroySampler(device, sampler, NULL); vkDestroyBuffer(device, buffer, NULL); vkFreeMemory(device, buffer_memory, NULL);
    vkDestroyImageView(device, view, NULL); vkDestroyImage(device, image, NULL); vkFreeMemory(device, image_memory, NULL);
    shaderc_compile_options_release(options); shaderc_compiler_release(compiler);
    vkDestroyDevice(device, NULL); vkDestroyInstance(instance, NULL); return 0;
}
