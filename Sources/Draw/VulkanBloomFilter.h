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

		class VulkanBloomFilter : public VulkanPostProcessFilter {
		private:
			VkPipeline downsamplePipeline;
			VkPipeline compositePipeline;
			VkPipeline finalCompositePipeline;
			VkPipelineLayout downsampleLayout;
			VkPipelineLayout compositeLayout;
			VkPipelineLayout finalCompositeLayout;
			VkDescriptorSetLayout downsampleDescLayout;
			VkDescriptorSetLayout compositeDescLayout;
			VkDescriptorSetLayout finalCompositeDescLayout;

			struct BloomLevel {
				int width, height;
				Handle<VulkanImage> image;
				VkFramebuffer framebuffer;
			};
			std::vector<BloomLevel> levels;

			void CreatePipeline() override;
			void CreateRenderPass() override;
			void CreateLevels(int width, int height);
			void DestroyLevels();

		public:
			VulkanBloomFilter(VulkanRenderer&);
			~VulkanBloomFilter();

			void Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output) override;
		};
	}
}
