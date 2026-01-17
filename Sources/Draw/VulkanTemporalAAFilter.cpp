/*
 Copyright (c) 2017 yvt

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

#include "VulkanTemporalAAFilter.h"
#include "VulkanRenderer.h"
#include "VulkanImage.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>

namespace spades {
	namespace draw {
		VulkanTemporalAAFilter::VulkanTemporalAAFilter(VulkanRenderer& r)
			: VulkanPostProcessFilter(r) {
			historyBuffer.valid = false;
			historyBuffer.framebuffer = VK_NULL_HANDLE;
			CreateRenderPass();
			CreatePipeline();
		}

		VulkanTemporalAAFilter::~VulkanTemporalAAFilter() {
			DeleteHistoryBuffer();
			DestroyResources();
		}

		void VulkanTemporalAAFilter::CreateRenderPass() {
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
				SPRaise("Failed to create temporal AA render pass");
			}
		}

		void VulkanTemporalAAFilter::CreatePipeline() {
			// TODO: Create descriptor set layout for:
			// - current frame texture
			// - history buffer texture
			// - motion vectors (derived from prevMatrix and prevViewOrigin)

			// TODO: Create pipeline layout

			// TODO: Create shader modules from SPIR-V (TemporalAA.program)

			// TODO: Create graphics pipeline with temporal AA shader

			SPLog("VulkanTemporalAAFilter pipeline created (placeholder)");
		}

		void VulkanTemporalAAFilter::DeleteHistoryBuffer() {
			if (historyBuffer.framebuffer != VK_NULL_HANDLE) {
				vkDestroyFramebuffer(device->GetDevice(), historyBuffer.framebuffer, nullptr);
				historyBuffer.framebuffer = VK_NULL_HANDLE;
			}
			historyBuffer.image = nullptr;
			historyBuffer.valid = false;
		}

		Vector2 VulkanTemporalAAFilter::GetProjectionMatrixJitter() {
			// Halton sequence jitter pattern for temporal anti-aliasing
			static const Vector2 jitterTable[] = {
				{0.0f, 0.0f},
				{0.5f, 0.333333f},
				{0.25f, 0.666667f},
				{0.75f, 0.111111f},
				{0.125f, 0.444444f},
				{0.625f, 0.777778f},
				{0.375f, 0.222222f},
				{0.875f, 0.555556f}
			};

			Vector2 jitter = jitterTable[jitterTableIndex];
			jitterTableIndex = (jitterTableIndex + 1) % (sizeof(jitterTable) / sizeof(jitterTable[0]));

			// Convert from [0,1] to [-1,1] range and scale
			return (jitter - MakeVector2(0.5f, 0.5f)) * 2.0f;
		}

		void VulkanTemporalAAFilter::Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output) {
			Filter(commandBuffer, input, output, false);
		}

		void VulkanTemporalAAFilter::Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output, bool useFxaa) {
			SPADES_MARK_FUNCTION();

			// TODO: Implement temporal anti-aliasing:
			// 1. Check if history buffer is valid and matches current resolution
			// 2. If not valid, create/recreate history buffer
			// 3. Blend current frame with history buffer using motion vectors
			// 4. Apply optional FXAA pass
			// 5. Copy result to history buffer for next frame
			// 6. Update prevMatrix and prevViewOrigin

			SPLog("VulkanTemporalAAFilter::Filter called (placeholder implementation)");
		}
	}
}
