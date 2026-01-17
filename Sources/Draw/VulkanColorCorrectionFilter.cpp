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

#include "VulkanColorCorrectionFilter.h"
#include "VulkanRenderer.h"
#include "VulkanImage.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>

namespace spades {
	namespace draw {
		VulkanColorCorrectionFilter::VulkanColorCorrectionFilter(VulkanRenderer& r)
			: VulkanPostProcessFilter(r) {
			CreateRenderPass();
			CreatePipeline();
		}

		VulkanColorCorrectionFilter::~VulkanColorCorrectionFilter() {
			DestroyResources();
		}

		void VulkanColorCorrectionFilter::CreateRenderPass() {
			VkAttachmentDescription colorAttachment = {};
			colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
			colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			VkAttachmentReference colorAttachmentRef = {};
			colorAttachmentRef.attachment = 0;
			colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorAttachmentRef;

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = 1;
			renderPassInfo.pAttachments = &colorAttachment;
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;

			if (vkCreateRenderPass(device->GetDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
				SPRaise("Failed to create color correction render pass");
			}
		}

		void VulkanColorCorrectionFilter::CreatePipeline() {
			// TODO: Create descriptor set layout for input texture and uniforms (tint, fogLuminance)

			// TODO: Create pipeline layout

			// TODO: Create shader modules from SPIR-V (ColorCorrection.program)

			// TODO: Create graphics pipeline with color correction shader

			SPLog("VulkanColorCorrectionFilter pipeline created (placeholder)");
		}

		void VulkanColorCorrectionFilter::Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output) {
			Filter(commandBuffer, input, output, MakeVector3(1.0f, 1.0f, 1.0f), 1.0f);
		}

		void VulkanColorCorrectionFilter::Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output,
		                                         Vector3 tint, float fogLuminance) {
			SPADES_MARK_FUNCTION();

			// TODO: Bind pipeline
			// TODO: Update uniforms with tint and fogLuminance
			// TODO: Bind descriptor set with input texture
			// TODO: Begin render pass with output framebuffer
			// TODO: Draw fullscreen quad
			// TODO: End render pass

			SPLog("VulkanColorCorrectionFilter::Filter called (placeholder implementation)");
		}
	}
}
