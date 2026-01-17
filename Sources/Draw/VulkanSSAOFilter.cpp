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

#include "VulkanSSAOFilter.h"
#include "VulkanRenderer.h"
#include "VulkanImage.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/Settings.h>

SPADES_SETTING(r_ssao);

namespace spades {
	namespace draw {

		VulkanSSAOFilter::VulkanSSAOFilter(VulkanRenderer& r)
		: renderer(r),
		  device(r.GetDevice()),
		  ssaoPipeline(VK_NULL_HANDLE),
		  bilateralPipeline(VK_NULL_HANDLE),
		  pipelineLayout(VK_NULL_HANDLE),
		  descriptorSetLayout(VK_NULL_HANDLE),
		  renderPass(VK_NULL_HANDLE),
		  ssaoFramebuffer(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			if (!(int)r_ssao) {
				SPLog("SSAO filter disabled");
				return;
			}

			SPLog("Creating SSAO filter");

			try {
				CreateRenderPass();
				CreatePipelines();
			} catch (...) {
				DestroyResources();
				throw;
			}
		}

		VulkanSSAOFilter::~VulkanSSAOFilter() {
			SPADES_MARK_FUNCTION();
			DestroyResources();
		}

		void VulkanSSAOFilter::CreateRenderPass() {
			SPADES_MARK_FUNCTION();

			VkAttachmentDescription colorAttachment{};
			colorAttachment.format = VK_FORMAT_R8_UNORM;
			colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			VkAttachmentReference colorAttachmentRef{};
			colorAttachmentRef.attachment = 0;
			colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass{};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorAttachmentRef;

			VkRenderPassCreateInfo renderPassInfo{};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = 1;
			renderPassInfo.pAttachments = &colorAttachment;
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;

			if (vkCreateRenderPass(device->GetDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
				SPRaise("Failed to create SSAO render pass");
			}
		}

		void VulkanSSAOFilter::CreatePipelines() {
			SPADES_MARK_FUNCTION();
			// TODO: Create SSAO and bilateral filter pipelines
			SPLog("SSAO pipeline creation deferred - needs shader support");
		}

		void VulkanSSAOFilter::DestroyResources() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			if (ssaoFramebuffer != VK_NULL_HANDLE) {
				vkDestroyFramebuffer(vkDevice, ssaoFramebuffer, nullptr);
				ssaoFramebuffer = VK_NULL_HANDLE;
			}

			ssaoImage = nullptr;
			ditherPattern = nullptr;

			if (ssaoPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, ssaoPipeline, nullptr);
				ssaoPipeline = VK_NULL_HANDLE;
			}

			if (bilateralPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, bilateralPipeline, nullptr);
				bilateralPipeline = VK_NULL_HANDLE;
			}

			if (pipelineLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, pipelineLayout, nullptr);
				pipelineLayout = VK_NULL_HANDLE;
			}

			if (descriptorSetLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(vkDevice, descriptorSetLayout, nullptr);
				descriptorSetLayout = VK_NULL_HANDLE;
			}

			if (renderPass != VK_NULL_HANDLE) {
				vkDestroyRenderPass(vkDevice, renderPass, nullptr);
				renderPass = VK_NULL_HANDLE;
			}
		}

		void VulkanSSAOFilter::Filter(VkCommandBuffer commandBuffer) {
			SPADES_MARK_FUNCTION();

			if (!(int)r_ssao) {
				return;
			}

			// TODO: Implement SSAO filtering
			// This would involve:
			// 1. Rendering SSAO using depth buffer and normal information
			// 2. Applying bilateral filter to smooth the result
			SPLog("SSAO filtering not yet implemented");
		}
	}
}
