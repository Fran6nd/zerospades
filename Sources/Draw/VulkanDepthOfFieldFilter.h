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

namespace spades {
	namespace draw {
		class VulkanDepthOfFieldFilter : public VulkanPostProcessFilter {
		private:
			VkPipeline cocGenPipeline;
			VkPipeline cocMixPipeline;
			VkPipeline gaussPipeline;
			VkPipeline blurPipeline;
			VkPipeline finalMixPipeline;

			VkPipelineLayout cocGenLayout;
			VkPipelineLayout cocMixLayout;
			VkPipelineLayout gaussLayout;
			VkPipelineLayout blurLayout;
			VkPipelineLayout finalMixLayout;

			VkDescriptorSetLayout cocGenDescLayout;
			VkDescriptorSetLayout cocMixDescLayout;
			VkDescriptorSetLayout gaussDescLayout;
			VkDescriptorSetLayout blurDescLayout;
			VkDescriptorSetLayout finalMixDescLayout;

			void CreatePipeline() override;
			void CreateRenderPass() override;

		public:
			VulkanDepthOfFieldFilter(VulkanRenderer&);
			~VulkanDepthOfFieldFilter();

			void Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output) override;
			void Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output,
			           float blurDepthRange, float vignetteBlur, float globalBlur,
			           float nearBlur, float farBlur);
		};
	}
}
