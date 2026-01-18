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
#include "VulkanBuffer.h"
#include "VulkanProgram.h"
#include "VulkanFramebufferManager.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Core/Math.h>
#include <cstring>

namespace spades {
	namespace draw {

		struct DownsampleUniforms {
			float colorUniform[4];
			float texCoordRange[4];
		};

		struct CompositeUniforms {
			float mix1[3];
			float _pad0;
			float mix2[3];
			float _pad1;
		};

		VulkanBloomFilter::VulkanBloomFilter(VulkanRenderer& r)
			: VulkanPostProcessFilter(r),
			  downsamplePipeline(VK_NULL_HANDLE),
			  downsampleLayout(VK_NULL_HANDLE),
			  compositePipeline(VK_NULL_HANDLE),
			  compositeLayout(VK_NULL_HANDLE),
			  finalCompositePipeline(VK_NULL_HANDLE),
			  finalCompositeLayout(VK_NULL_HANDLE),
			  downsampleDescLayout(VK_NULL_HANDLE),
			  compositeDescLayout(VK_NULL_HANDLE),
			  finalCompositeDescLayout(VK_NULL_HANDLE),
			  descriptorPool(VK_NULL_HANDLE),
			  downsampleRenderPass(VK_NULL_HANDLE),
			  compositeRenderPass(VK_NULL_HANDLE) {
			CreateQuadBuffers();
			CreateDescriptorPool();
			CreateDownsampleRenderPass();
			CreateCompositeRenderPass();
			CreateRenderPass();
			CreatePipeline();
		}

		VulkanBloomFilter::~VulkanBloomFilter() {
			vkDeviceWaitIdle(device->GetDevice());

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
			if (descriptorPool != VK_NULL_HANDLE) {
				vkDestroyDescriptorPool(device->GetDevice(), descriptorPool, nullptr);
			}
			if (downsampleRenderPass != VK_NULL_HANDLE) {
				vkDestroyRenderPass(device->GetDevice(), downsampleRenderPass, nullptr);
			}
			if (compositeRenderPass != VK_NULL_HANDLE) {
				vkDestroyRenderPass(device->GetDevice(), compositeRenderPass, nullptr);
			}

			DestroyResources();
		}

		void VulkanBloomFilter::CreateQuadBuffers() {
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

			downsampleUniformBuffer = new VulkanBuffer(device, sizeof(DownsampleUniforms),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			compositeUniformBuffer = new VulkanBuffer(device, sizeof(CompositeUniforms),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		}

		void VulkanBloomFilter::CreateDescriptorPool() {
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
				SPRaise("Failed to create bloom descriptor pool");
			}
		}

		void VulkanBloomFilter::CreateDownsampleRenderPass() {
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

			VkSubpassDependency dependency = {};
			dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass = 0;
			dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = 1;
			renderPassInfo.pAttachments = &colorAttachment;
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = 1;
			renderPassInfo.pDependencies = &dependency;

			if (vkCreateRenderPass(device->GetDevice(), &renderPassInfo, nullptr, &downsampleRenderPass) != VK_SUCCESS) {
				SPRaise("Failed to create bloom downsample render pass");
			}
		}

		void VulkanBloomFilter::CreateCompositeRenderPass() {
			VkAttachmentDescription colorAttachment = {};
			colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
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

			VkSubpassDependency dependency = {};
			dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass = 0;
			dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = 1;
			renderPassInfo.pAttachments = &colorAttachment;
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = 1;
			renderPassInfo.pDependencies = &dependency;

			if (vkCreateRenderPass(device->GetDevice(), &renderPassInfo, nullptr, &compositeRenderPass) != VK_SUCCESS) {
				SPRaise("Failed to create bloom composite render pass");
			}
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

			VkSubpassDependency dependency = {};
			dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass = 0;
			dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = 1;
			renderPassInfo.pAttachments = &colorAttachment;
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = 1;
			renderPassInfo.pDependencies = &dependency;

			if (vkCreateRenderPass(device->GetDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
				SPRaise("Failed to create bloom final render pass");
			}
		}

		void VulkanBloomFilter::CreatePipeline() {
			// Load programs
			downsampleProgram = renderer.RegisterProgram("Shaders/PostFilters/BloomDownsample.vk.program");
			compositeProgram = renderer.RegisterProgram("Shaders/PostFilters/BloomComposite.vk.program");

			// --- Downsample pipeline ---
			downsampleDescLayout = downsampleProgram->GetDescriptorSetLayout();

			VkPipelineLayoutCreateInfo downsampleLayoutInfo = {};
			downsampleLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			downsampleLayoutInfo.setLayoutCount = 1;
			downsampleLayoutInfo.pSetLayouts = &downsampleDescLayout;

			if (vkCreatePipelineLayout(device->GetDevice(), &downsampleLayoutInfo, nullptr, &downsampleLayout) != VK_SUCCESS) {
				SPRaise("Failed to create downsample pipeline layout");
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

			auto downsampleStages = downsampleProgram->GetShaderStages();

			VkGraphicsPipelineCreateInfo pipelineInfo = {};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = static_cast<uint32_t>(downsampleStages.size());
			pipelineInfo.pStages = downsampleStages.data();
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

			// --- Composite pipeline (with alpha blending) ---
			VkPipelineColorBlendAttachmentState blendAttachment = {};
			blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
				VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			blendAttachment.blendEnable = VK_TRUE;
			blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
			blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

			VkPipelineColorBlendStateCreateInfo blendColorBlending = {};
			blendColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			blendColorBlending.logicOpEnable = VK_FALSE;
			blendColorBlending.attachmentCount = 1;
			blendColorBlending.pAttachments = &blendAttachment;

			// Use downsample layout for composite pass (same shader)
			compositeDescLayout = downsampleDescLayout;
			compositeLayout = downsampleLayout;

			pipelineInfo.pColorBlendState = &blendColorBlending;
			pipelineInfo.renderPass = compositeRenderPass;
			pipelineInfo.layout = compositeLayout;

			if (vkCreateGraphicsPipelines(device->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &compositePipeline) != VK_SUCCESS) {
				SPRaise("Failed to create composite pipeline");
			}

			// --- Final composite pipeline (gamma mix) ---
			finalCompositeDescLayout = compositeProgram->GetDescriptorSetLayout();

			VkPipelineLayoutCreateInfo finalLayoutInfo = {};
			finalLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			finalLayoutInfo.setLayoutCount = 1;
			finalLayoutInfo.pSetLayouts = &finalCompositeDescLayout;

			if (vkCreatePipelineLayout(device->GetDevice(), &finalLayoutInfo, nullptr, &finalCompositeLayout) != VK_SUCCESS) {
				SPRaise("Failed to create final composite pipeline layout");
			}

			auto compositeStages = compositeProgram->GetShaderStages();

			pipelineInfo.stageCount = static_cast<uint32_t>(compositeStages.size());
			pipelineInfo.pStages = compositeStages.data();
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.renderPass = renderPass;
			pipelineInfo.layout = finalCompositeLayout;

			if (vkCreateGraphicsPipelines(device->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &finalCompositePipeline) != VK_SUCCESS) {
				SPRaise("Failed to create final composite pipeline");
			}

			// Set layouts to null so destructor doesn't double-free
			compositeDescLayout = VK_NULL_HANDLE;
			compositeLayout = VK_NULL_HANDLE;
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

				// Create framebuffer for downsample pass
				VkImageView attachments[] = { level.image->GetImageView() };
				VkFramebufferCreateInfo framebufferInfo = {};
				framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				framebufferInfo.renderPass = downsampleRenderPass;
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
			vkDeviceWaitIdle(device->GetDevice());
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
			int inputWidth = input->GetWidth();
			int inputHeight = input->GetHeight();

			if (levels.empty() || levels[0].width != inputWidth || levels[0].height != inputHeight) {
				CreateLevels(inputWidth, inputHeight);
			}

			VkBuffer vertexBuffers[] = { quadVertexBuffer->GetBuffer() };
			VkDeviceSize offsets[] = { 0 };

			// --- Step 1: Create downsample levels ---
			for (int i = 0; i < 6; i++) {
				VulkanImage* prevLevel;
				int prevW, prevH;

				if (i == 0) {
					prevLevel = input;
					prevW = inputWidth;
					prevH = inputHeight;
				} else {
					prevLevel = levels[i - 1].image.GetPointerOrNull();
					prevW = levels[i - 1].width;
					prevH = levels[i - 1].height;
				}

				BloomLevel& newLevel = levels[i];
				int newW = newLevel.width;
				int newH = newLevel.height;

				// Transition previous level to shader read
				if (i > 0) {
					VkImageMemoryBarrier barrier = {};
					barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					barrier.image = prevLevel->GetImage();
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
				}

				// Update uniforms
				DownsampleUniforms uniforms;
				uniforms.colorUniform[0] = 1.0f;
				uniforms.colorUniform[1] = 1.0f;
				uniforms.colorUniform[2] = 1.0f;
				uniforms.colorUniform[3] = 1.0f;
				uniforms.texCoordRange[0] = 0.0f;
				uniforms.texCoordRange[1] = 0.0f;
				uniforms.texCoordRange[2] = (float)newW * 2.0f / (float)prevW;
				uniforms.texCoordRange[3] = (float)newH * 2.0f / (float)prevH;

				void* data = downsampleUniformBuffer->Map();
				memcpy(data, &uniforms, sizeof(uniforms));
				downsampleUniformBuffer->Unmap();

				// Allocate descriptor set
				VkDescriptorSetAllocateInfo allocInfo = {};
				allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				allocInfo.descriptorPool = descriptorPool;
				allocInfo.descriptorSetCount = 1;
				allocInfo.pSetLayouts = &downsampleDescLayout;

				VkDescriptorSet descriptorSet;
				if (vkAllocateDescriptorSets(device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
					SPRaise("Failed to allocate downsample descriptor set");
				}

				// Update descriptor set
				VkDescriptorBufferInfo bufferInfo = {};
				bufferInfo.buffer = downsampleUniformBuffer->GetBuffer();
				bufferInfo.offset = 0;
				bufferInfo.range = sizeof(DownsampleUniforms);

				VkDescriptorImageInfo imageInfo = {};
				imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				imageInfo.imageView = prevLevel->GetImageView();
				imageInfo.sampler = prevLevel->GetSampler();

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

				// Begin render pass
				VkRenderPassBeginInfo renderPassInfo = {};
				renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				renderPassInfo.renderPass = downsampleRenderPass;
				renderPassInfo.framebuffer = newLevel.framebuffer;
				renderPassInfo.renderArea.offset = { 0, 0 };
				renderPassInfo.renderArea.extent = { (uint32_t)newW, (uint32_t)newH };

				vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, downsamplePipeline);

				VkViewport viewport = {};
				viewport.x = 0.0f;
				viewport.y = 0.0f;
				viewport.width = (float)newW;
				viewport.height = (float)newH;
				viewport.minDepth = 0.0f;
				viewport.maxDepth = 1.0f;
				vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

				VkRect2D scissor = {};
				scissor.offset = { 0, 0 };
				scissor.extent = { (uint32_t)newW, (uint32_t)newH };
				vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
					downsampleLayout, 0, 1, &descriptorSet, 0, nullptr);

				vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
				vkCmdBindIndexBuffer(commandBuffer, quadIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);
				vkCmdDrawIndexed(commandBuffer, 6, 1, 0, 0, 0);

				vkCmdEndRenderPass(commandBuffer);

				vkFreeDescriptorSets(device->GetDevice(), descriptorPool, 1, &descriptorSet);
			}

			// --- Step 2: Composite levels in reverse order ---
			for (int i = (int)levels.size() - 1; i >= 1; i--) {
				int cnt = (int)levels.size() - i;
				float alpha = (float)cnt / (float)(cnt + 1);
				alpha = sqrtf(alpha);

				BloomLevel& curLevel = levels[i];
				BloomLevel& targLevel = levels[i - 1];

				// Transition current level to shader read
				VkImageMemoryBarrier barrier = {};
				barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.image = curLevel.image->GetImage();
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

				// Need a different framebuffer for composite (render pass that loads)
				VkFramebuffer compositeFramebuffer;
				VkImageView attachments[] = { targLevel.image->GetImageView() };
				VkFramebufferCreateInfo framebufferInfo = {};
				framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				framebufferInfo.renderPass = compositeRenderPass;
				framebufferInfo.attachmentCount = 1;
				framebufferInfo.pAttachments = attachments;
				framebufferInfo.width = targLevel.width;
				framebufferInfo.height = targLevel.height;
				framebufferInfo.layers = 1;

				if (vkCreateFramebuffer(device->GetDevice(), &framebufferInfo, nullptr, &compositeFramebuffer) != VK_SUCCESS) {
					SPRaise("Failed to create composite framebuffer");
				}

				// Update uniforms (color with alpha)
				DownsampleUniforms uniforms;
				uniforms.colorUniform[0] = 1.0f;
				uniforms.colorUniform[1] = 1.0f;
				uniforms.colorUniform[2] = 1.0f;
				uniforms.colorUniform[3] = alpha;
				uniforms.texCoordRange[0] = 0.0f;
				uniforms.texCoordRange[1] = 0.0f;
				uniforms.texCoordRange[2] = 1.0f;
				uniforms.texCoordRange[3] = 1.0f;

				void* data = downsampleUniformBuffer->Map();
				memcpy(data, &uniforms, sizeof(uniforms));
				downsampleUniformBuffer->Unmap();

				// Allocate descriptor set
				VkDescriptorSetAllocateInfo allocInfo = {};
				allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				allocInfo.descriptorPool = descriptorPool;
				allocInfo.descriptorSetCount = 1;
				allocInfo.pSetLayouts = &downsampleDescLayout;

				VkDescriptorSet descriptorSet;
				if (vkAllocateDescriptorSets(device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
					SPRaise("Failed to allocate composite descriptor set");
				}

				// Update descriptor set
				VkDescriptorBufferInfo bufferInfo = {};
				bufferInfo.buffer = downsampleUniformBuffer->GetBuffer();
				bufferInfo.offset = 0;
				bufferInfo.range = sizeof(DownsampleUniforms);

				VkDescriptorImageInfo imageInfo = {};
				imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				imageInfo.imageView = curLevel.image->GetImageView();
				imageInfo.sampler = curLevel.image->GetSampler();

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

				// Begin render pass
				VkRenderPassBeginInfo renderPassInfo = {};
				renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				renderPassInfo.renderPass = compositeRenderPass;
				renderPassInfo.framebuffer = compositeFramebuffer;
				renderPassInfo.renderArea.offset = { 0, 0 };
				renderPassInfo.renderArea.extent = { (uint32_t)targLevel.width, (uint32_t)targLevel.height };

				vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipeline);

				VkViewport viewport = {};
				viewport.x = 0.0f;
				viewport.y = 0.0f;
				viewport.width = (float)targLevel.width;
				viewport.height = (float)targLevel.height;
				viewport.minDepth = 0.0f;
				viewport.maxDepth = 1.0f;
				vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

				VkRect2D scissor = {};
				scissor.offset = { 0, 0 };
				scissor.extent = { (uint32_t)targLevel.width, (uint32_t)targLevel.height };
				vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
					downsampleLayout, 0, 1, &descriptorSet, 0, nullptr);

				vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
				vkCmdBindIndexBuffer(commandBuffer, quadIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);
				vkCmdDrawIndexed(commandBuffer, 6, 1, 0, 0, 0);

				vkCmdEndRenderPass(commandBuffer);

				vkFreeDescriptorSets(device->GetDevice(), descriptorPool, 1, &descriptorSet);
				vkDestroyFramebuffer(device->GetDevice(), compositeFramebuffer, nullptr);
			}

			// --- Step 3: Final composite (gamma mix original + bloom) ---

			// Transition top bloom level to shader read
			VkImageMemoryBarrier barrier = {};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = levels[0].image->GetImage();
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

			// Create output framebuffer
			VkFramebuffer outputFramebuffer;
			VkImageView outputAttachments[] = { output->GetImageView() };
			VkFramebufferCreateInfo outputFbInfo = {};
			outputFbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			outputFbInfo.renderPass = renderPass;
			outputFbInfo.attachmentCount = 1;
			outputFbInfo.pAttachments = outputAttachments;
			outputFbInfo.width = output->GetWidth();
			outputFbInfo.height = output->GetHeight();
			outputFbInfo.layers = 1;

			if (vkCreateFramebuffer(device->GetDevice(), &outputFbInfo, nullptr, &outputFramebuffer) != VK_SUCCESS) {
				SPRaise("Failed to create output framebuffer");
			}

			// Update composite uniforms (gamma mix: 0.8 original, 0.2 bloom)
			CompositeUniforms compUniforms;
			compUniforms.mix1[0] = 0.8f;
			compUniforms.mix1[1] = 0.8f;
			compUniforms.mix1[2] = 0.8f;
			compUniforms._pad0 = 0.0f;
			compUniforms.mix2[0] = 0.2f;
			compUniforms.mix2[1] = 0.2f;
			compUniforms.mix2[2] = 0.2f;
			compUniforms._pad1 = 0.0f;

			void* compData = compositeUniformBuffer->Map();
			memcpy(compData, &compUniforms, sizeof(compUniforms));
			compositeUniformBuffer->Unmap();

			// Allocate descriptor set
			VkDescriptorSetAllocateInfo finalAllocInfo = {};
			finalAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			finalAllocInfo.descriptorPool = descriptorPool;
			finalAllocInfo.descriptorSetCount = 1;
			finalAllocInfo.pSetLayouts = &finalCompositeDescLayout;

			VkDescriptorSet finalDescriptorSet;
			if (vkAllocateDescriptorSets(device->GetDevice(), &finalAllocInfo, &finalDescriptorSet) != VK_SUCCESS) {
				SPRaise("Failed to allocate final composite descriptor set");
			}

			// Update descriptor set
			VkDescriptorBufferInfo compBufferInfo = {};
			compBufferInfo.buffer = compositeUniformBuffer->GetBuffer();
			compBufferInfo.offset = 0;
			compBufferInfo.range = sizeof(CompositeUniforms);

			VkDescriptorImageInfo inputImageInfo = {};
			inputImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			inputImageInfo.imageView = input->GetImageView();
			inputImageInfo.sampler = input->GetSampler();

			VkDescriptorImageInfo bloomImageInfo = {};
			bloomImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			bloomImageInfo.imageView = levels[0].image->GetImageView();
			bloomImageInfo.sampler = levels[0].image->GetSampler();

			VkWriteDescriptorSet finalWrites[3] = {};
			finalWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			finalWrites[0].dstSet = finalDescriptorSet;
			finalWrites[0].dstBinding = 0;
			finalWrites[0].dstArrayElement = 0;
			finalWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			finalWrites[0].descriptorCount = 1;
			finalWrites[0].pBufferInfo = &compBufferInfo;

			finalWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			finalWrites[1].dstSet = finalDescriptorSet;
			finalWrites[1].dstBinding = 1;
			finalWrites[1].dstArrayElement = 0;
			finalWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			finalWrites[1].descriptorCount = 1;
			finalWrites[1].pImageInfo = &inputImageInfo;

			finalWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			finalWrites[2].dstSet = finalDescriptorSet;
			finalWrites[2].dstBinding = 2;
			finalWrites[2].dstArrayElement = 0;
			finalWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			finalWrites[2].descriptorCount = 1;
			finalWrites[2].pImageInfo = &bloomImageInfo;

			vkUpdateDescriptorSets(device->GetDevice(), 3, finalWrites, 0, nullptr);

			// Begin render pass
			VkRenderPassBeginInfo finalRenderPassInfo = {};
			finalRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			finalRenderPassInfo.renderPass = renderPass;
			finalRenderPassInfo.framebuffer = outputFramebuffer;
			finalRenderPassInfo.renderArea.offset = { 0, 0 };
			finalRenderPassInfo.renderArea.extent = { (uint32_t)output->GetWidth(), (uint32_t)output->GetHeight() };

			vkCmdBeginRenderPass(commandBuffer, &finalRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, finalCompositePipeline);

			VkViewport finalViewport = {};
			finalViewport.x = 0.0f;
			finalViewport.y = 0.0f;
			finalViewport.width = (float)output->GetWidth();
			finalViewport.height = (float)output->GetHeight();
			finalViewport.minDepth = 0.0f;
			finalViewport.maxDepth = 1.0f;
			vkCmdSetViewport(commandBuffer, 0, 1, &finalViewport);

			VkRect2D finalScissor = {};
			finalScissor.offset = { 0, 0 };
			finalScissor.extent = { (uint32_t)output->GetWidth(), (uint32_t)output->GetHeight() };
			vkCmdSetScissor(commandBuffer, 0, 1, &finalScissor);

			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				finalCompositeLayout, 0, 1, &finalDescriptorSet, 0, nullptr);

			vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
			vkCmdBindIndexBuffer(commandBuffer, quadIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);
			vkCmdDrawIndexed(commandBuffer, 6, 1, 0, 0, 0);

			vkCmdEndRenderPass(commandBuffer);

			vkFreeDescriptorSets(device->GetDevice(), descriptorPool, 1, &finalDescriptorSet);
			vkDestroyFramebuffer(device->GetDevice(), outputFramebuffer, nullptr);
		}
	}
}
