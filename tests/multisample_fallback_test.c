// SPDX-License-Identifier: MIT
// Exercise the production fallback creation path with captured Vulkan/VMA calls, no real GPU.
#include <assert.h>
#include "../runtime/nvn/vulkan/textures.c"

VkG g_vk;
uint64_t prof_views_created;
static VkImageCreateInfo created[2];
static int images, queries, clears, barriers;

void rt_fatal_trace(void) { abort(); }
void vk_device_fault_report(void) { abort(); }
uint64_t vk_pending_seq(void) { return 1; }
VkCommandBuffer vk_upload_cmd(void) { return (VkCommandBuffer)(uintptr_t)1; }
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice device, VkFormat format,
        VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties *out) {
    (void)device;
    assert(format == VK_FORMAT_R8G8B8A8_UNORM && type == VK_IMAGE_TYPE_2D && tiling == VK_IMAGE_TILING_OPTIMAL && !flags);
    assert(usage == (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));
    memset(out, 0, sizeof *out); out->sampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_16_BIT;
    queries++; return VK_SUCCESS;
}
VkResult vmaCreateImage(VmaAllocator allocator, const VkImageCreateInfo *info, const VmaAllocationCreateInfo *allocation,
        VkImage *image, VmaAllocation *memory, VmaAllocationInfo *details) {
    (void)allocator; (void)allocation; (void)details;
    assert(images < 2); created[images] = *info; images++;
    *image = (VkImage)(uintptr_t)images; *memory = (VmaAllocation)(uintptr_t)images;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice device, const VkImageViewCreateInfo *info,
        const VkAllocationCallbacks *allocation, VkImageView *view) {
    (void)device; (void)allocation;
    assert(info->viewType == VK_IMAGE_VIEW_TYPE_2D && info->subresourceRange.levelCount == 1 &&
           info->subresourceRange.layerCount == 1 && info->format == VK_FORMAT_R8G8B8A8_UNORM);
    *view = (VkImageView)(uintptr_t)((uintptr_t)info->image + 10); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier2(VkCommandBuffer buffer, const VkDependencyInfo *info) {
    (void)buffer;
    assert(info->imageMemoryBarrierCount == 1 && info->pImageMemoryBarriers->newLayout == VK_IMAGE_LAYOUT_GENERAL);
    barriers++;
}
VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(VkCommandBuffer buffer, VkImage image, VkImageLayout layout,
        const VkClearColorValue *zero, uint32_t count, const VkImageSubresourceRange *range) {
    (void)buffer; (void)image;
    assert(layout == VK_IMAGE_LAYOUT_GENERAL && zero->uint32[0] == 0 && count == 1 && range->layerCount == 1);
    clears++;
}
int main(void) {
    VkImageView ordinary = vk_dummy_view(TEXTYPE_2D);
    assert(ordinary && images == 1 && queries == 0 && created[0].samples == VK_SAMPLE_COUNT_1_BIT);
    VkImageView multisample = vk_dummy_view(TEXTYPE_2D_MS);
    assert(multisample && multisample != ordinary && images == 2 && queries == 1);
    assert(created[1].samples == VK_SAMPLE_COUNT_4_BIT && created[1].mipLevels == 1 && created[1].arrayLayers == 1);
    assert(vk_dummy_view(TEXTYPE_2D_MS) == multisample && images == 2 && queries == 1);
    assert(clears == 2 && barriers == 2);
    puts("Production multisample fallback: supported sample count, distinct view and cached reuse passed");
    return 0;
}
