// SPDX-License-Identifier: MIT
// Manual, headless GPU readback. Reuse the existing fixture's Vulkan allocation/compiler helpers only.
// No game, input, window or player state is loaded. CI compiles this file but does not execute it.
#define main tlds_fixture_entry_unused
#include "tlds_multisample_gpu.c"
#undef main

typedef struct { const char *name; int dim, x, y; bool store, bytes, alias, atomic; uint32_t initial, delta; } SurfaceCase;

static void instruction(uint64_t *code, size_t *n, uint64_t w) {
    if (*n % 4 == 0) code[(*n)++] = 0;
    code[(*n)++] = w;
}
static void immediate(uint64_t *code, size_t *n, unsigned reg, uint32_t value) {
    instruction(code, n, 0x0100000000000000ull | (uint64_t)value << 20 | 7ull << 16 | reg);
}
static VkPipeline surface_program(const SurfaceCase *test, VkPipelineLayout layout) {
    uint64_t code[32] = {0}; size_t n = 0;
    unsigned a = test->alias ? 4 : 10;
    immediate(code, &n, a, (uint32_t)test->x); immediate(code, &n, a + 1, (uint32_t)test->y);
    if (test->store) {
        const uint32_t values[] = {0x3f800000, 0x3e800000, 0x3f000000, 0x3f800000};
        for (unsigned i = 0; i < 4; i++) immediate(code, &n, 4 + i, values[i]);
    }
    if (test->atomic) immediate(code, &n, 12, test->delta ? test->delta : 3);
    uint64_t op = (test->store ? 0xeb20000000000000ull : 0xeb00000000000000ull) | 1ull << 51 |
        (test->store ? 0 : 1ull << 52) | (uint64_t)test->dim << 33 | 72ull << 36 |
        (uint64_t)(test->store ? 15 : 4) << 20 | 7ull << 16 | (uint64_t)a << 8 | 4;
    if (test->bytes) op |= 1ull << 23;
    if (test->atomic) op = 0xea00000000000000ull | 1ull << 51 | (uint64_t)test->dim << 33 |
        72ull << 36 | 12ull << 20 | 7ull << 16 | (uint64_t)a << 8 | 4;
    instruction(code, &n, op); instruction(code, &n, 0xe30000000007000full);
    ShaderKey key = {.glsl = 1};
    ShaderInfo *s = maxwell_translate((const uint8_t *)code, n * 8, STAGE_COMPUTE, &key);
    if (!s->msl || s->nimages != 1 || s->images[0].raw32 != !test->store ||
        s->images[0].write != (test->store || test->atomic) || s->images[0].atomic != test->atomic) {
        fprintf(stderr, "%s: translation=%s images=%d raw32=%d write=%d atomic=%d: %s\n", test->name,
                s->msl ? "ok" : "failed", s->nimages, s->images[0].raw32, s->images[0].write, s->images[0].atomic, s->error);
    }
    assert(s->msl && s->nimages == 1 && s->images[0].raw32 == !test->store &&
           s->images[0].write == (test->store || test->atomic) && s->images[0].atomic == test->atomic);
    const char *entry = strstr(s->msl, "void main()"), *end = strrchr(s->msl, '}'); assert(entry && end > entry);
    char *text = malloc(strlen(s->msl) + 256); assert(text);
    // Observation sink only: the translated instruction, bounds checks and register assignments are unchanged.
    snprintf(text, strlen(s->msl) + 256, "%.*slayout(set=0,binding=31,std430) buffer Observed { uint value; } observed;\n%.*sobserved.value=r4;\n}\n",
             (int)(entry - s->msl), s->msl, (int)(end - entry), entry);
    VkPipeline p = pipeline(text, layout); free(text); maxwell_free(s); return p;
}

static void run_case(const SurfaceCase *test, VkQueue queue, uint32_t family) {
    unsigned width = 4, height = test->dim == 0 ? 1 : 2, pixels = width * height;
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
        .imageType = test->dim == 0 ? VK_IMAGE_TYPE_1D : VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {width, height, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    VkImage image; V(vkCreateImage(device, &ci, NULL, &image));
    VkMemoryRequirements req; vkGetImageMemoryRequirements(device, image, &req);
    VkDeviceMemory imem = memory(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); V(vkBindImageMemory(device, image, imem, 0));
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = image,
        .viewType = test->dim == 0 ? VK_IMAGE_VIEW_TYPE_1D : VK_IMAGE_VIEW_TYPE_2D,
        .format = test->store ? ci.format : VK_FORMAT_R32_UINT, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkImageView view; V(vkCreateImageView(device, &vi, NULL, &view));
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 256,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VkBuffer buffer; V(vkCreateBuffer(device, &bi, NULL, &buffer)); vkGetBufferMemoryRequirements(device, buffer, &req);
    VkDeviceMemory bmem = memory(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    V(vkBindBufferMemory(device, buffer, bmem, 0)); uint32_t *mapped; V(vkMapMemory(device, bmem, 0, 256, 0, (void **)&mapped));
    for (int i = 0; i < 64; i++) mapped[i] = 0xdeadbeef;
    uint32_t base = test->initial ? test->initial : 0x10203040u;
    for (unsigned i = 0; i < pixels; i++) mapped[i] = base + i;
    VkDescriptorSetLayoutBinding bindings[] = {{31, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
                                              {64, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}};
    VkDescriptorSetLayoutCreateInfo li = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 2, .pBindings = bindings};
    VkDescriptorSetLayout sl; V(vkCreateDescriptorSetLayout(device, &li, NULL, &sl));
    VkPipelineLayoutCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &sl};
    VkPipelineLayout layout; V(vkCreatePipelineLayout(device, &pi, NULL, &layout));
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}};
    VkDescriptorPoolCreateInfo dpi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 2, .pPoolSizes = sizes};
    VkDescriptorPool pool; V(vkCreateDescriptorPool(device, &dpi, NULL, &pool));
    VkDescriptorSetAllocateInfo dai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = pool,
                                      .descriptorSetCount = 1, .pSetLayouts = &sl};
    VkDescriptorSet set; V(vkAllocateDescriptorSets(device, &dai, &set));
    VkDescriptorBufferInfo db = {buffer, 128, 4}; VkDescriptorImageInfo ii = {VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet writes[] = {
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 31, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &db},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 64, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &ii}};
    vkUpdateDescriptorSets(device, 2, writes, 0, NULL); VkPipeline pipe = surface_program(test, layout);
    VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
    VkCommandPool cp; V(vkCreateCommandPool(device, &pci, NULL, &cp));
    VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cp,
                                       .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer cb; V(vkAllocateCommandBuffers(device, &cai, &cb));
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; V(vkBeginCommandBuffer(cb, &begin));
    VkImageMemoryBarrier initial = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = image, .subresourceRange = vi.subresourceRange};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &initial);
    VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {width, height, 1}};
    vkCmdCopyBufferToImage(cb, buffer, image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
    VkMemoryBarrier ready = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                             .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &ready, 0, NULL, 0, NULL);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, NULL); vkCmdDispatch(cb, 1, 1, 1);
    ready.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; ready.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &ready, 0, NULL, 0, NULL);
    copy.bufferOffset = 64; vkCmdCopyImageToBuffer(cb, image, VK_IMAGE_LAYOUT_GENERAL, buffer, 1, &copy);
    ready.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT; ready.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &ready, 0, NULL, 0, NULL);
    V(vkEndCommandBuffer(cb)); VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb};
    V(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE)); V(vkQueueWaitIdle(queue));
    int x = test->bytes ? test->x >> 2 : test->x;
    bool inside = x >= 0 && x < (int)width && test->y >= 0 && test->y < (int)height;
    if (!test->store) assert(mapped[32] == (inside ? base + test->y * width + x : 0));
    for (unsigned i = 0; i < pixels; i++) {
        uint32_t expected = base + i;
        if (inside && i == test->y * width + x) {
            if (test->store) expected = 0xff8040ffu;
            else if (test->atomic) expected += test->delta ? test->delta : 3;
        }
        if (mapped[16+i] != expected) { fprintf(stderr, "%s pixel%u expected%08x got%08x\n", test->name, i, expected, mapped[16+i]); exit(1); }
    }
    printf("PASS %s\n", test->name);
    vkDestroyCommandPool(device, cp, NULL); vkDestroyPipeline(device, pipe, NULL); vkDestroyDescriptorPool(device, pool, NULL);
    vkDestroyPipelineLayout(device, layout, NULL); vkDestroyDescriptorSetLayout(device, sl, NULL);
    vkUnmapMemory(device, bmem); vkDestroyBuffer(device, buffer, NULL); vkFreeMemory(device, bmem, NULL);
    vkDestroyImageView(device, view, NULL); vkDestroyImage(device, image, NULL); vkFreeMemory(device, imem, NULL);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!getenv("MESA_SHADER_CACHE_DIR") || !getenv("XDG_CACHE_HOME")) { fputs("Isolate the driver caches first\n", stderr); return 2; }
    struct rlimit core = {0, 0}; setrlimit(RLIMIT_CORE, &core);
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance instance; V(vkCreateInstance(&ci, NULL, &instance));
    uint32_t count = 16; VkPhysicalDevice devices[16]; V(vkEnumeratePhysicalDevices(instance, &count, devices));
    for (uint32_t i = 0; i < count; i++) { VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(devices[i], &p);
        if (p.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU && p.apiVersion >= VK_API_VERSION_1_3) { gpu = devices[i]; break; } }
    if (!gpu) { vkDestroyInstance(instance, NULL); return 77; }
    VkPhysicalDeviceVulkan13Features have13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 have = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &have13}; vkGetPhysicalDeviceFeatures2(gpu, &have);
    if (!have.features.shaderStorageImageWriteWithoutFormat || !have13.maintenance4) { vkDestroyInstance(instance, NULL); return 77; }
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(gpu, &props);
    printf("GPU: %s vendor=%04x device=%04x driver=%u\n", props.deviceName, props.vendorID, props.deviceID, props.driverVersion);
    uint32_t nq = 32; VkQueueFamilyProperties queues[32]; vkGetPhysicalDeviceQueueFamilyProperties(gpu, &nq, queues);
    uint32_t family = UINT32_MAX; for (uint32_t i = 0; i < nq; i++) if (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { family = i; break; }
    assert(family != UINT32_MAX); float priority = 1;
    VkDeviceQueueCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
    VkPhysicalDeviceFeatures enabled = {.shaderStorageImageWriteWithoutFormat = VK_TRUE};
    VkPhysicalDeviceVulkan13Features enable13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .maintenance4 = VK_TRUE};
    VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &enable13, .pEnabledFeatures = &enabled,
                            .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi}; V(vkCreateDevice(gpu, &di, NULL, &device));
    VkQueue queue; vkGetDeviceQueue(device, family, 0, &queue); compiler = shaderc_compiler_initialize(); options = shaderc_compile_options_initialize(); assert(compiler && options);
    shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    const SurfaceCase cases[] = {
        {"raw1d",0,2,0,false,false,false}, {"raw2d",3,2,1,false,false,false},
        {"raw-byte-coordinate",0,8,0,false,true,false}, {"raw-alias-coordinate",3,2,1,false,false,true},
        {"raw-negative",0,-1,0,false,false,false}, {"raw-negative-byte",0,-4,0,false,true,false},
        {"raw-width-oob",3,4,0,false,false,false}, {"raw-height-oob",3,0,2,false,false,false},
        {"formatted1d",0,2,0,true,false,false}, {"formatted2d",3,2,1,true,false,false},
        {"formatted-negative",3,-1,0,true,false,false}, {"formatted-width-oob",3,4,0,true,false,false},
        {"atomic1d-alias-old-value",0,1,0,false,false,true,true,0,3},
        {"atomic2d-old-value",3,2,1,false,false,false,true,0,3},
        {"atomic-negative-coordinate",0,-1,0,false,false,false,true,0,3},
        {"atomic-wrap",0,0,0,false,false,false,true,0xffffffffu,3},
        {"atomic-negative-add",0,0,0,false,false,false,true,1,0xfffffffeu}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) run_case(&cases[i], queue, family);
    puts("All17 surface readback cases matched; game bindings, scaling and gameplay remain separate acceptance");
    shaderc_compile_options_release(options); shaderc_compiler_release(compiler); vkDestroyDevice(device, NULL); vkDestroyInstance(instance, NULL); return 0;
}
