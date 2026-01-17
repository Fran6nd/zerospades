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

#pragma once

#include <vulkan/vulkan.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {

		class VulkanImage : public RefCountedObject {
			Handle<gui::SDLVulkanDevice> device;
			VkImage image;
			VkDeviceMemory memory;
			VkImageView imageView;
			VkSampler sampler;

			uint32_t width;
			uint32_t height;
			VkFormat format;
			VkImageLayout currentLayout;

			bool ownsImage; // If false, image is owned externally (e.g., swapchain)

			uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

		protected:
			~VulkanImage();

		public:
			// Create image with memory allocation
			VulkanImage(Handle<gui::SDLVulkanDevice> device, uint32_t width, uint32_t height,
			            VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
			            VkMemoryPropertyFlags properties);

			// Wrap existing image (e.g., from swapchain)
			VulkanImage(Handle<gui::SDLVulkanDevice> device, VkImage existingImage,
			            uint32_t width, uint32_t height, VkFormat format);

			VkImage GetImage() const { return image; }
			VkImageView GetImageView() const { return imageView; }
			VkSampler GetSampler() const { return sampler; }
			uint32_t GetWidth() const { return width; }
			uint32_t GetHeight() const { return height; }
			VkFormat GetFormat() const { return format; }
			VkImageLayout GetCurrentLayout() const { return currentLayout; }
		Handle<gui::SDLVulkanDevice> GetDevice() const { return device; }

			// Transition image layout
			void TransitionLayout(VkCommandBuffer commandBuffer, VkImageLayout newLayout,
			                      VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask,
			                      VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask);

			// Copy data from buffer to image
			void CopyFromBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer);

			// Create image view (called automatically in constructor)
			void CreateImageView(VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT);

			// Create sampler (optional, for texture sampling)
			void CreateSampler(VkFilter magFilter = VK_FILTER_LINEAR,
			                   VkFilter minFilter = VK_FILTER_LINEAR,
			                   VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			                   bool enableAnisotropy = true);
		};

	} // namespace draw
} // namespace spades
