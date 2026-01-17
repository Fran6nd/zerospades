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
#include <Core/Math.h>

namespace spades {
	namespace draw {
		class VulkanBuffer;
		class VulkanProgram;

		class VulkanColorCorrectionFilter : public VulkanPostProcessFilter {
		private:
			Handle<VulkanBuffer> uniformBuffer;
			Handle<VulkanBuffer> gaussUniformBuffer;
			Handle<VulkanBuffer> quadVertexBuffer;
			Handle<VulkanBuffer> quadIndexBuffer;
			VkDescriptorPool descriptorPool;
			VkFramebuffer framebuffer;

			// Gaussian blur pipeline for sharpening
			VkPipeline gaussPipeline;
			VkPipelineLayout gaussPipelineLayout;
			VkDescriptorSetLayout gaussDescriptorSetLayout;
			VkRenderPass gaussRenderPass;

			void CreatePipeline() override;
			void CreateRenderPass() override;
			void CreateQuadBuffers();
			void CreateDescriptorPool();
			void CreateGaussPipeline();
			void CreateGaussRenderPass();

		public:
			VulkanColorCorrectionFilter(VulkanRenderer&);
			~VulkanColorCorrectionFilter();

			void Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output) override;
			void Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output,
			           Vector3 tint, float fogLuminance);
		};
	}
}
