/*
 Copyright (c) 2015 yvt

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
		class VulkanAutoExposureFilter : public VulkanPostProcessFilter {
		private:
			VkPipeline preprocessPipeline;
			VkPipeline computeGainPipeline;
			VkPipelineLayout preprocessLayout;
			VkPipelineLayout computeGainLayout;
			VkDescriptorSetLayout preprocessDescLayout;
			VkDescriptorSetLayout computeGainDescLayout;

			Handle<VulkanImage> exposureImage;
			VkFramebuffer exposureFramebuffer;

			void CreatePipeline() override;
			void CreateRenderPass() override;
			void CreateExposureResources();

		public:
			VulkanAutoExposureFilter(VulkanRenderer&);
			~VulkanAutoExposureFilter();

			void Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output) override;
			void Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output, float dt);
		};
	}
}
