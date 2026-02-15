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

#include "VulkanMapShadowRenderer.h"
#include "VulkanRenderer.h"
#include "VulkanImage.h"
#include "VulkanBuffer.h"
#include <Client/GameMap.h>
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>

namespace spades {
	namespace draw {

		static uint32_t BuildPixel(int distance, uint32_t color, bool side) {
			int r = (uint8_t)(color);
			int g = (uint8_t)(color >> 8);
			int b = (uint8_t)(color >> 16);

			r >>= 2;
			g >>= 2;
			b >>= 2;

			int ex1 = side ? 1 : 0, ex2 = 0, ex3 = 0;

			return r + (g << 8) + (b << 16) + (distance << 24) +
			       (ex1 << 7) + (ex2 << 15) + (ex3 << 23);
		}

		VulkanMapShadowRenderer::VulkanMapShadowRenderer(VulkanRenderer& renderer,
		                                                 client::GameMap* map)
		    : renderer(renderer), device(renderer.GetDevice()), map(map),
		      needsFullUpload(true) {
			SPADES_MARK_FUNCTION();

			w = map->Width();
			h = map->Height();
			d = map->Depth();

			updateBitmapPitch = (w + 31) / 32;
			updateBitmap.resize(updateBitmapPitch * h);
			bitmap.resize(w * h);

			// Mark all pixels for initial generation
			std::fill(updateBitmap.begin(), updateBitmap.end(), 0xffffffffUL);
			std::fill(bitmap.begin(), bitmap.end(), 0xffffffffUL);

			// Create shadow image (512x512 RGBA8)
			shadowImage = Handle<VulkanImage>::New(
				device, (uint32_t)w, (uint32_t)h,
				VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			shadowImage->CreateSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST,
			                           VK_SAMPLER_ADDRESS_MODE_REPEAT, false);

			// Create staging buffer (512*512*4 = 1MB)
			size_t bufferSize = w * h * 4;
			stagingBuffer = Handle<VulkanBuffer>::New(
				device, bufferSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			// Generate all pixels immediately
			for (int y = 0; y < h; y++) {
				for (int x = 0; x < w; x++) {
					bitmap[x + y * w] = GeneratePixel(x, y);
				}
			}
			std::fill(updateBitmap.begin(), updateBitmap.end(), 0);

			// Upload initial data via one-time command buffer
			stagingBuffer->UpdateData(bitmap.data(), w * h * 4);

			VkCommandBufferAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandPool = device->GetCommandPool();
			allocInfo.commandBufferCount = 1;

			VkCommandBuffer commandBuffer;
			if (vkAllocateCommandBuffers(device->GetDevice(), &allocInfo, &commandBuffer) != VK_SUCCESS) {
				SPRaise("Failed to allocate command buffer for shadow map upload");
			}

			VkCommandBufferBeginInfo beginInfo{};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			vkBeginCommandBuffer(commandBuffer, &beginInfo);

			shadowImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				0, VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

			shadowImage->CopyFromBuffer(commandBuffer, stagingBuffer->GetBuffer());

			shadowImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

			vkEndCommandBuffer(commandBuffer);

			VkSubmitInfo submitInfo{};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffer;

			vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
			vkQueueWaitIdle(device->GetGraphicsQueue());

			vkFreeCommandBuffers(device->GetDevice(), device->GetCommandPool(), 1, &commandBuffer);

			needsFullUpload = false;

			SPLog("Map shadow renderer created (%dx%d)", w, h);
		}

		VulkanMapShadowRenderer::~VulkanMapShadowRenderer() {
			SPADES_MARK_FUNCTION();
		}

		uint32_t VulkanMapShadowRenderer::GeneratePixel(int x, int y) {
			for (int z = 0; z < d; z++) {
				// z-plane hit
				if (map->IsSolid(x, y, z) && z < 63) {
					return BuildPixel(z, map->GetColor(x, y, z), false);
				}

				y = y + 1;
				if (y == h)
					y = 0;

				// y-plane hit
				if (map->IsSolid(x, y, z) && z < 63) {
					return BuildPixel(z + 1, map->GetColor(x, y, z), true);
				}
			}
			return BuildPixel(64, map->GetColor(x, y == h ? 0 : y, 63), false);
		}

		void VulkanMapShadowRenderer::MarkUpdate(int x, int y) {
			x &= w - 1;
			y &= h - 1;
			updateBitmap[(x >> 5) + y * updateBitmapPitch] |= 1UL << (x & 31);
		}

		void VulkanMapShadowRenderer::GameMapChanged(int x, int y, int z, client::GameMap* m) {
			MarkUpdate(x, y - z);
			MarkUpdate(x, y - z - 1);
		}

		void VulkanMapShadowRenderer::Update(VkCommandBuffer commandBuffer) {
			SPADES_MARK_FUNCTION();

			bool anyChanges = false;

			for (size_t i = 0; i < updateBitmap.size(); i++) {
				if (updateBitmap[i] == 0)
					continue;

				int y = static_cast<int>(i / updateBitmapPitch);
				int x = static_cast<int>((i - y * updateBitmapPitch) * 32);
				size_t bitmapPixelPosBase = i * 32;

				for (int j = 0; j < 32; j++) {
					uint32_t pixel = GeneratePixel(x + j, y);
					if (bitmap[bitmapPixelPosBase + j] != pixel) {
						bitmap[bitmapPixelPosBase + j] = pixel;
						anyChanges = true;
					}
				}

				updateBitmap[i] = 0;
			}

			if (!anyChanges)
				return;

			// Upload entire bitmap to staging buffer and copy to image
			stagingBuffer->UpdateData(bitmap.data(), w * h * 4);

			shadowImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

			shadowImage->CopyFromBuffer(commandBuffer, stagingBuffer->GetBuffer());

			shadowImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
		}

	} // namespace draw
} // namespace spades
