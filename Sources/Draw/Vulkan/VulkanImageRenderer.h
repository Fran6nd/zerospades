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
#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanRenderer;
		class VulkanImage;
		class VulkanBuffer;

		class VulkanImageRenderer : public RefCountedObject {
			struct ImageVertex {
				float x, y, u, v;
				float r, g, b, a;
			};

			struct Batch {
				VulkanImage* image;
				std::vector<ImageVertex> vertices;
				std::vector<uint32_t> indices;
			};

			VulkanRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;
			VulkanImage* image;

			float invScreenWidthFactored;
			float invScreenHeightFactored;

			std::vector<ImageVertex> vertices;
			std::vector<uint32_t> indices;
			std::vector<Batch> batches;

			Handle<VulkanBuffer> vertexBuffer;
			Handle<VulkanBuffer> indexBuffer;

			// Per-frame resources (one per swapchain image to avoid use-after-free)
			std::vector<std::vector<Handle<VulkanBuffer>>> perFrameBuffers;
			std::vector<std::vector<VulkanImage*>> perFrameImages; // Images to release after frame completes

			VkPipeline pipeline;
			VkPipelineLayout pipelineLayout;
			VkDescriptorSetLayout descriptorSetLayout;
			std::vector<VkDescriptorPool> perFrameDescriptorPools; // One pool per swapchain image

			void CreatePipeline();
			void CreateDescriptorSet();

		public:
			VulkanImageRenderer(VulkanRenderer& r);
			~VulkanImageRenderer();

			void Flush(VkCommandBuffer commandBuffer, uint32_t frameIndex);
			void SetImage(VulkanImage* img);
			void Add(float dx1, float dy1, float dx2, float dy2, float dx3, float dy3, float dx4,
			         float dy4, float sx1, float sy1, float sx2, float sy2, float sx3, float sy3,
			         float sx4, float sy4, float r, float g, float b, float a);
		};
	} // namespace draw
} // namespace spades
