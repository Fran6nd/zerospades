/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "VulkanImage.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Core/Exception.h>

namespace spades {
	namespace draw {

		VulkanImage::VulkanImage(Handle<gui::SDLVulkanDevice> dev, uint32_t w, uint32_t h,
		                         VkFormat fmt, VkImageTiling tiling, VkImageUsageFlags usage,
		                         VkMemoryPropertyFlags properties)
		: device(std::move(dev)),
		  image(VK_NULL_HANDLE),
		  memory(VK_NULL_HANDLE),
		  imageView(VK_NULL_HANDLE),
		  sampler(VK_NULL_HANDLE),
		  width(w),
		  height(h),
		  format(fmt),
		  currentLayout(VK_IMAGE_LAYOUT_UNDEFINED),
		  ownsImage(true) {

			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			// Create image
			VkImageCreateInfo imageInfo{};
			imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageInfo.imageType = VK_IMAGE_TYPE_2D;
			imageInfo.extent.width = width;
			imageInfo.extent.height = height;
			imageInfo.extent.depth = 1;
			imageInfo.mipLevels = 1;
			imageInfo.arrayLayers = 1;
			imageInfo.format = format;
			imageInfo.tiling = tiling;
			imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageInfo.usage = usage;
			imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VkResult result = vkCreateImage(vkDevice, &imageInfo, nullptr, &image);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create Vulkan image (error code: %d)", result);
			}

			// Allocate memory
			VkMemoryRequirements memRequirements;
			vkGetImageMemoryRequirements(vkDevice, image, &memRequirements);

			VkMemoryAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = memRequirements.size;
			allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

			result = vkAllocateMemory(vkDevice, &allocInfo, nullptr, &memory);
			if (result != VK_SUCCESS) {
				vkDestroyImage(vkDevice, image, nullptr);
				SPRaise("Failed to allocate Vulkan image memory (error code: %d)", result);
			}

			vkBindImageMemory(vkDevice, image, memory, 0);

			// Create image view
			CreateImageView();
		}

		VulkanImage::VulkanImage(Handle<gui::SDLVulkanDevice> dev, VkImage existingImage,
		                         uint32_t w, uint32_t h, VkFormat fmt)
		: device(std::move(dev)),
		  image(existingImage),
		  memory(VK_NULL_HANDLE),
		  imageView(VK_NULL_HANDLE),
		  sampler(VK_NULL_HANDLE),
		  width(w),
		  height(h),
		  format(fmt),
		  currentLayout(VK_IMAGE_LAYOUT_UNDEFINED),
		  ownsImage(false) {

			SPADES_MARK_FUNCTION();

			// Create image view for the existing image
			CreateImageView();
		}

		VulkanImage::~VulkanImage() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			if (sampler != VK_NULL_HANDLE) {
				vkDestroySampler(vkDevice, sampler, nullptr);
			}

			if (imageView != VK_NULL_HANDLE) {
				vkDestroyImageView(vkDevice, imageView, nullptr);
			}

			if (ownsImage) {
				if (image != VK_NULL_HANDLE) {
					vkDestroyImage(vkDevice, image, nullptr);
				}

				if (memory != VK_NULL_HANDLE) {
					vkFreeMemory(vkDevice, memory, nullptr);
				}
			}
		}

		uint32_t VulkanImage::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
			VkPhysicalDeviceMemoryProperties memProperties;
			vkGetPhysicalDeviceMemoryProperties(device->GetPhysicalDevice(), &memProperties);

			for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
				if ((typeFilter & (1 << i)) &&
				    (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
					return i;
				}
			}

			SPRaise("Failed to find suitable memory type");
		}

		void VulkanImage::CreateImageView(VkImageAspectFlags aspectFlags) {
			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = image;
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = format;
			viewInfo.subresourceRange.aspectMask = aspectFlags;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.baseArrayLayer = 0;
			viewInfo.subresourceRange.layerCount = 1;

			VkResult result = vkCreateImageView(device->GetDevice(), &viewInfo, nullptr, &imageView);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create image view (error code: %d)", result);
			}
		}

		void VulkanImage::CreateSampler(VkFilter magFilter, VkFilter minFilter,
		                                VkSamplerAddressMode addressMode, bool enableAnisotropy) {
			VkSamplerCreateInfo samplerInfo{};
			samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerInfo.magFilter = magFilter;
			samplerInfo.minFilter = minFilter;
			samplerInfo.addressModeU = addressMode;
			samplerInfo.addressModeV = addressMode;
			samplerInfo.addressModeW = addressMode;

			if (enableAnisotropy) {
				samplerInfo.anisotropyEnable = VK_TRUE;
				samplerInfo.maxAnisotropy = 16.0f;
			} else {
				samplerInfo.anisotropyEnable = VK_FALSE;
				samplerInfo.maxAnisotropy = 1.0f;
			}

			samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
			samplerInfo.unnormalizedCoordinates = VK_FALSE;
			samplerInfo.compareEnable = VK_FALSE;
			samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerInfo.mipLodBias = 0.0f;
			samplerInfo.minLod = 0.0f;
			samplerInfo.maxLod = 0.0f;

			VkResult result = vkCreateSampler(device->GetDevice(), &samplerInfo, nullptr, &sampler);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create texture sampler (error code: %d)", result);
			}
		}

		void VulkanImage::TransitionLayout(VkCommandBuffer commandBuffer, VkImageLayout newLayout,
		                                   VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask,
		                                   VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask) {
			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = currentLayout;
			barrier.newLayout = newLayout;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image;
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			barrier.srcAccessMask = srcAccessMask;
			barrier.dstAccessMask = dstAccessMask;

			vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, 0,
			                     0, nullptr, 0, nullptr, 1, &barrier);

			currentLayout = newLayout;
		}

		void VulkanImage::CopyFromBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer) {
			VkBufferImageCopy region{};
			region.bufferOffset = 0;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.mipLevel = 0;
			region.imageSubresource.baseArrayLayer = 0;
			region.imageSubresource.layerCount = 1;
			region.imageOffset = {0, 0, 0};
			region.imageExtent = {width, height, 1};

			vkCmdCopyBufferToImage(commandBuffer, buffer, image,
			                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		}

	} // namespace draw
} // namespace spades
