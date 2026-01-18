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
		class VulkanBuffer;
		class VulkanProgram;

		class VulkanDepthOfFieldFilter : public VulkanPostProcessFilter {
		private:
			// CoC generation pipeline
			Handle<VulkanProgram> cocGenProgram;
			VkPipeline cocGenPipeline;
			VkPipelineLayout cocGenLayout;
			VkDescriptorSetLayout cocGenDescLayout;

			// Blur pipeline
			Handle<VulkanProgram> blurProgram;
			VkPipeline blurPipeline;
			VkPipelineLayout blurLayout;
			VkDescriptorSetLayout blurDescLayout;

			// Gauss blur pipeline (reuse from ColorCorrection)
			Handle<VulkanProgram> gaussProgram;
			VkPipeline gaussPipeline;
			VkPipelineLayout gaussLayout;
			VkDescriptorSetLayout gaussDescLayout;

			// Final mix pipeline
			Handle<VulkanProgram> finalMixProgram;
			VkPipeline finalMixPipeline;
			VkPipelineLayout finalMixLayout;
			VkDescriptorSetLayout finalMixDescLayout;

			// Render passes
			VkRenderPass cocRenderPass;
			VkRenderPass blurRenderPass;

			// Descriptor pool
			VkDescriptorPool descriptorPool;

			// Buffers
			Handle<VulkanBuffer> quadVertexBuffer;
			Handle<VulkanBuffer> quadIndexBuffer;
			Handle<VulkanBuffer> cocGenUniformBuffer;
			Handle<VulkanBuffer> blurUniformBuffer;
			Handle<VulkanBuffer> gaussUniformBuffer;
			Handle<VulkanBuffer> finalMixUniformBuffer;

			void CreatePipeline() override;
			void CreateRenderPass() override;
			void CreateQuadBuffers();
			void CreateDescriptorPool();
			void CreateCoCRenderPass();
			void CreateBlurRenderPass();

			Handle<VulkanImage> GenerateCoC(VkCommandBuffer commandBuffer, int width, int height,
				float blurDepthRange, float vignetteBlur, float globalBlur, float nearBlur, float farBlur);
			Handle<VulkanImage> BlurWithCoC(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* coc,
				float offsetX, float offsetY, int width, int height);
			Handle<VulkanImage> GaussBlur(VkCommandBuffer commandBuffer, VulkanImage* input, bool horizontal, float spread);

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
