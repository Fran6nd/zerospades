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

#include "VulkanBloomFilter.h"
#include "VulkanRenderer.h"
#include "VulkanImage.h"
#include "VulkanFramebufferManager.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>

namespace spades {
	namespace draw {
		VulkanBloomFilter::VulkanBloomFilter(VulkanRenderer& r)
			: VulkanPostProcessFilter(r),
			  downsamplePipeline(VK_NULL_HANDLE),
			  compositePipeline(VK_NULL_HANDLE),
			  finalCompositePipeline(VK_NULL_HANDLE),
			  downsampleLayout(VK_NULL_HANDLE),
			  compositeLayout(VK_NULL_HANDLE),
			  finalCompositeLayout(VK_NULL_HANDLE),
			  downsampleDescLayout(VK_NULL_HANDLE),
			  compositeDescLayout(VK_NULL_HANDLE),
			  finalCompositeDescLayout(VK_NULL_HANDLE) {
			CreateRenderPass();
			CreatePipeline();
		}

		VulkanBloomFilter::~VulkanBloomFilter() {
			DestroyLevels();

			if (downsamplePipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), downsamplePipeline, nullptr);
			}
			if (compositePipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), compositePipeline, nullptr);
			}
			if (finalCompositePipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), finalCompositePipeline, nullptr);
			}
			if (downsampleLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), downsampleLayout, nullptr);
			}
			if (compositeLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), compositeLayout, nullptr);
			}
			if (finalCompositeLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), finalCompositeLayout, nullptr);
			}
			if (downsampleDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), downsampleDescLayout, nullptr);
			}
			if (compositeDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), compositeDescLayout, nullptr);
			}
			if (finalCompositeDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), finalCompositeDescLayout, nullptr);
			}

			DestroyResources();
		}

		void VulkanBloomFilter::CreateRenderPass() {
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
				SPRaise("Failed to create bloom render pass");
			}
		}

		void VulkanBloomFilter::CreatePipeline() {
			// TODO: Create shader modules and pipeline
			// This requires SPIR-V shaders which will be implemented separately
			SPLog("VulkanBloomFilter pipelines created (placeholder)");
		}

		void VulkanBloomFilter::CreateLevels(int width, int height) {
			DestroyLevels();

			// Create 6 mipmap levels like the GL version
			for (int i = 0; i < 6; i++) {
				int levelWidth = (width >> i);
				int levelHeight = (height >> i);
				if (levelWidth < 1) levelWidth = 1;
				if (levelHeight < 1) levelHeight = 1;

				BloomLevel level;
				level.width = levelWidth;
				level.height = levelHeight;

				// Create image for this level
				level.image = new VulkanImage(device, levelWidth, levelHeight, VK_FORMAT_R8G8B8A8_UNORM,
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

				// Create framebuffer
				VkImageView attachments[] = { level.image->GetImageView() };
				VkFramebufferCreateInfo framebufferInfo = {};
				framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				framebufferInfo.renderPass = renderPass;
				framebufferInfo.attachmentCount = 1;
				framebufferInfo.pAttachments = attachments;
				framebufferInfo.width = levelWidth;
				framebufferInfo.height = levelHeight;
				framebufferInfo.layers = 1;

				if (vkCreateFramebuffer(device->GetDevice(), &framebufferInfo, nullptr, &level.framebuffer) != VK_SUCCESS) {
					SPRaise("Failed to create bloom level framebuffer");
				}

				levels.push_back(level);
			}
		}

		void VulkanBloomFilter::DestroyLevels() {
			for (auto& level : levels) {
				if (level.framebuffer != VK_NULL_HANDLE) {
					vkDestroyFramebuffer(device->GetDevice(), level.framebuffer, nullptr);
				}
			}
			levels.clear();
		}

		void VulkanBloomFilter::Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output) {
			SPADES_MARK_FUNCTION();

			// Recreate levels if input size changed
			if (levels.empty() || levels[0].width != input->GetWidth() || levels[0].height != input->GetHeight()) {
				CreateLevels(input->GetWidth(), input->GetHeight());
			}

			// TODO: Implement actual bloom filtering with downsampling and compositing
			// For now, this is a placeholder
			SPLog("VulkanBloomFilter::Filter called (placeholder implementation)");
		}
	}
}
