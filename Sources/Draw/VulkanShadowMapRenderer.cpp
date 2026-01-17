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

#include "VulkanShadowMapRenderer.h"
#include "VulkanRenderer.h"
#include "VulkanImage.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Settings.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Client/SceneDefinition.h>

SPADES_SETTING(r_shadowMapSize);

namespace spades {
	namespace draw {

		VulkanShadowMapRenderer::VulkanShadowMapRenderer(VulkanRenderer& r)
		: renderer(r),
		  device(r.GetDevice()),
		  renderPass(VK_NULL_HANDLE),
		  pipelineLayout(VK_NULL_HANDLE),
		  descriptorSetLayout(VK_NULL_HANDLE),
		  pipeline(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			textureSize = (int)r_shadowMapSize;
			if (textureSize < 128) textureSize = 128;
			if (textureSize > 4096) textureSize = 4096;

			SPLog("Creating shadow map renderer with size %dx%d", textureSize, textureSize);

			for (int i = 0; i < NumSlices; i++) {
				framebuffers[i] = VK_NULL_HANDLE;
				shadowMapImages[i] = nullptr;
			}

			try {
				CreateRenderPass();
				CreateFramebuffers();
				CreatePipeline();
			} catch (...) {
				DestroyResources();
				throw;
			}
		}

		VulkanShadowMapRenderer::~VulkanShadowMapRenderer() {
			SPADES_MARK_FUNCTION();
			DestroyResources();
		}

		void VulkanShadowMapRenderer::CreateRenderPass() {
			SPADES_MARK_FUNCTION();

			VkAttachmentDescription depthAttachment{};
			depthAttachment.format = VK_FORMAT_D32_SFLOAT;
			depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

			VkAttachmentReference depthAttachmentRef{};
			depthAttachmentRef.attachment = 0;
			depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass{};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 0;
			subpass.pDepthStencilAttachment = &depthAttachmentRef;

			VkSubpassDependency dependency{};
			dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass = 0;
			dependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			dependency.srcAccessMask = 0;
			dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

			VkRenderPassCreateInfo renderPassInfo{};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = 1;
			renderPassInfo.pAttachments = &depthAttachment;
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = 1;
			renderPassInfo.pDependencies = &dependency;

			if (vkCreateRenderPass(device->GetDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
				SPRaise("Failed to create shadow map render pass");
			}
		}

		void VulkanShadowMapRenderer::CreateFramebuffers() {
			SPADES_MARK_FUNCTION();

			for (int i = 0; i < NumSlices; i++) {
				shadowMapImages[i] = Handle<VulkanImage>::New(
					device,
					(uint32_t)textureSize, (uint32_t)textureSize,
					VK_FORMAT_D32_SFLOAT,
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
				);

				VkImageView attachments[] = { shadowMapImages[i]->GetImageView() };

				VkFramebufferCreateInfo framebufferInfo{};
				framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				framebufferInfo.renderPass = renderPass;
				framebufferInfo.attachmentCount = 1;
				framebufferInfo.pAttachments = attachments;
				framebufferInfo.width = textureSize;
				framebufferInfo.height = textureSize;
				framebufferInfo.layers = 1;

				if (vkCreateFramebuffer(device->GetDevice(), &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS) {
					SPRaise("Failed to create shadow map framebuffer %d", i);
				}
			}
		}

		void VulkanShadowMapRenderer::CreatePipeline() {
			SPADES_MARK_FUNCTION();
			// TODO: Create shadow map rendering pipeline
			SPLog("Shadow map pipeline creation deferred - needs shader support");
		}

		void VulkanShadowMapRenderer::DestroyResources() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			if (pipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, pipeline, nullptr);
				pipeline = VK_NULL_HANDLE;
			}

			if (pipelineLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, pipelineLayout, nullptr);
				pipelineLayout = VK_NULL_HANDLE;
			}

			if (descriptorSetLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(vkDevice, descriptorSetLayout, nullptr);
				descriptorSetLayout = VK_NULL_HANDLE;
			}

			for (int i = 0; i < NumSlices; i++) {
				if (framebuffers[i] != VK_NULL_HANDLE) {
					vkDestroyFramebuffer(vkDevice, framebuffers[i], nullptr);
					framebuffers[i] = VK_NULL_HANDLE;
				}
				shadowMapImages[i] = nullptr;
			}

			if (renderPass != VK_NULL_HANDLE) {
				vkDestroyRenderPass(vkDevice, renderPass, nullptr);
				renderPass = VK_NULL_HANDLE;
			}
		}

		void VulkanShadowMapRenderer::BuildMatrix(float near, float far) {
			SPADES_MARK_FUNCTION();

			const auto& sceneDef = renderer.GetSceneDef();
			Vector3 eye = sceneDef.viewOrigin;
			Vector3 direction = sceneDef.viewAxis[2];
			Vector3 up = sceneDef.viewAxis[1];

			// Build orthographic shadow matrix
			float size = (far - near) * 0.5f;
			Vector3 center = eye + direction * (near + far) * 0.5f;
			matrix = Matrix4::FromAxis(-direction, up, Vector3::Cross(-direction, up), center);
			matrix = Matrix4::Scale(1.0f / size, 1.0f / size, 1.0f / (far - near)) * matrix;

			// Build OBB for culling
			obb = OBB3(matrix);
			vpWidth = vpHeight = size * 2.0f;
		}

		void VulkanShadowMapRenderer::Render(VkCommandBuffer commandBuffer) {
			SPADES_MARK_FUNCTION();

			// Build shadow matrices for cascaded shadow maps
			float cascadeDistances[] = { 20.0f, 60.0f, 200.0f };

			for (int i = 0; i < NumSlices; i++) {
				float near = (i == 0) ? 0.0f : cascadeDistances[i - 1];
				float far = cascadeDistances[i];

				BuildMatrix(near, far);
				matrices[i] = matrix;

				// Begin render pass for this slice
				VkRenderPassBeginInfo renderPassInfo{};
				renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				renderPassInfo.renderPass = renderPass;
				renderPassInfo.framebuffer = framebuffers[i];
				renderPassInfo.renderArea.offset = {0, 0};
				renderPassInfo.renderArea.extent = {(uint32_t)textureSize, (uint32_t)textureSize};

				VkClearValue clearValue{};
				clearValue.depthStencil = {1.0f, 0};
				renderPassInfo.clearValueCount = 1;
				renderPassInfo.pClearValues = &clearValue;

				vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

				// Render shadow casters (map and models) from light's perspective
				// Note: Full implementation requires:
				// - Shadow-specific shaders and pipeline
				// - Light space transformation matrices passed to renderers
				// - Map renderer shadow pass
				// - Model renderer shadow pass
				// For now, this is a placeholder that sets up the render pass structure

				// Set viewport and scissor for shadow map
				VkViewport viewport{};
				viewport.x = 0.0f;
				viewport.y = 0.0f;
				viewport.width = (float)textureSize;
				viewport.height = (float)textureSize;
				viewport.minDepth = 0.0f;
				viewport.maxDepth = 1.0f;
				vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

				VkRect2D scissor{};
				scissor.offset = {0, 0};
				scissor.extent = {(uint32_t)textureSize, (uint32_t)textureSize};
				vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

				// TODO: Call map renderer's shadow pass
				// TODO: Call model renderer's shadow pass with shadow caster models

				vkCmdEndRenderPass(commandBuffer);
			}
		}

		bool VulkanShadowMapRenderer::Cull(const AABB3& box) {
			// Conservative bounding sphere test
			Vector3 center = (box.min + box.max) * 0.5f;
			float radius = (box.max - box.min).GetLength() * 0.5f;
			return SphereCull(center, radius);
		}

		bool VulkanShadowMapRenderer::SphereCull(const Vector3& center, float rad) {
			// Project sphere center to shadow map space
			Vector3 centerProj = (matrix * MakeVector4(center.x, center.y, center.z, 1.0f)).GetXYZ();

			// Check if sphere is outside the shadow map viewport
			if (centerProj.x + rad < -vpWidth * 0.5f || centerProj.x - rad > vpWidth * 0.5f) {
				return true;
			}
			if (centerProj.y + rad < -vpHeight * 0.5f || centerProj.y - rad > vpHeight * 0.5f) {
				return true;
			}

			return false;
		}
	}
}
