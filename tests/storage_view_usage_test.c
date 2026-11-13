// SPDX-License-Identifier: MIT
// Capture the production image-view creation path, including usage-sensitive cache reuse. No GPU is used.
#include <assert.h>
#include "../runtime/nvn/vulkan/textures.c"

VkG g_vk;
uint64_t prof_views_created;
static unsigned calls;
static VkImageUsageFlags captured_usage;
static VkComponentMapping captured_components;
static bool captured_override;
uint64_t vk_pending_seq(void) { return 1; }
void rt_fatal_trace(void) { abort(); }
void vk_device_fault_report(void) { abort(); }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice device, const VkImageViewCreateInfo *info,
        const VkAllocationCallbacks *allocation, VkImageView *view) {
    (void)device; (void)allocation;
    const VkImageViewUsageCreateInfo *use = info->pNext;
    captured_override = use != NULL;
    if (use) { assert(use->sType == VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO); captured_usage = use->usage; }
    captured_components = info->components;
    *view = (VkImageView)(uintptr_t)(100 + ++calls); return VK_SUCCESS;
}

int main(void) {
    TexBackend b = {.image = (VkImage)(uintptr_t)1, .storage_ok = true,
        .image_usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    const VkComponentMapping id = {0};
    VkImageView sampled = vk_image_view(&b, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_SRGB, 0, 1, 0, 1, id, VK_IMAGE_ASPECT_COLOR_BIT);
    assert(sampled && calls == 1 && captured_override && captured_usage == (b.image_usage & ~VK_IMAGE_USAGE_STORAGE_BIT));
    assert(vk_image_view(&b, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_SRGB, 0, 1, 0, 1, id, VK_IMAGE_ASPECT_COLOR_BIT) == sampled && calls == 1);
    VkImageView storage = image_view_usage(&b, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R32_UINT, 0, 1, 0, 1, id, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_STORAGE_BIT);
    assert(storage && calls == 2 && captured_override && captured_usage == VK_IMAGE_USAGE_STORAGE_BIT);
    VkImageView raw_sampled = vk_image_view(&b, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R32_UINT, 0, 1, 0, 1, id, VK_IMAGE_ASPECT_COLOR_BIT);
    assert(raw_sampled && raw_sampled != storage && calls == 3 && !(captured_usage & VK_IMAGE_USAGE_STORAGE_BIT));
    assert(image_view_usage(&b, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R32_UINT, 0, 1, 0, 1, id, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_STORAGE_BIT) == storage && calls == 3);
    TexBackend ordinary = {.image = (VkImage)(uintptr_t)2};
    assert(vk_image_view(&ordinary, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, 0, 1, 0, 1, id, VK_IMAGE_ASPECT_COLOR_BIT));
    assert(calls == 4 && !captured_override);
    VkComponentMapping swizzled = id; swizzled.a = VK_COMPONENT_SWIZZLE_ONE;
    assert(vk_image_view(&b, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_SRGB, 0, 1, 0, 1, swizzled, VK_IMAGE_ASPECT_COLOR_BIT));
    assert(calls == 5 && captured_override && !(captured_usage & VK_IMAGE_USAGE_STORAGE_BIT) && captured_components.a == VK_COMPONENT_SWIZZLE_ONE);
    puts("Production views: sRGB sampling, storage-only usage, distinct usage cache, swizzle and legacy creation passed");
    return 0;
}
