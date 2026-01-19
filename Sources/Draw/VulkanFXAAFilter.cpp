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

#include "VulkanFXAAFilter.h"
#include "VulkanRenderer.h"
#include "VulkanImage.h"
#include "VulkanBuffer.h"
#include "VulkanProgram.h"
#include "VulkanRenderPassUtils.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>

namespace spades {
	namespace draw {

		struct FXAAUniforms {
			float inverseVP[2];
			float _pad[2];
		};

		VulkanFXAAFilter::VulkanFXAAFilter(VulkanRenderer& r)
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

		VulkanFXAAFilter::~VulkanFXAAFilter() {
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

		void VulkanFXAAFilter::CreateRenderPass() {
			renderPass = CreateSimpleColorRenderPass(device->GetDevice(), VK_FORMAT_R8G8B8A8_UNORM);
		}

		void VulkanFXAAFilter::CreateQuadBuffers() {
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

		void VulkanFXAAFilter::CreatePipeline() {
			SPADES_MARK_FUNCTION();

			VulkanProgram* program = renderer.RegisterProgram("Shaders/PostFilters/FXAA.vk.program");
			if (!program || !program->IsLinked()) {
				SPRaise("Failed to load FXAA shader program");
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
				SPRaise("Failed to create FXAA filter pipeline");
			}

			SPLog("VulkanFXAAFilter pipeline created successfully");
		}

		void VulkanFXAAFilter::CreateDescriptorPool() {
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
				SPRaise("Failed to create FXAA filter descriptor pool");
			}
		}

		void VulkanFXAAFilter::Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output) {
			SPADES_MARK_FUNCTION();

			if (!pipeline || !input || !output) {
				return;
			}

			// Setup uniforms
			FXAAUniforms uniforms;
			uniforms.inverseVP[0] = 1.0f / (float)input->GetWidth();
			uniforms.inverseVP[1] = 1.0f / (float)input->GetHeight();

			if (!uniformBuffer) {
				uniformBuffer = Handle<VulkanBuffer>::New(
					device,
					sizeof(uniforms),
					VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
				);
			}
			uniformBuffer->UpdateData(&uniforms, sizeof(uniforms));

			// Allocate descriptor set
			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &descriptorSetLayout;

			VkDescriptorSet descriptorSet;
			if (vkAllocateDescriptorSets(device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
				SPLog("Warning: Failed to allocate FXAA filter descriptor set");
				return;
			}

			// Update descriptor set
			VkDescriptorImageInfo imageInfo{};
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = input->GetImageView();
			imageInfo.sampler = input->GetSampler();

			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = uniformBuffer->GetBuffer();
			bufferInfo.offset = 0;
			bufferInfo.range = sizeof(uniforms);

			VkWriteDescriptorSet descriptorWrites[2] = {};
			descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[0].dstSet = descriptorSet;
			descriptorWrites[0].dstBinding = 0;
			descriptorWrites[0].dstArrayElement = 0;
			descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrites[0].descriptorCount = 1;
			descriptorWrites[0].pImageInfo = &imageInfo;

			descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[1].dstSet = descriptorSet;
			descriptorWrites[1].dstBinding = 1;
			descriptorWrites[1].dstArrayElement = 0;
			descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descriptorWrites[1].descriptorCount = 1;
			descriptorWrites[1].pBufferInfo = &bufferInfo;

			vkUpdateDescriptorSets(device->GetDevice(), 2, descriptorWrites, 0, nullptr);

			// Create framebuffer
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
				SPRaise("Failed to create FXAA filter framebuffer");
			}

			// Begin render pass
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

			// Cleanup
			vkFreeDescriptorSets(device->GetDevice(), descriptorPool, 1, &descriptorSet);
		}
	}
}
