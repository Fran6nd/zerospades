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
#include "VulkanBuffer.h"
#include "VulkanProgram.h"
#include "VulkanFramebufferManager.h"
#include "VulkanRenderPassUtils.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/Settings.h>
#include <Core/Math.h>
#include <cstring>
#include <cmath>
#include <algorithm>

SPADES_SETTING(r_ssao);

namespace spades {
	namespace draw {

		struct SSAOUniforms {
			float zNearFar[2];
			float pixelShift[2];
			float fieldOfView[2];
			float sampleOffsetScale[2];
			float texCoordRange[4];
		};

		struct BilateralUniforms {
			float unitShift[2];
			float zNearFar[2];
			float pixelShift[4];
			int isUpsampling;
			float _pad0;
			float _pad1;
			float _pad2;
		};

		VulkanSSAOFilter::VulkanSSAOFilter(VulkanRenderer& r)
		: renderer(r),
		  device(r.GetDevice()),
		  ssaoPipeline(VK_NULL_HANDLE),
		  ssaoPipelineLayout(VK_NULL_HANDLE),
		  ssaoDescLayout(VK_NULL_HANDLE),
		  bilateralPipeline(VK_NULL_HANDLE),
		  bilateralPipelineLayout(VK_NULL_HANDLE),
		  bilateralDescLayout(VK_NULL_HANDLE),
		  ssaoRenderPass(VK_NULL_HANDLE),
		  bilateralRenderPass(VK_NULL_HANDLE),
		  descriptorPool(VK_NULL_HANDLE),
		  ssaoFramebuffer(VK_NULL_HANDLE),
		  ssaoWidth(0),
		  ssaoHeight(0) {
			SPADES_MARK_FUNCTION();

			if (!(int)r_ssao) {
				SPLog("SSAO filter disabled");
				return;
			}

			SPLog("Creating SSAO filter");

			try {
				CreateQuadBuffers();
				CreateDescriptorPool();
				CreateDitherPattern();
				CreateRenderPass();
				CreatePipelines();
			} catch (...) {
				DestroyResources();
				throw;
			}
		}

		VulkanSSAOFilter::~VulkanSSAOFilter() {
			SPADES_MARK_FUNCTION();
			vkDeviceWaitIdle(device->GetDevice());
			DestroyResources();
		}

		void VulkanSSAOFilter::CreateQuadBuffers() {
			float vertices[] = {
				0.0f, 0.0f,
				1.0f, 0.0f,
				1.0f, 1.0f,
				0.0f, 1.0f
			};

			uint16_t indices[] = { 0, 1, 2, 2, 3, 0 };

			quadVertexBuffer = new VulkanBuffer(device, sizeof(vertices),
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			void* vertexData = quadVertexBuffer->Map();
			memcpy(vertexData, vertices, sizeof(vertices));
			quadVertexBuffer->Unmap();

			quadIndexBuffer = new VulkanBuffer(device, sizeof(indices),
				VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			void* indexData = quadIndexBuffer->Map();
			memcpy(indexData, indices, sizeof(indices));
			quadIndexBuffer->Unmap();

			ssaoUniformBuffer = new VulkanBuffer(device, sizeof(SSAOUniforms),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			bilateralUniformBuffer = new VulkanBuffer(device, sizeof(BilateralUniforms),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		}

		void VulkanSSAOFilter::CreateDescriptorPool() {
			VkDescriptorPoolSize poolSizes[] = {
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 20 },
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 40 }
			};

			VkDescriptorPoolCreateInfo poolInfo = {};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 2;
			poolInfo.pPoolSizes = poolSizes;
			poolInfo.maxSets = 30;
			poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

			if (vkCreateDescriptorPool(device->GetDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
				SPRaise("Failed to create SSAO descriptor pool");
			}
		}

		void VulkanSSAOFilter::CreateDitherPattern() {
			// Create a 4x4 dither pattern
			const int size = 4;
			uint8_t pattern[size * size];

			// Bayer dithering matrix 4x4
			const int bayerMatrix[4][4] = {
				{ 0,  8,  2, 10},
				{12,  4, 14,  6},
				{ 3, 11,  1,  9},
				{15,  7, 13,  5}
			};

			for (int y = 0; y < size; y++) {
				for (int x = 0; x < size; x++) {
					pattern[y * size + x] = (uint8_t)(bayerMatrix[y][x] * 17); // Scale to 0-255
				}
			}

			ditherPattern = new VulkanImage(device, size, size, VK_FORMAT_R8_UNORM,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			// Upload pattern data
			VkDeviceSize imageSize = size * size;
			Handle<VulkanBuffer> stagingBuffer = new VulkanBuffer(device, imageSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			void* data = stagingBuffer->Map();
			memcpy(data, pattern, imageSize);
			stagingBuffer->Unmap();

			// Create a one-time command buffer
			VkCommandBufferAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandPool = device->GetCommandPool();
			allocInfo.commandBufferCount = 1;

			VkCommandBuffer commandBuffer;
			vkAllocateCommandBuffers(device->GetDevice(), &allocInfo, &commandBuffer);

			VkCommandBufferBeginInfo beginInfo = {};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			vkBeginCommandBuffer(commandBuffer, &beginInfo);

			VkImageMemoryBarrier barrier = {};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = ditherPattern->GetImage();
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			VkBufferImageCopy region = {};
			region.bufferOffset = 0;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.mipLevel = 0;
			region.imageSubresource.baseArrayLayer = 0;
			region.imageSubresource.layerCount = 1;
			region.imageOffset = {0, 0, 0};
			region.imageExtent = {(uint32_t)size, (uint32_t)size, 1};

			vkCmdCopyBufferToImage(commandBuffer, stagingBuffer->GetBuffer(),
				ditherPattern->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			vkEndCommandBuffer(commandBuffer);

			// Submit and wait
			VkSubmitInfo submitInfo = {};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffer;

			vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
			vkQueueWaitIdle(device->GetGraphicsQueue());

			vkFreeCommandBuffers(device->GetDevice(), device->GetCommandPool(), 1, &commandBuffer);
		}

		void VulkanSSAOFilter::CreateRenderPass() {
			SPADES_MARK_FUNCTION();

			VkSubpassDependency dependency = {};
			dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass = 0;
			dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			ssaoRenderPass = CreateSimpleColorRenderPass(
				device->GetDevice(),
				VK_FORMAT_R8_UNORM,
				VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				&dependency
			);

			// Bilateral filter render pass (same format)
			bilateralRenderPass = ssaoRenderPass;
		}

		void VulkanSSAOFilter::CreatePipelines() {
			SPADES_MARK_FUNCTION();

			// Load programs
			ssaoProgram = renderer.RegisterProgram("Shaders/Vulkan/PostFilters/SSAO.vk.program");
			bilateralProgram = renderer.RegisterProgram("Shaders/Vulkan/PostFilters/BilateralFilter.vk.program");

			// --- SSAO pipeline ---
			ssaoDescLayout = ssaoProgram->GetDescriptorSetLayout();

			VkPipelineLayoutCreateInfo ssaoLayoutInfo = {};
			ssaoLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			ssaoLayoutInfo.setLayoutCount = 1;
			ssaoLayoutInfo.pSetLayouts = &ssaoDescLayout;

			if (vkCreatePipelineLayout(device->GetDevice(), &ssaoLayoutInfo, nullptr, &ssaoPipelineLayout) != VK_SUCCESS) {
				SPRaise("Failed to create SSAO pipeline layout");
			}

			// Vertex input
			VkVertexInputBindingDescription bindingDesc = {};
			bindingDesc.binding = 0;
			bindingDesc.stride = sizeof(float) * 2;
			bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			VkVertexInputAttributeDescription attrDesc = {};
			attrDesc.binding = 0;
			attrDesc.location = 0;
			attrDesc.format = VK_FORMAT_R32G32_SFLOAT;
			attrDesc.offset = 0;

			VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
			vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputInfo.vertexBindingDescriptionCount = 1;
			vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
			vertexInputInfo.vertexAttributeDescriptionCount = 1;
			vertexInputInfo.pVertexAttributeDescriptions = &attrDesc;

			VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
			inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			inputAssembly.primitiveRestartEnable = VK_FALSE;

			VkPipelineViewportStateCreateInfo viewportState = {};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.scissorCount = 1;

			VkPipelineRasterizationStateCreateInfo rasterizer = {};
			rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizer.depthClampEnable = VK_FALSE;
			rasterizer.rasterizerDiscardEnable = VK_FALSE;
			rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = VK_CULL_MODE_NONE;
			rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rasterizer.depthBiasEnable = VK_FALSE;

			VkPipelineMultisampleStateCreateInfo multisampling = {};
			multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampling.sampleShadingEnable = VK_FALSE;
			multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
			colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
				VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			colorBlendAttachment.blendEnable = VK_FALSE;

			VkPipelineColorBlendStateCreateInfo colorBlending = {};
			colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlending.logicOpEnable = VK_FALSE;
			colorBlending.attachmentCount = 1;
			colorBlending.pAttachments = &colorBlendAttachment;

			VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dynamicState = {};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = 2;
			dynamicState.pDynamicStates = dynamicStates;

			VkPipelineDepthStencilStateCreateInfo depthStencil = {};
			depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencil.depthTestEnable = VK_FALSE;
			depthStencil.depthWriteEnable = VK_FALSE;
			depthStencil.stencilTestEnable = VK_FALSE;

			auto ssaoStages = ssaoProgram->GetShaderStages();

			VkGraphicsPipelineCreateInfo pipelineInfo = {};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = static_cast<uint32_t>(ssaoStages.size());
			pipelineInfo.pStages = ssaoStages.data();
			pipelineInfo.pVertexInputState = &vertexInputInfo;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterizer;
			pipelineInfo.pMultisampleState = &multisampling;
			pipelineInfo.pDepthStencilState = &depthStencil;
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.pDynamicState = &dynamicState;
			pipelineInfo.layout = ssaoPipelineLayout;
			pipelineInfo.renderPass = ssaoRenderPass;
			pipelineInfo.subpass = 0;

			if (vkCreateGraphicsPipelines(device->GetDevice(), renderer.GetPipelineCache(), 1, &pipelineInfo, nullptr, &ssaoPipeline) != VK_SUCCESS) {
				SPRaise("Failed to create SSAO pipeline");
			}

			// --- Bilateral filter pipeline ---
			bilateralDescLayout = bilateralProgram->GetDescriptorSetLayout();

			VkPipelineLayoutCreateInfo bilateralLayoutInfo = {};
			bilateralLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			bilateralLayoutInfo.setLayoutCount = 1;
			bilateralLayoutInfo.pSetLayouts = &bilateralDescLayout;

			if (vkCreatePipelineLayout(device->GetDevice(), &bilateralLayoutInfo, nullptr, &bilateralPipelineLayout) != VK_SUCCESS) {
				SPRaise("Failed to create bilateral pipeline layout");
			}

			auto bilateralStages = bilateralProgram->GetShaderStages();

			pipelineInfo.stageCount = static_cast<uint32_t>(bilateralStages.size());
			pipelineInfo.pStages = bilateralStages.data();
			pipelineInfo.layout = bilateralPipelineLayout;
			pipelineInfo.renderPass = bilateralRenderPass;

			if (vkCreateGraphicsPipelines(device->GetDevice(), renderer.GetPipelineCache(), 1, &pipelineInfo, nullptr, &bilateralPipeline) != VK_SUCCESS) {
				SPRaise("Failed to create bilateral pipeline");
			}
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

			if (ssaoPipelineLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, ssaoPipelineLayout, nullptr);
				ssaoPipelineLayout = VK_NULL_HANDLE;
			}

			if (bilateralPipelineLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, bilateralPipelineLayout, nullptr);
				bilateralPipelineLayout = VK_NULL_HANDLE;
			}

			if (ssaoDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(vkDevice, ssaoDescLayout, nullptr);
				ssaoDescLayout = VK_NULL_HANDLE;
			}

			if (bilateralDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(vkDevice, bilateralDescLayout, nullptr);
				bilateralDescLayout = VK_NULL_HANDLE;
			}

			if (descriptorPool != VK_NULL_HANDLE) {
				vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
				descriptorPool = VK_NULL_HANDLE;
			}

			if (ssaoRenderPass != VK_NULL_HANDLE) {
				vkDestroyRenderPass(vkDevice, ssaoRenderPass, nullptr);
				ssaoRenderPass = VK_NULL_HANDLE;
				bilateralRenderPass = VK_NULL_HANDLE;
			}

			quadVertexBuffer = nullptr;
			quadIndexBuffer = nullptr;
			ssaoUniformBuffer = nullptr;
			bilateralUniformBuffer = nullptr;
		}

		Handle<VulkanImage> VulkanSSAOFilter::GenerateRawSSAOImage(VkCommandBuffer commandBuffer, int width, int height) {
			SPADES_MARK_FUNCTION();

			// Create output image
			Handle<VulkanImage> output = new VulkanImage(device, width, height, VK_FORMAT_R8_UNORM,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			// Create framebuffer
			VkFramebuffer framebuffer;
			VkImageView attachments[] = { output->GetImageView() };
			VkFramebufferCreateInfo framebufferInfo = {};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = ssaoRenderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = attachments;
			framebufferInfo.width = width;
			framebufferInfo.height = height;
			framebufferInfo.layers = 1;

			if (vkCreateFramebuffer(device->GetDevice(), &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
				SPRaise("Failed to create SSAO framebuffer");
			}

			// Get scene definition for uniforms
			const client::SceneDefinition& def = renderer.GetSceneDef();

			// Update uniforms
			SSAOUniforms uniforms;
			uniforms.zNearFar[0] = def.zNear;
			uniforms.zNearFar[1] = def.zFar;
			uniforms.pixelShift[0] = 1.0f / (float)width;
			uniforms.pixelShift[1] = 1.0f / (float)height;
			uniforms.fieldOfView[0] = std::tan(def.fovX * 0.5f);
			uniforms.fieldOfView[1] = std::tan(def.fovY * 0.5f);

			float kernelSize = std::max(1.0f, std::min((float)width, (float)height) * 0.0018f);
			uniforms.sampleOffsetScale[0] = kernelSize / (float)width;
			uniforms.sampleOffsetScale[1] = kernelSize / (float)height;

			int renderWidth = (int)renderer.ScreenWidth();
			if (width < renderWidth) {
				// 2x downsampling
				uniforms.texCoordRange[0] = 0.25f / width;
				uniforms.texCoordRange[1] = 0.25f / height;
				uniforms.texCoordRange[2] = 1.0f;
				uniforms.texCoordRange[3] = 1.0f;
			} else {
				uniforms.texCoordRange[0] = 0.0f;
				uniforms.texCoordRange[1] = 0.0f;
				uniforms.texCoordRange[2] = 1.0f;
				uniforms.texCoordRange[3] = 1.0f;
			}

			void* data = ssaoUniformBuffer->Map();
			memcpy(data, &uniforms, sizeof(uniforms));
			ssaoUniformBuffer->Unmap();

			// Allocate descriptor set
			VkDescriptorSetAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &ssaoDescLayout;

			VkDescriptorSet descriptorSet;
			if (vkAllocateDescriptorSets(device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
				SPRaise("Failed to allocate SSAO descriptor set");
			}

			// Get depth texture from framebuffer manager
			VulkanImage* depthImage = renderer.GetFramebufferManager()->GetDepthImage().GetPointerOrNull();

			// Update descriptor set
			VkDescriptorBufferInfo bufferInfo = {};
			bufferInfo.buffer = ssaoUniformBuffer->GetBuffer();
			bufferInfo.offset = 0;
			bufferInfo.range = sizeof(SSAOUniforms);

			VkDescriptorImageInfo depthImageInfo = {};
			depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			depthImageInfo.imageView = depthImage->GetImageView();
			depthImageInfo.sampler = depthImage->GetSampler();

			VkDescriptorImageInfo ditherImageInfo = {};
			ditherImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			ditherImageInfo.imageView = ditherPattern->GetImageView();
			ditherImageInfo.sampler = ditherPattern->GetSampler();

			VkWriteDescriptorSet descriptorWrites[3] = {};
			descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[0].dstSet = descriptorSet;
			descriptorWrites[0].dstBinding = 0;
			descriptorWrites[0].dstArrayElement = 0;
			descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descriptorWrites[0].descriptorCount = 1;
			descriptorWrites[0].pBufferInfo = &bufferInfo;

			descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[1].dstSet = descriptorSet;
			descriptorWrites[1].dstBinding = 1;
			descriptorWrites[1].dstArrayElement = 0;
			descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrites[1].descriptorCount = 1;
			descriptorWrites[1].pImageInfo = &depthImageInfo;

			descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[2].dstSet = descriptorSet;
			descriptorWrites[2].dstBinding = 2;
			descriptorWrites[2].dstArrayElement = 0;
			descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrites[2].descriptorCount = 1;
			descriptorWrites[2].pImageInfo = &ditherImageInfo;

			vkUpdateDescriptorSets(device->GetDevice(), 3, descriptorWrites, 0, nullptr);

			// Begin render pass
			VkRenderPassBeginInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = ssaoRenderPass;
			renderPassInfo.framebuffer = framebuffer;
			renderPassInfo.renderArea.offset = { 0, 0 };
			renderPassInfo.renderArea.extent = { (uint32_t)width, (uint32_t)height };

			vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ssaoPipeline);

			VkViewport viewport = {};
			viewport.x = 0.0f;
			viewport.y = 0.0f;
			viewport.width = (float)width;
			viewport.height = (float)height;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

			VkRect2D scissor = {};
			scissor.offset = { 0, 0 };
			scissor.extent = { (uint32_t)width, (uint32_t)height };
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				ssaoPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

			VkBuffer vertexBuffers[] = { quadVertexBuffer->GetBuffer() };
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
			vkCmdBindIndexBuffer(commandBuffer, quadIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);
			vkCmdDrawIndexed(commandBuffer, 6, 1, 0, 0, 0);

			vkCmdEndRenderPass(commandBuffer);

			vkFreeDescriptorSets(device->GetDevice(), descriptorPool, 1, &descriptorSet);
			vkDestroyFramebuffer(device->GetDevice(), framebuffer, nullptr);

			return output;
		}

		Handle<VulkanImage> VulkanSSAOFilter::ApplyBilateralFilter(VkCommandBuffer commandBuffer, VulkanImage* input, bool direction, int width, int height) {
			SPADES_MARK_FUNCTION();

			// Create output image
			Handle<VulkanImage> output = new VulkanImage(device, width, height, VK_FORMAT_R8_UNORM,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			// Create framebuffer
			VkFramebuffer framebuffer;
			VkImageView attachments[] = { output->GetImageView() };
			VkFramebufferCreateInfo framebufferInfo = {};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = bilateralRenderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = attachments;
			framebufferInfo.width = width;
			framebufferInfo.height = height;
			framebufferInfo.layers = 1;

			if (vkCreateFramebuffer(device->GetDevice(), &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
				SPRaise("Failed to create bilateral framebuffer");
			}

			// Get scene definition for uniforms
			const client::SceneDefinition& def = renderer.GetSceneDef();

			// Update uniforms
			BilateralUniforms uniforms;
			uniforms.unitShift[0] = direction ? 1.0f / (float)width : 0.0f;
			uniforms.unitShift[1] = direction ? 0.0f : 1.0f / (float)height;
			uniforms.zNearFar[0] = def.zNear;
			uniforms.zNearFar[1] = def.zFar;
			uniforms.pixelShift[0] = 1.0f / (float)width;
			uniforms.pixelShift[1] = 1.0f / (float)height;
			uniforms.pixelShift[2] = (float)width;
			uniforms.pixelShift[3] = (float)height;
			uniforms.isUpsampling = (width > input->GetWidth()) ? 1 : 0;
			uniforms._pad0 = 0.0f;
			uniforms._pad1 = 0.0f;
			uniforms._pad2 = 0.0f;

			void* data = bilateralUniformBuffer->Map();
			memcpy(data, &uniforms, sizeof(uniforms));
			bilateralUniformBuffer->Unmap();

			// Allocate descriptor set
			VkDescriptorSetAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &bilateralDescLayout;

			VkDescriptorSet descriptorSet;
			if (vkAllocateDescriptorSets(device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
				SPRaise("Failed to allocate bilateral descriptor set");
			}

			// Get depth texture from framebuffer manager
			VulkanImage* depthImage = renderer.GetFramebufferManager()->GetDepthImage().GetPointerOrNull();

			// Update descriptor set
			VkDescriptorBufferInfo bufferInfo = {};
			bufferInfo.buffer = bilateralUniformBuffer->GetBuffer();
			bufferInfo.offset = 0;
			bufferInfo.range = sizeof(BilateralUniforms);

			VkDescriptorImageInfo inputImageInfo = {};
			inputImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			inputImageInfo.imageView = input->GetImageView();
			inputImageInfo.sampler = input->GetSampler();

			VkDescriptorImageInfo depthImageInfo = {};
			depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			depthImageInfo.imageView = depthImage->GetImageView();
			depthImageInfo.sampler = depthImage->GetSampler();

			VkWriteDescriptorSet descriptorWrites[3] = {};
			descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[0].dstSet = descriptorSet;
			descriptorWrites[0].dstBinding = 0;
			descriptorWrites[0].dstArrayElement = 0;
			descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descriptorWrites[0].descriptorCount = 1;
			descriptorWrites[0].pBufferInfo = &bufferInfo;

			descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[1].dstSet = descriptorSet;
			descriptorWrites[1].dstBinding = 1;
			descriptorWrites[1].dstArrayElement = 0;
			descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrites[1].descriptorCount = 1;
			descriptorWrites[1].pImageInfo = &inputImageInfo;

			descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[2].dstSet = descriptorSet;
			descriptorWrites[2].dstBinding = 2;
			descriptorWrites[2].dstArrayElement = 0;
			descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrites[2].descriptorCount = 1;
			descriptorWrites[2].pImageInfo = &depthImageInfo;

			vkUpdateDescriptorSets(device->GetDevice(), 3, descriptorWrites, 0, nullptr);

			// Transition input to shader read
			VkImageMemoryBarrier barrier = {};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = input->GetImage();
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			// Begin render pass
			VkRenderPassBeginInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = bilateralRenderPass;
			renderPassInfo.framebuffer = framebuffer;
			renderPassInfo.renderArea.offset = { 0, 0 };
			renderPassInfo.renderArea.extent = { (uint32_t)width, (uint32_t)height };

			vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, bilateralPipeline);

			VkViewport viewport = {};
			viewport.x = 0.0f;
			viewport.y = 0.0f;
			viewport.width = (float)width;
			viewport.height = (float)height;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

			VkRect2D scissor = {};
			scissor.offset = { 0, 0 };
			scissor.extent = { (uint32_t)width, (uint32_t)height };
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				bilateralPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

			VkBuffer vertexBuffers[] = { quadVertexBuffer->GetBuffer() };
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
			vkCmdBindIndexBuffer(commandBuffer, quadIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);
			vkCmdDrawIndexed(commandBuffer, 6, 1, 0, 0, 0);

			vkCmdEndRenderPass(commandBuffer);

			vkFreeDescriptorSets(device->GetDevice(), descriptorPool, 1, &descriptorSet);
			vkDestroyFramebuffer(device->GetDevice(), framebuffer, nullptr);

			return output;
		}

		void VulkanSSAOFilter::Filter(VkCommandBuffer commandBuffer) {
			SPADES_MARK_FUNCTION();

			if (!(int)r_ssao) {
				return;
			}

			if (ssaoPipeline == VK_NULL_HANDLE) {
				return;
			}

			int width = (int)renderer.ScreenWidth();
			int height = (int)renderer.ScreenHeight();

			bool mirror = renderer.IsRenderingMirror();
			bool useLowQualitySSAO = mirror || (int)r_ssao >= 2;

			// Generate raw SSAO
			Handle<VulkanImage> ssao;
			if (useLowQualitySSAO) {
				ssao = GenerateRawSSAOImage(commandBuffer, (width + 1) / 2, (height + 1) / 2);
			} else {
				ssao = GenerateRawSSAOImage(commandBuffer, width, height);
			}

			// Apply bilateral filter passes
			ssao = ApplyBilateralFilter(commandBuffer, ssao.GetPointerOrNull(), false, width, height);
			ssao = ApplyBilateralFilter(commandBuffer, ssao.GetPointerOrNull(), true, width, height);

			if (!mirror) {
				ssao = ApplyBilateralFilter(commandBuffer, ssao.GetPointerOrNull(), false, width, height);
				ssao = ApplyBilateralFilter(commandBuffer, ssao.GetPointerOrNull(), true, width, height);
			}

			ssaoImage = ssao;
			ssaoWidth = width;
			ssaoHeight = height;
		}
	}
}
