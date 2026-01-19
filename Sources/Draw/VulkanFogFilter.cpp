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

#include "VulkanFogFilter.h"
#include "VulkanRenderer.h"
#include "VulkanImage.h"
#include "VulkanBuffer.h"
#include "VulkanProgram.h"
#include "VulkanShader.h"
#include "VulkanShadowMapRenderer.h"
#include "VulkanFramebufferManager.h"
#include "VulkanRenderPassUtils.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Client/SceneDefinition.h>

namespace spades {
	namespace draw {
		VulkanFogFilter::VulkanFogFilter(VulkanRenderer& r)
			: VulkanPostProcessFilter(r),
			  uniformBuffer(nullptr),
			  quadVertexBuffer(nullptr),
			  quadIndexBuffer(nullptr),
			  descriptorPool(VK_NULL_HANDLE),
			  framebuffer(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			CreateRenderPass();
			CreateQuadBuffers();
			CreatePipeline();
			CreateDescriptorPool();
		}

		VulkanFogFilter::~VulkanFogFilter() {
			DestroyResources();

			if (descriptorPool != VK_NULL_HANDLE) {
				vkDestroyDescriptorPool(device->GetDevice(), descriptorPool, nullptr);
				descriptorPool = VK_NULL_HANDLE;
			}

			if (framebuffer != VK_NULL_HANDLE) {
				vkDestroyFramebuffer(device->GetDevice(), framebuffer, nullptr);
				framebuffer = VK_NULL_HANDLE;
			}
		}

		void VulkanFogFilter::CreateRenderPass() {
			renderPass = CreateSimpleColorRenderPass(device->GetDevice(), VK_FORMAT_R8G8B8A8_UNORM);
		}

		void VulkanFogFilter::CreateQuadBuffers() {
			SPADES_MARK_FUNCTION();

			struct QuadVertex {
				float x, y;
			};

			QuadVertex vertices[] = {
				{0.0f, 0.0f},
				{1.0f, 0.0f},
				{0.0f, 1.0f},
				{1.0f, 1.0f}
			};

			uint16_t indices[] = {0, 1, 2, 2, 1, 3};

			quadVertexBuffer = Handle<VulkanBuffer>::New(
				device,
				sizeof(vertices),
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);
			quadVertexBuffer->UpdateData(vertices, sizeof(vertices));

			quadIndexBuffer = Handle<VulkanBuffer>::New(
				device,
				sizeof(indices),
				VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);
			quadIndexBuffer->UpdateData(indices, sizeof(indices));
		}

		void VulkanFogFilter::CreatePipeline() {
			SPADES_MARK_FUNCTION();

			VulkanProgram* program = renderer.RegisterProgram("Shaders/PostFilters/Fog.program");
			if (!program || !program->IsLinked()) {
				SPRaise("Failed to load Fog shader program");
			}

			descriptorSetLayout = program->GetDescriptorSetLayout();
			pipelineLayout = program->GetPipelineLayout();

			VkVertexInputBindingDescription bindingDescription{};
			bindingDescription.binding = 0;
			bindingDescription.stride = sizeof(float) * 2;
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			VkVertexInputAttributeDescription attributeDescription{};
			attributeDescription.binding = 0;
			attributeDescription.location = 0;
			attributeDescription.format = VK_FORMAT_R32G32_SFLOAT;
			attributeDescription.offset = 0;

			VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
			vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputInfo.vertexBindingDescriptionCount = 1;
			vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
			vertexInputInfo.vertexAttributeDescriptionCount = 1;
			vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

			VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
			inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			inputAssembly.primitiveRestartEnable = VK_FALSE;

			VkPipelineViewportStateCreateInfo viewportState{};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.scissorCount = 1;

			VkPipelineRasterizationStateCreateInfo rasterizer{};
			rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizer.depthClampEnable = VK_FALSE;
			rasterizer.rasterizerDiscardEnable = VK_FALSE;
			rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = VK_CULL_MODE_NONE;
			rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rasterizer.depthBiasEnable = VK_FALSE;

			VkPipelineMultisampleStateCreateInfo multisampling{};
			multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampling.sampleShadingEnable = VK_FALSE;
			multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			VkPipelineDepthStencilStateCreateInfo depthStencil{};
			depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencil.depthTestEnable = VK_FALSE;
			depthStencil.depthWriteEnable = VK_FALSE;

			VkPipelineColorBlendAttachmentState colorBlendAttachment{};
			colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			colorBlendAttachment.blendEnable = VK_FALSE;

			VkPipelineColorBlendStateCreateInfo colorBlending{};
			colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlending.logicOpEnable = VK_FALSE;
			colorBlending.attachmentCount = 1;
			colorBlending.pAttachments = &colorBlendAttachment;

			VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
			VkPipelineDynamicStateCreateInfo dynamicState{};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = 2;
			dynamicState.pDynamicStates = dynamicStates;

			VkGraphicsPipelineCreateInfo pipelineInfo{};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = static_cast<uint32_t>(program->GetShaderStages().size());
			pipelineInfo.pStages = program->GetShaderStages().data();
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

			if (vkCreateGraphicsPipelines(device->GetDevice(), renderer.GetPipelineCache(), 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
				SPRaise("Failed to create fog filter pipeline");
			}

			SPLog("VulkanFogFilter pipeline created successfully");
		}

		void VulkanFogFilter::CreateDescriptorPool() {
			SPADES_MARK_FUNCTION();

			VkDescriptorPoolSize poolSizes[2];
			poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSizes[0].descriptorCount = 10;
			poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			poolSizes[1].descriptorCount = 10;

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 2;
			poolInfo.pPoolSizes = poolSizes;
			poolInfo.maxSets = 10;
			poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

			if (vkCreateDescriptorPool(device->GetDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
				SPRaise("Failed to create fog filter descriptor pool");
			}
		}

		void VulkanFogFilter::Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output) {
			SPADES_MARK_FUNCTION();

			if (!pipeline || !input || !output) {
				return;
			}

			const client::SceneDefinition& sceneDef = renderer.GetSceneDef();
			Vector3 fogCol = renderer.GetFogColor();
			fogCol *= fogCol;

			struct FogUniforms {
				float fov[2];
				float viewOrigin[3];
				float _pad0;
				float viewAxisUp[3];
				float _pad1;
				float viewAxisSide[3];
				float _pad2;
				float viewAxisFront[3];
				float _pad3;
				float zNearFar[2];
				float fogColor[3];
				float fogDistance;
			} uniforms;

			uniforms.fov[0] = tanf(sceneDef.fovX * 0.5f);
			uniforms.fov[1] = tanf(sceneDef.fovY * 0.5f);

			Vector3 viewOrigin = sceneDef.viewOrigin;
			Vector3 viewAxis[3] = {sceneDef.viewAxis[0], sceneDef.viewAxis[1], sceneDef.viewAxis[2]};

			if (renderer.IsRenderingMirror()) {
				viewOrigin.z = 63.f * 2.f - viewOrigin.z;
				viewAxis[0].z = -viewAxis[0].z;
				viewAxis[1].z = -viewAxis[1].z;
				viewAxis[2].z = -viewAxis[2].z;
			}

			uniforms.viewOrigin[0] = viewOrigin.x;
			uniforms.viewOrigin[1] = viewOrigin.y;
			uniforms.viewOrigin[2] = viewOrigin.z;
			uniforms.viewAxisUp[0] = viewAxis[1].x;
			uniforms.viewAxisUp[1] = viewAxis[1].y;
			uniforms.viewAxisUp[2] = viewAxis[1].z;
			uniforms.viewAxisSide[0] = viewAxis[0].x;
			uniforms.viewAxisSide[1] = viewAxis[0].y;
			uniforms.viewAxisSide[2] = viewAxis[0].z;
			uniforms.viewAxisFront[0] = viewAxis[2].x;
			uniforms.viewAxisFront[1] = viewAxis[2].y;
			uniforms.viewAxisFront[2] = viewAxis[2].z;
			uniforms.zNearFar[0] = sceneDef.zNear;
			uniforms.zNearFar[1] = sceneDef.zFar;
			uniforms.fogColor[0] = fogCol.x;
			uniforms.fogColor[1] = fogCol.y;
			uniforms.fogColor[2] = fogCol.z;
			uniforms.fogDistance = 128.0f;

			if (!uniformBuffer) {
				uniformBuffer = Handle<VulkanBuffer>::New(
					device,
					sizeof(uniforms),
					VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
				);
			}
			uniformBuffer->UpdateData(&uniforms, sizeof(uniforms));

			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &descriptorSetLayout;

			VkDescriptorSet descriptorSet;
			if (vkAllocateDescriptorSets(device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
				SPLog("Warning: Failed to allocate fog filter descriptor set");
				return;
			}

			VkDescriptorImageInfo colorImageInfo{};
			colorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			colorImageInfo.imageView = input->GetImageView();
			colorImageInfo.sampler = input->GetSampler();

			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = uniformBuffer->GetBuffer();
			bufferInfo.offset = 0;
			bufferInfo.range = sizeof(uniforms);

			std::vector<VkWriteDescriptorSet> descriptorWrites;

			VkWriteDescriptorSet colorTextureWrite{};
			colorTextureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			colorTextureWrite.dstSet = descriptorSet;
			colorTextureWrite.dstBinding = 0;
			colorTextureWrite.dstArrayElement = 0;
			colorTextureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			colorTextureWrite.descriptorCount = 1;
			colorTextureWrite.pImageInfo = &colorImageInfo;
			descriptorWrites.push_back(colorTextureWrite);

			vkUpdateDescriptorSets(device->GetDevice(), static_cast<uint32_t>(descriptorWrites.size()),
			                      descriptorWrites.data(), 0, nullptr);

			VkFramebufferCreateInfo framebufferInfo{};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = renderPass;
			framebufferInfo.attachmentCount = 1;
			VkImageView attachments[] = {output->GetImageView()};
			framebufferInfo.pAttachments = attachments;
			framebufferInfo.width = output->GetWidth();
			framebufferInfo.height = output->GetHeight();
			framebufferInfo.layers = 1;

			if (framebuffer != VK_NULL_HANDLE) {
				vkDestroyFramebuffer(device->GetDevice(), framebuffer, nullptr);
			}
			if (vkCreateFramebuffer(device->GetDevice(), &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
				SPRaise("Failed to create fog filter framebuffer");
			}

			VkRenderPassBeginInfo renderPassInfo{};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = renderPass;
			renderPassInfo.framebuffer = framebuffer;
			renderPassInfo.renderArea.offset = {0, 0};
			renderPassInfo.renderArea.extent = {output->GetWidth(), output->GetHeight()};

			vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

			VkViewport viewport{};
			viewport.x = 0.0f;
			viewport.y = 0.0f;
			viewport.width = static_cast<float>(output->GetWidth());
			viewport.height = static_cast<float>(output->GetHeight());
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

			VkRect2D scissor{};
			scissor.offset = {0, 0};
			scissor.extent = {output->GetWidth(), output->GetHeight()};
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
			                       0, 1, &descriptorSet, 0, nullptr);

			VkBuffer vertexBuffers[] = {quadVertexBuffer->GetBuffer()};
			VkDeviceSize offsets[] = {0};
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
			vkCmdBindIndexBuffer(commandBuffer, quadIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

			vkCmdDrawIndexed(commandBuffer, 6, 1, 0, 0, 0);

			vkCmdEndRenderPass(commandBuffer);

			vkFreeDescriptorSets(device->GetDevice(), descriptorPool, 1, &descriptorSet);
		}
	}
}
