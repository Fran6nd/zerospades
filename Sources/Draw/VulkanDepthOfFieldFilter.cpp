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

#include "VulkanDepthOfFieldFilter.h"
#include "VulkanRenderer.h"
#include "VulkanImage.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>

namespace spades {
	namespace draw {
		VulkanDepthOfFieldFilter::VulkanDepthOfFieldFilter(VulkanRenderer& r)
			: VulkanPostProcessFilter(r),
			  cocGenPipeline(VK_NULL_HANDLE),
			  cocMixPipeline(VK_NULL_HANDLE),
			  gaussPipeline(VK_NULL_HANDLE),
			  blurPipeline(VK_NULL_HANDLE),
			  finalMixPipeline(VK_NULL_HANDLE),
			  cocGenLayout(VK_NULL_HANDLE),
			  cocMixLayout(VK_NULL_HANDLE),
			  gaussLayout(VK_NULL_HANDLE),
			  blurLayout(VK_NULL_HANDLE),
			  finalMixLayout(VK_NULL_HANDLE),
			  cocGenDescLayout(VK_NULL_HANDLE),
			  cocMixDescLayout(VK_NULL_HANDLE),
			  gaussDescLayout(VK_NULL_HANDLE),
			  blurDescLayout(VK_NULL_HANDLE),
			  finalMixDescLayout(VK_NULL_HANDLE) {
			CreateRenderPass();
			CreatePipeline();
		}

		VulkanDepthOfFieldFilter::~VulkanDepthOfFieldFilter() {
			if (cocGenPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), cocGenPipeline, nullptr);
			}
			if (cocMixPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), cocMixPipeline, nullptr);
			}
			if (gaussPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), gaussPipeline, nullptr);
			}
			if (blurPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), blurPipeline, nullptr);
			}
			if (finalMixPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), finalMixPipeline, nullptr);
			}

			if (cocGenLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), cocGenLayout, nullptr);
			}
			if (cocMixLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), cocMixLayout, nullptr);
			}
			if (gaussLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), gaussLayout, nullptr);
			}
			if (blurLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), blurLayout, nullptr);
			}
			if (finalMixLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), finalMixLayout, nullptr);
			}

			if (cocGenDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), cocGenDescLayout, nullptr);
			}
			if (cocMixDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), cocMixDescLayout, nullptr);
			}
			if (gaussDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), gaussDescLayout, nullptr);
			}
			if (blurDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), blurDescLayout, nullptr);
			}
			if (finalMixDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), finalMixDescLayout, nullptr);
			}

			DestroyResources();
		}

		void VulkanDepthOfFieldFilter::CreateRenderPass() {
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
				SPRaise("Failed to create depth of field render pass");
			}
		}

		void VulkanDepthOfFieldFilter::CreatePipeline() {
			// TODO: Create multiple pipelines for DoF stages:
			// - cocGenPipeline: Generate Circle of Confusion radius
			// - cocMixPipeline: Mix CoC radius
			// - gaussPipeline: Gaussian blur for CoC
			// - blurPipeline: Blur with CoC
			// - finalMixPipeline: Final composite

			SPLog("VulkanDepthOfFieldFilter pipelines created (placeholder)");
		}

		void VulkanDepthOfFieldFilter::Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output) {
			Filter(commandBuffer, input, output, 10.0f, 0.0f, 0.0f, 0.0f, 1.0f);
		}

		void VulkanDepthOfFieldFilter::Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output,
		                                      float blurDepthRange, float vignetteBlur, float globalBlur,
		                                      float nearBlur, float farBlur) {
			SPADES_MARK_FUNCTION();

			// TODO: Implement multi-stage depth of field filtering:
			// 1. Generate CoC (Circle of Confusion)
			// 2. Blur CoC
			// 3. Apply blur based on CoC
			// 4. Final composite

			SPLog("VulkanDepthOfFieldFilter::Filter called (placeholder implementation)");
		}
	}
}
