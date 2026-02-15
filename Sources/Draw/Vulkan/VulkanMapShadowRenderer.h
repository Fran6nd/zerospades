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

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace client {
		class GameMap;
	}
	namespace gui {
		class SDLVulkanDevice;
	}
	namespace draw {
		class VulkanRenderer;
		class VulkanImage;
		class VulkanBuffer;

		/** Generates a heightmap shadow texture from the game map (matching GLMapShadowRenderer). */
		class VulkanMapShadowRenderer {
			VulkanRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;
			client::GameMap* map;

			Handle<VulkanImage> shadowImage;
			Handle<VulkanBuffer> stagingBuffer;

			int w, h, d;

			size_t updateBitmapPitch;
			std::vector<uint32_t> updateBitmap;
			std::vector<uint32_t> bitmap;

			bool needsFullUpload;

			uint32_t GeneratePixel(int x, int y);
			void MarkUpdate(int x, int y);

		public:
			VulkanMapShadowRenderer(VulkanRenderer& renderer, client::GameMap* map);
			~VulkanMapShadowRenderer();

			void GameMapChanged(int x, int y, int z, client::GameMap*);
			void Update(VkCommandBuffer commandBuffer);

			VulkanImage* GetShadowImage() { return shadowImage.GetPointerOrNull(); }
		};
	} // namespace draw
} // namespace spades
