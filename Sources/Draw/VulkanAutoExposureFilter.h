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
		class VulkanBuffer;
		class VulkanProgram;

		class VulkanAutoExposureFilter : public VulkanPostProcessFilter {
		private:
			// Preprocess pipeline (convert to brightness)
			Handle<VulkanProgram> preprocessProgram;
			VkPipeline preprocessPipeline;
			VkPipelineLayout preprocessLayout;
			VkDescriptorSetLayout preprocessDescLayout;

			// Downsample pipeline
			Handle<VulkanProgram> downsampleProgram;
			VkPipeline downsamplePipeline;
			VkPipelineLayout downsampleLayout;
			VkDescriptorSetLayout downsampleDescLayout;

			// Compute gain pipeline
			Handle<VulkanProgram> computeGainProgram;
			VkPipeline computeGainPipeline;
			VkPipelineLayout computeGainLayout;
			VkDescriptorSetLayout computeGainDescLayout;

			// Apply exposure pipeline
			Handle<VulkanProgram> applyProgram;
			VkPipeline applyPipeline;
			VkPipelineLayout applyLayout;
			VkDescriptorSetLayout applyDescLayout;

			// Render passes
			VkRenderPass downsampleRenderPass;
			VkRenderPass exposureRenderPass;

			// Exposure storage (1x1 texture)
			Handle<VulkanImage> exposureImage;
			VkFramebuffer exposureFramebuffer;

			// Descriptor pool
			VkDescriptorPool descriptorPool;

			// Buffers
			Handle<VulkanBuffer> quadVertexBuffer;
			Handle<VulkanBuffer> quadIndexBuffer;
			Handle<VulkanBuffer> computeGainUniformBuffer;

			void CreatePipeline() override;
			void CreateRenderPass() override;
			void CreateQuadBuffers();
			void CreateDescriptorPool();
			void CreateExposureResources();

			Handle<VulkanImage> DownsampleToLuminance(VkCommandBuffer commandBuffer, VulkanImage* input, int width, int height);
			void ComputeGain(VkCommandBuffer commandBuffer, VulkanImage* luminanceImage, float dt);

		public:
			VulkanAutoExposureFilter(VulkanRenderer&);
			~VulkanAutoExposureFilter();

			void Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output) override;
			void Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output, float dt);
		};
	}
}
