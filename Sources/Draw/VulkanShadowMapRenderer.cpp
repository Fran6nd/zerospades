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
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Client/SceneDefinition.h>
#include <cstring>
#include <vector>

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

			VkDevice vkDevice = device->GetDevice();

			// Load SPIR-V shaders
			auto LoadSPIRVFile = [](const char* filename) -> std::vector<uint32_t> {
				std::unique_ptr<IStream> stream = FileManager::OpenForReading(filename);
				if (!stream) {
					SPRaise("Failed to open shader file: %s", filename);
				}

				std::vector<uint8_t> buffer(stream->GetLength());
				stream->Read(buffer.data(), buffer.size());

				std::vector<uint32_t> code(buffer.size() / 4);
				std::memcpy(code.data(), buffer.data(), buffer.size());
				return code;
			};

			std::vector<uint32_t> vertCode = LoadSPIRVFile("Shaders/ShadowMap.vert.spv");
			std::vector<uint32_t> fragCode = LoadSPIRVFile("Shaders/ShadowMap.frag.spv");

			// Create shader modules
			VkShaderModuleCreateInfo vertShaderModuleInfo{};
			vertShaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			vertShaderModuleInfo.codeSize = vertCode.size() * sizeof(uint32_t);
			vertShaderModuleInfo.pCode = vertCode.data();

			VkShaderModule vertShaderModule;
			if (vkCreateShaderModule(vkDevice, &vertShaderModuleInfo, nullptr, &vertShaderModule) != VK_SUCCESS) {
				SPRaise("Failed to create vertex shader module for shadow map");
			}

			VkShaderModuleCreateInfo fragShaderModuleInfo{};
			fragShaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			fragShaderModuleInfo.codeSize = fragCode.size() * sizeof(uint32_t);
			fragShaderModuleInfo.pCode = fragCode.data();

			VkShaderModule fragShaderModule;
			if (vkCreateShaderModule(vkDevice, &fragShaderModuleInfo, nullptr, &fragShaderModule) != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
				SPRaise("Failed to create fragment shader module for shadow map");
			}

			// Shader stages
			VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
			vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
			vertShaderStageInfo.module = vertShaderModule;
			vertShaderStageInfo.pName = "main";

			VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
			fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			fragShaderStageInfo.module = fragShaderModule;
			fragShaderStageInfo.pName = "main";

			VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

			// Create descriptor set layout
			VkDescriptorSetLayoutBinding uboLayoutBinding{};
			uboLayoutBinding.binding = 0;
			uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			uboLayoutBinding.descriptorCount = 1;
			uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			uboLayoutBinding.pImmutableSamplers = nullptr;

			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = 1;
			layoutInfo.pBindings = &uboLayoutBinding;

			if (vkCreateDescriptorSetLayout(vkDevice, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
				vkDestroyShaderModule(vkDevice, fragShaderModule, nullptr);
				SPRaise("Failed to create descriptor set layout for shadow map");
			}

			// Create pipeline layout
			VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
			pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutInfo.setLayoutCount = 1;
			pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

			if (vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
				vkDestroyShaderModule(vkDevice, fragShaderModule, nullptr);
				SPRaise("Failed to create pipeline layout for shadow map");
			}

			// Vertex input (position only for shadow mapping)
			VkVertexInputBindingDescription bindingDescription{};
			bindingDescription.binding = 0;
			bindingDescription.stride = 12; // 3 floats (x, y, z)
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			VkVertexInputAttributeDescription attributeDescription{};
			attributeDescription.binding = 0;
			attributeDescription.location = 0;
			attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDescription.offset = 0;

			VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
			vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputInfo.vertexBindingDescriptionCount = 1;
			vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
			vertexInputInfo.vertexAttributeDescriptionCount = 1;
			vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

			// Input assembly
			VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
			inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			inputAssembly.primitiveRestartEnable = VK_FALSE;

			// Viewport state (dynamic)
			VkPipelineViewportStateCreateInfo viewportState{};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.pViewports = nullptr;
			viewportState.scissorCount = 1;
			viewportState.pScissors = nullptr;

			// Rasterization
			VkPipelineRasterizationStateCreateInfo rasterizer{};
			rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizer.depthClampEnable = VK_FALSE;
			rasterizer.rasterizerDiscardEnable = VK_FALSE;
			rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
			rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rasterizer.depthBiasEnable = VK_TRUE;

			// Multisampling
			VkPipelineMultisampleStateCreateInfo multisampling{};
			multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampling.sampleShadingEnable = VK_FALSE;
			multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			// Depth stencil
			VkPipelineDepthStencilStateCreateInfo depthStencil{};
			depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencil.depthTestEnable = VK_TRUE;
			depthStencil.depthWriteEnable = VK_TRUE;
			depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
			depthStencil.depthBoundsTestEnable = VK_FALSE;
			depthStencil.stencilTestEnable = VK_FALSE;

			// Color blend (no color attachments for depth-only pass)
			VkPipelineColorBlendStateCreateInfo colorBlending{};
			colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlending.logicOpEnable = VK_FALSE;
			colorBlending.attachmentCount = 0;
			colorBlending.pAttachments = nullptr;

			// Dynamic state
			VkDynamicState dynamicStates[] = {
				VK_DYNAMIC_STATE_VIEWPORT,
				VK_DYNAMIC_STATE_SCISSOR,
				VK_DYNAMIC_STATE_DEPTH_BIAS
			};

			VkPipelineDynamicStateCreateInfo dynamicState{};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = 3;
			dynamicState.pDynamicStates = dynamicStates;

			// Create graphics pipeline
			VkGraphicsPipelineCreateInfo pipelineInfo{};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = 2;
			pipelineInfo.pStages = shaderStages;
			pipelineInfo.pVertexInputState = &vertexInputInfo;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterizer;
			pipelineInfo.pMultisampleState = &multisampling;
			pipelineInfo.pDepthStencilState = &depthStencil;
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.pDynamicState = &dynamicState;
			pipelineInfo.layout = pipelineLayout;
			pipelineInfo.renderPass = renderPass;
			pipelineInfo.subpass = 0;
			pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

			VkResult result = vkCreateGraphicsPipelines(vkDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

			// Clean up shader modules
			vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
			vkDestroyShaderModule(vkDevice, fragShaderModule, nullptr);

			if (result != VK_SUCCESS) {
				SPRaise("Failed to create shadow map graphics pipeline");
			}

			SPLog("Shadow map pipeline created successfully");
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

				// Bind the shadow map pipeline
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

				// Set depth bias for shadow mapping (reduces shadow acne)
				vkCmdSetDepthBias(commandBuffer, 1.25f, 0.0f, 1.75f);

				// Render map shadow pass
				VulkanMapRenderer* mapRenderer = renderer.GetMapRenderer();
				if (mapRenderer) {
					mapRenderer->RenderShadowMapPass(commandBuffer);
				}

				// Render model shadow pass
				VulkanModelRenderer* modelRenderer = renderer.GetModelRenderer();
				if (modelRenderer) {
					modelRenderer->RenderShadowMapPass(commandBuffer);
				}

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
