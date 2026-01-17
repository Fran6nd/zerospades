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

#include "VulkanAutoExposureFilter.h"
#include "VulkanRenderer.h"
#include "VulkanImage.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>

namespace spades {
	namespace draw {
		VulkanAutoExposureFilter::VulkanAutoExposureFilter(VulkanRenderer& r)
			: VulkanPostProcessFilter(r),
			  preprocessPipeline(VK_NULL_HANDLE),
			  computeGainPipeline(VK_NULL_HANDLE),
			  preprocessLayout(VK_NULL_HANDLE),
			  computeGainLayout(VK_NULL_HANDLE),
			  preprocessDescLayout(VK_NULL_HANDLE),
			  computeGainDescLayout(VK_NULL_HANDLE),
			  exposureFramebuffer(VK_NULL_HANDLE) {
			CreateRenderPass();
			CreatePipeline();
			CreateExposureResources();
		}

		VulkanAutoExposureFilter::~VulkanAutoExposureFilter() {
			if (exposureFramebuffer != VK_NULL_HANDLE) {
				vkDestroyFramebuffer(device->GetDevice(), exposureFramebuffer, nullptr);
			}

			if (preprocessPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), preprocessPipeline, nullptr);
			}
			if (computeGainPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), computeGainPipeline, nullptr);
			}
			if (preprocessLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), preprocessLayout, nullptr);
			}
			if (computeGainLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), computeGainLayout, nullptr);
			}
			if (preprocessDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), preprocessDescLayout, nullptr);
			}
			if (computeGainDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), computeGainDescLayout, nullptr);
			}

			DestroyResources();
		}

		void VulkanAutoExposureFilter::CreateRenderPass() {
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
				SPRaise("Failed to create auto exposure render pass");
			}
		}

		void VulkanAutoExposureFilter::CreatePipeline() {
			// TODO: Create pipelines for auto exposure:
			// - preprocessPipeline: Downsample and compute luminance
			// - computeGainPipeline: Compute exposure gain based on scene brightness

			SPLog("VulkanAutoExposureFilter pipelines created (placeholder)");
		}

		void VulkanAutoExposureFilter::CreateExposureResources() {
			// Create 1x1 framebuffer to hold scene brightness
			exposureImage = new VulkanImage(device, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			VkImageView attachments[] = { exposureImage->GetImageView() };
			VkFramebufferCreateInfo framebufferInfo = {};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = renderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = attachments;
			framebufferInfo.width = 1;
			framebufferInfo.height = 1;
			framebufferInfo.layers = 1;

			if (vkCreateFramebuffer(device->GetDevice(), &framebufferInfo, nullptr, &exposureFramebuffer) != VK_SUCCESS) {
				SPRaise("Failed to create exposure framebuffer");
			}
		}

		void VulkanAutoExposureFilter::Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output) {
			Filter(commandBuffer, input, output, 1.0f / 60.0f);
		}

		void VulkanAutoExposureFilter::Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output, float dt) {
			SPADES_MARK_FUNCTION();

			// TODO: Implement auto exposure filtering:
			// 1. Preprocess: Downsample input and compute average luminance
			// 2. Compute gain: Calculate exposure adjustment based on scene brightness
			// 3. Apply exposure to output image

			SPLog("VulkanAutoExposureFilter::Filter called (placeholder implementation)");
		}
	}
}
