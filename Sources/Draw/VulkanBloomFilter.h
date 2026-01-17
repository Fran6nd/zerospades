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

#include "VulkanPostProcessFilter.h"
#include <vector>

namespace spades {
	namespace draw {
		class VulkanFramebufferManager;
		class VulkanImage;
		class VulkanBuffer;
		class VulkanProgram;

		class VulkanBloomFilter : public VulkanPostProcessFilter {
		private:
			// Downsample pipeline (with color/alpha for blending during composite)
			Handle<VulkanProgram> downsampleProgram;
			VkPipeline downsamplePipeline;
			VkPipelineLayout downsampleLayout;

			// Composite pipeline (blends levels together)
			VkPipeline compositePipeline;
			VkPipelineLayout compositeLayout;

			// Final composite pipeline (gamma-correct mix of original + bloom)
			Handle<VulkanProgram> compositeProgram;
			VkPipeline finalCompositePipeline;
			VkPipelineLayout finalCompositeLayout;

			// Descriptor set layouts
			VkDescriptorSetLayout downsampleDescLayout;
			VkDescriptorSetLayout compositeDescLayout;
			VkDescriptorSetLayout finalCompositeDescLayout;

			// Descriptor pool
			VkDescriptorPool descriptorPool;

			// Buffers
			Handle<VulkanBuffer> quadVertexBuffer;
			Handle<VulkanBuffer> quadIndexBuffer;
			Handle<VulkanBuffer> downsampleUniformBuffer;
			Handle<VulkanBuffer> compositeUniformBuffer;

			// Render passes
			VkRenderPass downsampleRenderPass;
			VkRenderPass compositeRenderPass;

			struct BloomLevel {
				int width, height;
				Handle<VulkanImage> image;
				VkFramebuffer framebuffer;
			};
			std::vector<BloomLevel> levels;

			void CreatePipeline() override;
			void CreateRenderPass() override;
			void CreateQuadBuffers();
			void CreateDescriptorPool();
			void CreateDownsampleRenderPass();
			void CreateCompositeRenderPass();
			void CreateLevels(int width, int height);
			void DestroyLevels();

		public:
			VulkanBloomFilter(VulkanRenderer&);
			~VulkanBloomFilter();

			void Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output) override;
		};
	}
}
