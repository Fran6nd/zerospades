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
#include <vector>
#include <unordered_map>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanImage;

		/**
		 * Pool for temporary render target images.
		 *
		 * Reduces memory allocations by reusing images with matching specifications.
		 * Images are acquired at the start of a filter pass and released when no longer
		 * needed, allowing subsequent passes to reuse the same memory.
		 */
		class VulkanTemporaryImagePool : public RefCountedObject {
		public:
			struct ImageSpec {
				uint32_t width;
				uint32_t height;
				VkFormat format;

				bool operator==(const ImageSpec& other) const {
					return width == other.width && height == other.height && format == other.format;
				}
			};

			struct ImageSpecHash {
				size_t operator()(const ImageSpec& spec) const {
					return std::hash<uint32_t>()(spec.width) ^
					       (std::hash<uint32_t>()(spec.height) << 1) ^
					       (std::hash<uint32_t>()(static_cast<uint32_t>(spec.format)) << 2);
				}
			};

		private:
			struct PooledImage {
				Handle<VulkanImage> image;
				bool inUse;
			};

			Handle<gui::SDLVulkanDevice> device;
			std::unordered_map<ImageSpec, std::vector<PooledImage>, ImageSpecHash> pools;

			size_t totalAllocations;
			size_t totalReuses;
			size_t currentInUse;

		protected:
			~VulkanTemporaryImagePool();

		public:
			explicit VulkanTemporaryImagePool(Handle<gui::SDLVulkanDevice> device);

			/**
			 * Acquire a temporary image with the given specifications.
			 * The image is marked as in-use until Release() is called.
			 * Creates a new image if no matching one is available in the pool.
			 */
			Handle<VulkanImage> Acquire(uint32_t width, uint32_t height, VkFormat format);

			/**
			 * Return a previously acquired image back to the pool.
			 * The image becomes available for reuse by subsequent Acquire() calls.
			 */
			void Return(VulkanImage* image);

			/**
			 * Release all images back to the pool.
			 * Call this at the end of each frame to reset pool state.
			 */
			void ReleaseAll();

			/**
			 * Clear all pooled images, freeing GPU memory.
			 * Call this when the renderer is shutting down or recreating the swapchain.
			 */
			void Clear();

			/**
			 * Get statistics about pool usage.
			 */
			size_t GetTotalAllocations() const { return totalAllocations; }
			size_t GetTotalReuses() const { return totalReuses; }
			size_t GetCurrentInUse() const { return currentInUse; }
			size_t GetPooledImageCount() const;
		};

	} // namespace draw
} // namespace spades
