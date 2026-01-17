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
		class VulkanFramebufferManager;

		class VulkanSSAOFilter {
			VulkanRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;

			VkPipeline ssaoPipeline;
			VkPipeline bilateralPipeline;
			VkPipelineLayout pipelineLayout;
			VkDescriptorSetLayout descriptorSetLayout;
			VkRenderPass renderPass;

			Handle<VulkanImage> ditherPattern;
			Handle<VulkanImage> ssaoImage;
			VkFramebuffer ssaoFramebuffer;

			void CreatePipelines();
			void CreateRenderPass();
			void DestroyResources();

		public:
			VulkanSSAOFilter(VulkanRenderer&);
			~VulkanSSAOFilter();

			void Filter(VkCommandBuffer commandBuffer);
			VulkanImage* GetSSAOImage() { return ssaoImage.GetPointerOrNull(); }
		};
	}
}
