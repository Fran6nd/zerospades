/*
 Copyright (c) 2016 yvt

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

#include <vulkan/vulkan.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanRenderer;
		class VulkanImage;
		class VulkanBuffer;
		class VulkanProgram;
		class VulkanFramebufferManager;

		class VulkanSSAOFilter {
			VulkanRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;

			// SSAO generation pipeline
			Handle<VulkanProgram> ssaoProgram;
			VkPipeline ssaoPipeline;
			VkPipelineLayout ssaoPipelineLayout;
			VkDescriptorSetLayout ssaoDescLayout;

			// Bilateral filter pipeline
			Handle<VulkanProgram> bilateralProgram;
			VkPipeline bilateralPipeline;
			VkPipelineLayout bilateralPipelineLayout;
			VkDescriptorSetLayout bilateralDescLayout;

			// Render passes
			VkRenderPass ssaoRenderPass;
			VkRenderPass bilateralRenderPass;

			// Descriptor pool
			VkDescriptorPool descriptorPool;

			// Buffers
			Handle<VulkanBuffer> quadVertexBuffer;
			Handle<VulkanBuffer> quadIndexBuffer;
			Handle<VulkanBuffer> ssaoUniformBuffer;
			Handle<VulkanBuffer> bilateralUniformBuffer;

			// Dither pattern texture
			Handle<VulkanImage> ditherPattern;

			// SSAO output image
			Handle<VulkanImage> ssaoImage;
			VkFramebuffer ssaoFramebuffer;
			int ssaoWidth, ssaoHeight;

			void CreateQuadBuffers();
			void CreateDescriptorPool();
			void CreateRenderPass();
			void CreatePipelines();
			void CreateDitherPattern();
			void DestroyResources();

			Handle<VulkanImage> GenerateRawSSAOImage(VkCommandBuffer commandBuffer, int width, int height);
			Handle<VulkanImage> ApplyBilateralFilter(VkCommandBuffer commandBuffer, VulkanImage* input, bool direction, int width, int height);

		public:
			VulkanSSAOFilter(VulkanRenderer&);
			~VulkanSSAOFilter();

			void Filter(VkCommandBuffer commandBuffer);
			VulkanImage* GetSSAOImage() { return ssaoImage.GetPointerOrNull(); }
		};
	}
}
