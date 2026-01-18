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
#include "VulkanBuffer.h"
#include "VulkanProgram.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Core/Settings.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

SPADES_SETTING(r_hdrAutoExposureMin);
SPADES_SETTING(r_hdrAutoExposureMax);
SPADES_SETTING(r_hdrAutoExposureSpeed);

namespace spades {
	namespace draw {

		struct ComputeGainUniforms {
			float minGain;
			float maxGain;
			float blendRate;
			float _pad0;
		};

		VulkanAutoExposureFilter::VulkanAutoExposureFilter(VulkanRenderer& r)
			: VulkanPostProcessFilter(r),
			  preprocessProgram(nullptr),
			  preprocessPipeline(VK_NULL_HANDLE),
			  preprocessLayout(VK_NULL_HANDLE),
			  preprocessDescLayout(VK_NULL_HANDLE),
			  downsampleProgram(nullptr),
			  downsamplePipeline(VK_NULL_HANDLE),
			  downsampleLayout(VK_NULL_HANDLE),
			  downsampleDescLayout(VK_NULL_HANDLE),
			  computeGainProgram(nullptr),
			  computeGainPipeline(VK_NULL_HANDLE),
			  computeGainLayout(VK_NULL_HANDLE),
			  computeGainDescLayout(VK_NULL_HANDLE),
			  applyProgram(nullptr),
			  applyPipeline(VK_NULL_HANDLE),
			  applyLayout(VK_NULL_HANDLE),
			  applyDescLayout(VK_NULL_HANDLE),
			  downsampleRenderPass(VK_NULL_HANDLE),
			  exposureRenderPass(VK_NULL_HANDLE),
			  exposureFramebuffer(VK_NULL_HANDLE),
			  descriptorPool(VK_NULL_HANDLE) {
			CreateQuadBuffers();
			CreateDescriptorPool();
			CreateRenderPass();
			CreatePipeline();
			CreateExposureResources();
		}

		VulkanAutoExposureFilter::~VulkanAutoExposureFilter() {
			vkDeviceWaitIdle(device->GetDevice());

			if (exposureFramebuffer != VK_NULL_HANDLE) {
				vkDestroyFramebuffer(device->GetDevice(), exposureFramebuffer, nullptr);
			}

			if (preprocessPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), preprocessPipeline, nullptr);
			}
			if (downsamplePipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), downsamplePipeline, nullptr);
			}
			if (computeGainPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), computeGainPipeline, nullptr);
			}
			if (applyPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), applyPipeline, nullptr);
			}

			if (preprocessLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), preprocessLayout, nullptr);
			}
			if (downsampleLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), downsampleLayout, nullptr);
			}
			if (computeGainLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), computeGainLayout, nullptr);
			}
			if (applyLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), applyLayout, nullptr);
			}

			if (preprocessDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), preprocessDescLayout, nullptr);
			}
			if (downsampleDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), downsampleDescLayout, nullptr);
			}
			if (computeGainDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), computeGainDescLayout, nullptr);
			}
			if (applyDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), applyDescLayout, nullptr);
			}

			if (downsampleRenderPass != VK_NULL_HANDLE) {
				vkDestroyRenderPass(device->GetDevice(), downsampleRenderPass, nullptr);
			}
			if (exposureRenderPass != VK_NULL_HANDLE) {
				vkDestroyRenderPass(device->GetDevice(), exposureRenderPass, nullptr);
			}

			if (descriptorPool != VK_NULL_HANDLE) {
				vkDestroyDescriptorPool(device->GetDevice(), descriptorPool, nullptr);
			}

			DestroyResources();
		}

		void VulkanAutoExposureFilter::CreateQuadBuffers() {
			float vertices[] = {
				0.0f, 0.0f,
				1.0f, 0.0f,
				1.0f, 1.0f,
				0.0f, 1.0f
			};

			uint16_t indices[] = {
				0, 1, 2,
				0, 2, 3
			};

			quadVertexBuffer = new VulkanBuffer(device, sizeof(vertices),
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			void* data;
			vkMapMemory(device->GetDevice(), quadVertexBuffer->GetMemory(), 0, sizeof(vertices), 0, &data);
			memcpy(data, vertices, sizeof(vertices));
			vkUnmapMemory(device->GetDevice(), quadVertexBuffer->GetMemory());

			quadIndexBuffer = new VulkanBuffer(device, sizeof(indices),
				VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			vkMapMemory(device->GetDevice(), quadIndexBuffer->GetMemory(), 0, sizeof(indices), 0, &data);
			memcpy(data, indices, sizeof(indices));
			vkUnmapMemory(device->GetDevice(), quadIndexBuffer->GetMemory());

			computeGainUniformBuffer = new VulkanBuffer(device, sizeof(ComputeGainUniforms),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		}

		void VulkanAutoExposureFilter::CreateDescriptorPool() {
			VkDescriptorPoolSize poolSizes[] = {
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32 },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8 }
			};

			VkDescriptorPoolCreateInfo poolInfo = {};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
			poolInfo.maxSets = 32;
			poolInfo.poolSizeCount = 2;
			poolInfo.pPoolSizes = poolSizes;

			if (vkCreateDescriptorPool(device->GetDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
				SPRaise("Failed to create auto exposure descriptor pool");
			}
		}

		void VulkanAutoExposureFilter::CreateRenderPass() {
			// Downsample render pass
			{
				VkAttachmentDescription colorAttachment = {};
				colorAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
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

				if (vkCreateRenderPass(device->GetDevice(), &renderPassInfo, nullptr, &downsampleRenderPass) != VK_SUCCESS) {
					SPRaise("Failed to create downsample render pass");
				}
			}

			// Exposure render pass (with blending for temporal smoothing)
			{
				VkAttachmentDescription colorAttachment = {};
				colorAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
				colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
				colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
				colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				colorAttachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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

				if (vkCreateRenderPass(device->GetDevice(), &renderPassInfo, nullptr, &exposureRenderPass) != VK_SUCCESS) {
					SPRaise("Failed to create exposure render pass");
				}
			}

			// Main render pass (for apply step)
			{
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
					SPRaise("Failed to create apply render pass");
				}
			}
		}

		void VulkanAutoExposureFilter::CreateExposureResources() {
			exposureImage = new VulkanImage(device, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			exposureImage->CreateSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR,
				VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);

			// Initialize exposure image layout
			VkCommandBufferAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.commandPool = device->GetCommandPool();
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
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
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = exposureImage->GetImage();
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			vkEndCommandBuffer(commandBuffer);

			VkSubmitInfo submitInfo = {};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffer;

			vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
			vkQueueWaitIdle(device->GetGraphicsQueue());

			vkFreeCommandBuffers(device->GetDevice(), device->GetCommandPool(), 1, &commandBuffer);

			VkImageView attachments[] = { exposureImage->GetImageView() };
			VkFramebufferCreateInfo framebufferInfo = {};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = exposureRenderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = attachments;
			framebufferInfo.width = 1;
			framebufferInfo.height = 1;
			framebufferInfo.layers = 1;

			if (vkCreateFramebuffer(device->GetDevice(), &framebufferInfo, nullptr, &exposureFramebuffer) != VK_SUCCESS) {
				SPRaise("Failed to create exposure framebuffer");
			}
		}

		void VulkanAutoExposureFilter::CreatePipeline() {
			// Load shader programs
			preprocessProgram = renderer.RegisterProgram("Shaders/PostFilters/AutoExposurePreprocess.vk.program");
			downsampleProgram = renderer.RegisterProgram("Shaders/PostFilters/Downsample.vk.program");
			computeGainProgram = renderer.RegisterProgram("Shaders/PostFilters/AutoExposure.vk.program");
			applyProgram = renderer.RegisterProgram("Shaders/PostFilters/AutoExposureApply.vk.program");

			// Common vertex input state
			VkVertexInputBindingDescription bindingDescription = {};
			bindingDescription.binding = 0;
			bindingDescription.stride = sizeof(float) * 2;
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			VkVertexInputAttributeDescription attributeDescription = {};
			attributeDescription.binding = 0;
			attributeDescription.location = 0;
			attributeDescription.format = VK_FORMAT_R32G32_SFLOAT;
			attributeDescription.offset = 0;

			VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
			vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputInfo.vertexBindingDescriptionCount = 1;
			vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
			vertexInputInfo.vertexAttributeDescriptionCount = 1;
			vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

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

			VkPipelineDepthStencilStateCreateInfo depthStencil = {};
			depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencil.depthTestEnable = VK_FALSE;
			depthStencil.depthWriteEnable = VK_FALSE;

			VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dynamicState = {};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = 2;
			dynamicState.pDynamicStates = dynamicStates;

			// Preprocess pipeline
			{
				preprocessDescLayout = preprocessProgram->GetDescriptorSetLayout();

				VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
				pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				pipelineLayoutInfo.setLayoutCount = 1;
				pipelineLayoutInfo.pSetLayouts = &preprocessDescLayout;

				if (vkCreatePipelineLayout(device->GetDevice(), &pipelineLayoutInfo, nullptr, &preprocessLayout) != VK_SUCCESS) {
					SPRaise("Failed to create preprocess pipeline layout");
				}

				VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
				colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
					VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
				colorBlendAttachment.blendEnable = VK_FALSE;

				VkPipelineColorBlendStateCreateInfo colorBlending = {};
				colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
				colorBlending.logicOpEnable = VK_FALSE;
				colorBlending.attachmentCount = 1;
				colorBlending.pAttachments = &colorBlendAttachment;

				auto shaderStages = preprocessProgram->GetShaderStages();

				VkGraphicsPipelineCreateInfo pipelineInfo = {};
				pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
				pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
				pipelineInfo.pStages = shaderStages.data();
				pipelineInfo.pVertexInputState = &vertexInputInfo;
				pipelineInfo.pInputAssemblyState = &inputAssembly;
				pipelineInfo.pViewportState = &viewportState;
				pipelineInfo.pRasterizationState = &rasterizer;
				pipelineInfo.pMultisampleState = &multisampling;
				pipelineInfo.pDepthStencilState = &depthStencil;
				pipelineInfo.pColorBlendState = &colorBlending;
				pipelineInfo.pDynamicState = &dynamicState;
				pipelineInfo.layout = preprocessLayout;
				pipelineInfo.renderPass = downsampleRenderPass;
				pipelineInfo.subpass = 0;

				if (vkCreateGraphicsPipelines(device->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &preprocessPipeline) != VK_SUCCESS) {
					SPRaise("Failed to create preprocess pipeline");
				}
			}

			// Downsample pipeline
			{
				downsampleDescLayout = downsampleProgram->GetDescriptorSetLayout();

				VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
				pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				pipelineLayoutInfo.setLayoutCount = 1;
				pipelineLayoutInfo.pSetLayouts = &downsampleDescLayout;

				if (vkCreatePipelineLayout(device->GetDevice(), &pipelineLayoutInfo, nullptr, &downsampleLayout) != VK_SUCCESS) {
					SPRaise("Failed to create downsample pipeline layout");
				}

				VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
				colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
					VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
				colorBlendAttachment.blendEnable = VK_FALSE;

				VkPipelineColorBlendStateCreateInfo colorBlending = {};
				colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
				colorBlending.logicOpEnable = VK_FALSE;
				colorBlending.attachmentCount = 1;
				colorBlending.pAttachments = &colorBlendAttachment;

				auto shaderStages = downsampleProgram->GetShaderStages();

				VkGraphicsPipelineCreateInfo pipelineInfo = {};
				pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
				pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
				pipelineInfo.pStages = shaderStages.data();
				pipelineInfo.pVertexInputState = &vertexInputInfo;
				pipelineInfo.pInputAssemblyState = &inputAssembly;
				pipelineInfo.pViewportState = &viewportState;
				pipelineInfo.pRasterizationState = &rasterizer;
				pipelineInfo.pMultisampleState = &multisampling;
				pipelineInfo.pDepthStencilState = &depthStencil;
				pipelineInfo.pColorBlendState = &colorBlending;
				pipelineInfo.pDynamicState = &dynamicState;
				pipelineInfo.layout = downsampleLayout;
				pipelineInfo.renderPass = downsampleRenderPass;
				pipelineInfo.subpass = 0;

				if (vkCreateGraphicsPipelines(device->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &downsamplePipeline) != VK_SUCCESS) {
					SPRaise("Failed to create downsample pipeline");
				}
			}

			// Compute gain pipeline (with blending)
			{
				computeGainDescLayout = computeGainProgram->GetDescriptorSetLayout();

				VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
				pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				pipelineLayoutInfo.setLayoutCount = 1;
				pipelineLayoutInfo.pSetLayouts = &computeGainDescLayout;

				if (vkCreatePipelineLayout(device->GetDevice(), &pipelineLayoutInfo, nullptr, &computeGainLayout) != VK_SUCCESS) {
					SPRaise("Failed to create compute gain pipeline layout");
				}

				VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
				colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
					VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
				colorBlendAttachment.blendEnable = VK_TRUE;
				colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
				colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
				colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

				VkPipelineColorBlendStateCreateInfo colorBlending = {};
				colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
				colorBlending.logicOpEnable = VK_FALSE;
				colorBlending.attachmentCount = 1;
				colorBlending.pAttachments = &colorBlendAttachment;

				auto shaderStages = computeGainProgram->GetShaderStages();

				VkGraphicsPipelineCreateInfo pipelineInfo = {};
				pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
				pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
				pipelineInfo.pStages = shaderStages.data();
				pipelineInfo.pVertexInputState = &vertexInputInfo;
				pipelineInfo.pInputAssemblyState = &inputAssembly;
				pipelineInfo.pViewportState = &viewportState;
				pipelineInfo.pRasterizationState = &rasterizer;
				pipelineInfo.pMultisampleState = &multisampling;
				pipelineInfo.pDepthStencilState = &depthStencil;
				pipelineInfo.pColorBlendState = &colorBlending;
				pipelineInfo.pDynamicState = &dynamicState;
				pipelineInfo.layout = computeGainLayout;
				pipelineInfo.renderPass = exposureRenderPass;
				pipelineInfo.subpass = 0;

				if (vkCreateGraphicsPipelines(device->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computeGainPipeline) != VK_SUCCESS) {
					SPRaise("Failed to create compute gain pipeline");
				}
			}

			// Apply pipeline
			{
				applyDescLayout = applyProgram->GetDescriptorSetLayout();

				VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
				pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				pipelineLayoutInfo.setLayoutCount = 1;
				pipelineLayoutInfo.pSetLayouts = &applyDescLayout;

				if (vkCreatePipelineLayout(device->GetDevice(), &pipelineLayoutInfo, nullptr, &applyLayout) != VK_SUCCESS) {
					SPRaise("Failed to create apply pipeline layout");
				}

				VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
				colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
					VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
				colorBlendAttachment.blendEnable = VK_FALSE;

				VkPipelineColorBlendStateCreateInfo colorBlending = {};
				colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
				colorBlending.logicOpEnable = VK_FALSE;
				colorBlending.attachmentCount = 1;
				colorBlending.pAttachments = &colorBlendAttachment;

				auto shaderStages = applyProgram->GetShaderStages();

				VkGraphicsPipelineCreateInfo pipelineInfo = {};
				pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
				pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
				pipelineInfo.pStages = shaderStages.data();
				pipelineInfo.pVertexInputState = &vertexInputInfo;
				pipelineInfo.pInputAssemblyState = &inputAssembly;
				pipelineInfo.pViewportState = &viewportState;
				pipelineInfo.pRasterizationState = &rasterizer;
				pipelineInfo.pMultisampleState = &multisampling;
				pipelineInfo.pDepthStencilState = &depthStencil;
				pipelineInfo.pColorBlendState = &colorBlending;
				pipelineInfo.pDynamicState = &dynamicState;
				pipelineInfo.layout = applyLayout;
				pipelineInfo.renderPass = renderPass;
				pipelineInfo.subpass = 0;

				if (vkCreateGraphicsPipelines(device->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &applyPipeline) != VK_SUCCESS) {
					SPRaise("Failed to create apply pipeline");
				}
			}

			SPLog("VulkanAutoExposureFilter pipelines created");
		}

		Handle<VulkanImage> VulkanAutoExposureFilter::DownsampleToLuminance(VkCommandBuffer commandBuffer,
			VulkanImage* input, int width, int height) {

			std::vector<Handle<VulkanImage>> levels;
			std::vector<VkFramebuffer> framebuffers;
			VulkanImage* currentInput = input;
			int currentWidth = width;
			int currentHeight = height;
			bool firstLevel = true;

			while (currentWidth > 1 || currentHeight > 1) {
				int newWidth = (currentWidth + 1) / 2;
				int newHeight = (currentHeight + 1) / 2;

				Handle<VulkanImage> newLevel = new VulkanImage(device, newWidth, newHeight,
					VK_FORMAT_R16G16B16A16_SFLOAT,
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				newLevel->CreateSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR,
					VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);

				VkImageView attachments[] = { newLevel->GetImageView() };
				VkFramebufferCreateInfo framebufferInfo = {};
				framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				framebufferInfo.renderPass = downsampleRenderPass;
				framebufferInfo.attachmentCount = 1;
				framebufferInfo.pAttachments = attachments;
				framebufferInfo.width = newWidth;
				framebufferInfo.height = newHeight;
				framebufferInfo.layers = 1;

				VkFramebuffer framebuffer;
				if (vkCreateFramebuffer(device->GetDevice(), &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
					SPRaise("Failed to create downsample framebuffer");
				}
				framebuffers.push_back(framebuffer);

				// Transition input if not first level
				if (!firstLevel) {
					VkImageMemoryBarrier barrier = {};
					barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					barrier.image = currentInput->GetImage();
					barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					barrier.subresourceRange.baseMipLevel = 0;
					barrier.subresourceRange.levelCount = 1;
					barrier.subresourceRange.baseArrayLayer = 0;
					barrier.subresourceRange.layerCount = 1;
					barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
					barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

					vkCmdPipelineBarrier(commandBuffer,
						VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						0, 0, nullptr, 0, nullptr, 1, &barrier);
				}

				VkDescriptorSetAllocateInfo allocInfo = {};
				allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				allocInfo.descriptorPool = descriptorPool;
				allocInfo.descriptorSetCount = 1;
				allocInfo.pSetLayouts = firstLevel ? &preprocessDescLayout : &downsampleDescLayout;

				VkDescriptorSet descriptorSet;
				if (vkAllocateDescriptorSets(device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
					SPRaise("Failed to allocate downsample descriptor set");
				}

				VkDescriptorImageInfo imageInfo = {};
				imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				imageInfo.imageView = currentInput->GetImageView();
				imageInfo.sampler = currentInput->GetSampler();

				VkWriteDescriptorSet descriptorWrite = {};
				descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				descriptorWrite.dstSet = descriptorSet;
				descriptorWrite.dstBinding = 0;
				descriptorWrite.dstArrayElement = 0;
				descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				descriptorWrite.descriptorCount = 1;
				descriptorWrite.pImageInfo = &imageInfo;

				vkUpdateDescriptorSets(device->GetDevice(), 1, &descriptorWrite, 0, nullptr);

				VkRenderPassBeginInfo renderPassInfo = {};
				renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				renderPassInfo.renderPass = downsampleRenderPass;
				renderPassInfo.framebuffer = framebuffer;
				renderPassInfo.renderArea.offset = {0, 0};
				renderPassInfo.renderArea.extent = {(uint32_t)newWidth, (uint32_t)newHeight};

				vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport viewport = {};
				viewport.x = 0.0f;
				viewport.y = 0.0f;
				viewport.width = (float)newWidth;
				viewport.height = (float)newHeight;
				viewport.minDepth = 0.0f;
				viewport.maxDepth = 1.0f;
				vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

				VkRect2D scissor = {};
				scissor.offset = {0, 0};
				scissor.extent = {(uint32_t)newWidth, (uint32_t)newHeight};
				vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
					firstLevel ? preprocessPipeline : downsamplePipeline);
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
					firstLevel ? preprocessLayout : downsampleLayout, 0, 1, &descriptorSet, 0, nullptr);

				VkBuffer vertexBuffers[] = { quadVertexBuffer->GetBuffer() };
				VkDeviceSize offsets[] = { 0 };
				vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
				vkCmdBindIndexBuffer(commandBuffer, quadIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);
				vkCmdDrawIndexed(commandBuffer, 6, 1, 0, 0, 0);

				vkCmdEndRenderPass(commandBuffer);

				vkFreeDescriptorSets(device->GetDevice(), descriptorPool, 1, &descriptorSet);

				levels.push_back(newLevel);
				currentInput = newLevel.GetPointerOrNull();
				currentWidth = newWidth;
				currentHeight = newHeight;
				firstLevel = false;
			}

			// Wait for GPU and destroy temporary framebuffers
			vkQueueWaitIdle(device->GetGraphicsQueue());
			for (VkFramebuffer fb : framebuffers) {
				vkDestroyFramebuffer(device->GetDevice(), fb, nullptr);
			}

			return levels.empty() ? nullptr : levels.back();
		}

		void VulkanAutoExposureFilter::ComputeGain(VkCommandBuffer commandBuffer,
			VulkanImage* luminanceImage, float dt) {

			float minExposure = (float)r_hdrAutoExposureMin;
			float maxExposure = (float)r_hdrAutoExposureMax;

			minExposure = std::min(std::max(minExposure, -10.0f), 10.0f);
			maxExposure = std::min(std::max(maxExposure, minExposure), 10.0f);

			float speed = (float)r_hdrAutoExposureSpeed;
			if (speed < 0.0f) speed = 0.0f;
			float rate = 1.0f - std::pow(0.01f, dt * speed);

			ComputeGainUniforms uniforms;
			uniforms.minGain = std::pow(2.0f, minExposure);
			uniforms.maxGain = std::pow(2.0f, maxExposure);
			uniforms.blendRate = rate;
			uniforms._pad0 = 0.0f;

			void* data;
			vkMapMemory(device->GetDevice(), computeGainUniformBuffer->GetMemory(), 0, sizeof(uniforms), 0, &data);
			memcpy(data, &uniforms, sizeof(uniforms));
			vkUnmapMemory(device->GetDevice(), computeGainUniformBuffer->GetMemory());

			VkDescriptorSetAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &computeGainDescLayout;

			VkDescriptorSet descriptorSet;
			if (vkAllocateDescriptorSets(device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
				SPRaise("Failed to allocate compute gain descriptor set");
			}

			VkDescriptorBufferInfo bufferInfo = {};
			bufferInfo.buffer = computeGainUniformBuffer->GetBuffer();
			bufferInfo.offset = 0;
			bufferInfo.range = sizeof(ComputeGainUniforms);

			VkDescriptorImageInfo imageInfo = {};
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = luminanceImage->GetImageView();
			imageInfo.sampler = luminanceImage->GetSampler();

			VkWriteDescriptorSet descriptorWrites[2] = {};
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
			descriptorWrites[1].pImageInfo = &imageInfo;

			vkUpdateDescriptorSets(device->GetDevice(), 2, descriptorWrites, 0, nullptr);

			VkRenderPassBeginInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = exposureRenderPass;
			renderPassInfo.framebuffer = exposureFramebuffer;
			renderPassInfo.renderArea.offset = {0, 0};
			renderPassInfo.renderArea.extent = {1, 1};

			vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = {};
			viewport.x = 0.0f;
			viewport.y = 0.0f;
			viewport.width = 1.0f;
			viewport.height = 1.0f;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

			VkRect2D scissor = {};
			scissor.offset = {0, 0};
			scissor.extent = {1, 1};
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, computeGainPipeline);
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				computeGainLayout, 0, 1, &descriptorSet, 0, nullptr);

			VkBuffer vertexBuffers[] = { quadVertexBuffer->GetBuffer() };
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
			vkCmdBindIndexBuffer(commandBuffer, quadIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);
			vkCmdDrawIndexed(commandBuffer, 6, 1, 0, 0, 0);

			vkCmdEndRenderPass(commandBuffer);

			vkFreeDescriptorSets(device->GetDevice(), descriptorPool, 1, &descriptorSet);
		}

		void VulkanAutoExposureFilter::Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output) {
			Filter(commandBuffer, input, output, 1.0f / 60.0f);
		}

		void VulkanAutoExposureFilter::Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output, float dt) {
			SPADES_MARK_FUNCTION();

			int width = (int)renderer.ScreenWidth();
			int height = (int)renderer.ScreenHeight();

			// Step 1: Downsample input to 1x1 luminance
			Handle<VulkanImage> luminanceImage = DownsampleToLuminance(commandBuffer, input, width, height);

			if (!luminanceImage) {
				return;
			}

			// Step 2: Compute gain and update exposure texture
			ComputeGain(commandBuffer, luminanceImage.GetPointerOrNull(), dt);

			// Step 3: Apply exposure to output
			VkImageView attachments[] = { output->GetImageView() };
			VkFramebufferCreateInfo framebufferInfo = {};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = renderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = attachments;
			framebufferInfo.width = width;
			framebufferInfo.height = height;
			framebufferInfo.layers = 1;

			VkFramebuffer outputFramebuffer;
			if (vkCreateFramebuffer(device->GetDevice(), &framebufferInfo, nullptr, &outputFramebuffer) != VK_SUCCESS) {
				SPRaise("Failed to create apply framebuffer");
			}

			VkDescriptorSetAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &applyDescLayout;

			VkDescriptorSet descriptorSet;
			if (vkAllocateDescriptorSets(device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
				SPRaise("Failed to allocate apply descriptor set");
			}

			VkDescriptorImageInfo inputImageInfo = {};
			inputImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			inputImageInfo.imageView = input->GetImageView();
			inputImageInfo.sampler = input->GetSampler();

			VkDescriptorImageInfo exposureImageInfo = {};
			exposureImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			exposureImageInfo.imageView = exposureImage->GetImageView();
			exposureImageInfo.sampler = exposureImage->GetSampler();

			VkWriteDescriptorSet descriptorWrites[2] = {};
			descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[0].dstSet = descriptorSet;
			descriptorWrites[0].dstBinding = 0;
			descriptorWrites[0].dstArrayElement = 0;
			descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrites[0].descriptorCount = 1;
			descriptorWrites[0].pImageInfo = &inputImageInfo;

			descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[1].dstSet = descriptorSet;
			descriptorWrites[1].dstBinding = 1;
			descriptorWrites[1].dstArrayElement = 0;
			descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrites[1].descriptorCount = 1;
			descriptorWrites[1].pImageInfo = &exposureImageInfo;

			vkUpdateDescriptorSets(device->GetDevice(), 2, descriptorWrites, 0, nullptr);

			VkRenderPassBeginInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = renderPass;
			renderPassInfo.framebuffer = outputFramebuffer;
			renderPassInfo.renderArea.offset = {0, 0};
			renderPassInfo.renderArea.extent = {(uint32_t)width, (uint32_t)height};

			vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = {};
			viewport.x = 0.0f;
			viewport.y = 0.0f;
			viewport.width = (float)width;
			viewport.height = (float)height;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

			VkRect2D scissor = {};
			scissor.offset = {0, 0};
			scissor.extent = {(uint32_t)width, (uint32_t)height};
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, applyPipeline);
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				applyLayout, 0, 1, &descriptorSet, 0, nullptr);

			VkBuffer vertexBuffers[] = { quadVertexBuffer->GetBuffer() };
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
			vkCmdBindIndexBuffer(commandBuffer, quadIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);
			vkCmdDrawIndexed(commandBuffer, 6, 1, 0, 0, 0);

			vkCmdEndRenderPass(commandBuffer);

			// Wait for GPU and destroy temporary framebuffer
			vkQueueWaitIdle(device->GetGraphicsQueue());
			vkDestroyFramebuffer(device->GetDevice(), outputFramebuffer, nullptr);

			vkFreeDescriptorSets(device->GetDevice(), descriptorPool, 1, &descriptorSet);
		}
	}
}
