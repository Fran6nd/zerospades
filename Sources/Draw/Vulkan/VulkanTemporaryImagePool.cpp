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

#include "VulkanTemporaryImagePool.h"
#include "VulkanImage.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>

namespace spades {
	namespace draw {

		VulkanTemporaryImagePool::VulkanTemporaryImagePool(Handle<gui::SDLVulkanDevice> dev)
			: device(std::move(dev)),
			  totalAllocations(0),
			  totalReuses(0),
			  currentInUse(0) {
			SPADES_MARK_FUNCTION();
		}

		VulkanTemporaryImagePool::~VulkanTemporaryImagePool() {
			SPADES_MARK_FUNCTION();
			Clear();
		}

		Handle<VulkanImage> VulkanTemporaryImagePool::Acquire(uint32_t width, uint32_t height, VkFormat format) {
			SPADES_MARK_FUNCTION();

			ImageSpec spec{width, height, format};

			auto it = pools.find(spec);
			if (it != pools.end()) {
				for (auto& pooled : it->second) {
					if (!pooled.inUse) {
						pooled.inUse = true;
						currentInUse++;
						totalReuses++;
						return pooled.image;
					}
				}
			}

			Handle<VulkanImage> newImage = new VulkanImage(
				device, width, height, format,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			);
			newImage->CreateSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR,
			                        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);

			PooledImage pooled;
			pooled.image = newImage;
			pooled.inUse = true;

			pools[spec].push_back(pooled);
			totalAllocations++;
			currentInUse++;

			return newImage;
		}

		void VulkanTemporaryImagePool::Return(VulkanImage* image) {
			SPADES_MARK_FUNCTION();

			if (!image) return;

			ImageSpec spec{image->GetWidth(), image->GetHeight(), image->GetFormat()};

			auto it = pools.find(spec);
			if (it != pools.end()) {
				for (auto& pooled : it->second) {
					if (pooled.image.GetPointerOrNull() == image && pooled.inUse) {
						pooled.inUse = false;
						currentInUse--;
						return;
					}
				}
			}
		}

		void VulkanTemporaryImagePool::ReleaseAll() {
			SPADES_MARK_FUNCTION();

			for (auto& pair : pools) {
				for (auto& pooled : pair.second) {
					pooled.inUse = false;
				}
			}
			currentInUse = 0;
		}

		void VulkanTemporaryImagePool::Clear() {
			SPADES_MARK_FUNCTION();

			vkDeviceWaitIdle(device->GetDevice());
			pools.clear();
			currentInUse = 0;
		}

		size_t VulkanTemporaryImagePool::GetPooledImageCount() const {
			size_t count = 0;
			for (const auto& pair : pools) {
				count += pair.second.size();
			}
			return count;
		}

	} // namespace draw
} // namespace spades
