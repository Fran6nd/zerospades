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

#include "VulkanDepthOfFieldFilter.h"
#include "VulkanRenderer.h"
#include "VulkanImage.h"
#include "VulkanBuffer.h"
#include "VulkanProgram.h"
#include "VulkanFramebufferManager.h"
#include "VulkanRenderPassUtils.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Core/Math.h>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace spades {
	namespace draw {

		struct CoCGenUniforms {
			float zNearFar[2];
			float pixelShift[2];
			float depthScale;
			float maxVignetteBlur;
			float vignetteScale[2];
			float globalBlur;
			float nearBlur;
			float farBlur;
			float _pad0;
		};

		struct BlurUniforms {
			float offset[2];
			float _pad0[2];
		};

		struct GaussUniforms {
			float unitShift[2];
			float _pad0[2];
		};

		struct FinalMixUniforms {
			int blurredOnly;
			float _pad0;
			float _pad1;
			float _pad2;
		};

		VulkanDepthOfFieldFilter::VulkanDepthOfFieldFilter(VulkanRenderer& r)
			: VulkanPostProcessFilter(r),
			  cocGenPipeline(VK_NULL_HANDLE),
			  cocGenLayout(VK_NULL_HANDLE),
			  cocGenDescLayout(VK_NULL_HANDLE),
			  blurPipeline(VK_NULL_HANDLE),
			  blurLayout(VK_NULL_HANDLE),
			  blurDescLayout(VK_NULL_HANDLE),
			  gaussPipeline(VK_NULL_HANDLE),
			  gaussLayout(VK_NULL_HANDLE),
			  gaussDescLayout(VK_NULL_HANDLE),
			  finalMixPipeline(VK_NULL_HANDLE),
			  finalMixLayout(VK_NULL_HANDLE),
			  finalMixDescLayout(VK_NULL_HANDLE),
			  cocRenderPass(VK_NULL_HANDLE),
			  blurRenderPass(VK_NULL_HANDLE),
			  descriptorPool(VK_NULL_HANDLE) {
			try {
				CreateQuadBuffers();
				CreateDescriptorPool();
				CreateCoCRenderPass();
				CreateBlurRenderPass();
				CreateRenderPass();
				CreatePipeline();
			} catch (...) {
				Cleanup();
				throw;
			}
		}

		VulkanDepthOfFieldFilter::~VulkanDepthOfFieldFilter() {
			Cleanup();
		}

		void VulkanDepthOfFieldFilter::Cleanup() {
			vkDeviceWaitIdle(device->GetDevice());

			if (cocGenPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), cocGenPipeline, nullptr);
				cocGenPipeline = VK_NULL_HANDLE;
			}
			if (blurPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), blurPipeline, nullptr);
				blurPipeline = VK_NULL_HANDLE;
			}
			if (gaussPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), gaussPipeline, nullptr);
				gaussPipeline = VK_NULL_HANDLE;
			}
			if (finalMixPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), finalMixPipeline, nullptr);
				finalMixPipeline = VK_NULL_HANDLE;
			}

			if (cocGenLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), cocGenLayout, nullptr);
				cocGenLayout = VK_NULL_HANDLE;
			}
			if (blurLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), blurLayout, nullptr);
				blurLayout = VK_NULL_HANDLE;
			}
			if (gaussLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), gaussLayout, nullptr);
				gaussLayout = VK_NULL_HANDLE;
			}
			if (finalMixLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), finalMixLayout, nullptr);
				finalMixLayout = VK_NULL_HANDLE;
			}

			if (cocGenDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), cocGenDescLayout, nullptr);
				cocGenDescLayout = VK_NULL_HANDLE;
			}
			if (blurDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), blurDescLayout, nullptr);
				blurDescLayout = VK_NULL_HANDLE;
			}
			if (gaussDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), gaussDescLayout, nullptr);
				gaussDescLayout = VK_NULL_HANDLE;
			}
			if (finalMixDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), finalMixDescLayout, nullptr);
				finalMixDescLayout = VK_NULL_HANDLE;
			}

			if (descriptorPool != VK_NULL_HANDLE) {
				vkDestroyDescriptorPool(device->GetDevice(), descriptorPool, nullptr);
				descriptorPool = VK_NULL_HANDLE;
			}
			if (cocRenderPass != VK_NULL_HANDLE) {
				vkDestroyRenderPass(device->GetDevice(), cocRenderPass, nullptr);
				cocRenderPass = VK_NULL_HANDLE;
			}
			if (blurRenderPass != VK_NULL_HANDLE && blurRenderPass != cocRenderPass) {
				vkDestroyRenderPass(device->GetDevice(), blurRenderPass, nullptr);
			}
			blurRenderPass = VK_NULL_HANDLE;

			DestroyResources();
		}

		void VulkanDepthOfFieldFilter::CreateQuadBuffers() {
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

			cocGenUniformBuffer = new VulkanBuffer(device, sizeof(CoCGenUniforms),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			blurUniformBuffer = new VulkanBuffer(device, sizeof(BlurUniforms),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			gaussUniformBuffer = new VulkanBuffer(device, sizeof(GaussUniforms),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			finalMixUniformBuffer = new VulkanBuffer(device, sizeof(FinalMixUniforms),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		}

		void VulkanDepthOfFieldFilter::CreateDescriptorPool() {
			VkDescriptorPoolSize poolSizes[] = {
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 30 },
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 60 }
			};

			VkDescriptorPoolCreateInfo poolInfo = {};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 2;
			poolInfo.pPoolSizes = poolSizes;
			poolInfo.maxSets = 50;
			poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

			if (vkCreateDescriptorPool(device->GetDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
				SPRaise("Failed to create DoF descriptor pool");
			}
		}

		void VulkanDepthOfFieldFilter::CreateCoCRenderPass() {
			VkSubpassDependency dependency = {};
			dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass = 0;
			dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			cocRenderPass = CreateSimpleColorRenderPass(
				device->GetDevice(),
				VK_FORMAT_R8_UNORM,
				VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				&dependency
			);
		}

		void VulkanDepthOfFieldFilter::CreateBlurRenderPass() {
			VkSubpassDependency dependency = {};
			dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass = 0;
			dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			blurRenderPass = CreateSimpleColorRenderPass(
				device->GetDevice(),
				VK_FORMAT_R8G8B8A8_UNORM,
				VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				&dependency
			);
		}

		void VulkanDepthOfFieldFilter::CreateRenderPass() {
			// Use blurRenderPass as the main render pass for output
			renderPass = blurRenderPass;
		}

		void VulkanDepthOfFieldFilter::CreatePipeline() {
			// Load programs
			cocGenProgram = renderer.RegisterProgram("Shaders/Vulkan/PostFilters/DoFCoCGen.vk.program");
			blurProgram = renderer.RegisterProgram("Shaders/Vulkan/PostFilters/DoFBlur.vk.program");
			gaussProgram = renderer.RegisterProgram("Shaders/Vulkan/PostFilters/Gauss1D.vk.program");
			finalMixProgram = renderer.RegisterProgram("Shaders/Vulkan/PostFilters/DoFMix.vk.program");

			// Vertex input (same for all)
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

			// --- CoC Gen pipeline ---
			cocGenDescLayout = cocGenProgram->GetDescriptorSetLayout();

			VkPipelineLayoutCreateInfo cocGenLayoutInfo = {};
			cocGenLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			cocGenLayoutInfo.setLayoutCount = 1;
			cocGenLayoutInfo.pSetLayouts = &cocGenDescLayout;

			if (vkCreatePipelineLayout(device->GetDevice(), &cocGenLayoutInfo, nullptr, &cocGenLayout) != VK_SUCCESS) {
				SPRaise("Failed to create CoC gen pipeline layout");
			}

			auto cocGenStages = cocGenProgram->GetShaderStages();

			VkGraphicsPipelineCreateInfo pipelineInfo = {};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = static_cast<uint32_t>(cocGenStages.size());
			pipelineInfo.pStages = cocGenStages.data();
			pipelineInfo.pVertexInputState = &vertexInputInfo;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterizer;
			pipelineInfo.pMultisampleState = &multisampling;
			pipelineInfo.pDepthStencilState = &depthStencil;
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.pDynamicState = &dynamicState;
			pipelineInfo.layout = cocGenLayout;
			pipelineInfo.renderPass = cocRenderPass;
			pipelineInfo.subpass = 0;

			if (vkCreateGraphicsPipelines(device->GetDevice(), renderer.GetPipelineCache(), 1, &pipelineInfo, nullptr, &cocGenPipeline) != VK_SUCCESS) {
				SPRaise("Failed to create CoC gen pipeline");
			}

			// --- Blur pipeline ---
			blurDescLayout = blurProgram->GetDescriptorSetLayout();

			VkPipelineLayoutCreateInfo blurLayoutInfo = {};
			blurLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			blurLayoutInfo.setLayoutCount = 1;
			blurLayoutInfo.pSetLayouts = &blurDescLayout;

			if (vkCreatePipelineLayout(device->GetDevice(), &blurLayoutInfo, nullptr, &blurLayout) != VK_SUCCESS) {
				SPRaise("Failed to create blur pipeline layout");
			}

			auto blurStages = blurProgram->GetShaderStages();
			pipelineInfo.stageCount = static_cast<uint32_t>(blurStages.size());
			pipelineInfo.pStages = blurStages.data();
			pipelineInfo.layout = blurLayout;
			pipelineInfo.renderPass = blurRenderPass;

			if (vkCreateGraphicsPipelines(device->GetDevice(), renderer.GetPipelineCache(), 1, &pipelineInfo, nullptr, &blurPipeline) != VK_SUCCESS) {
				SPRaise("Failed to create blur pipeline");
			}

			// --- Gauss pipeline ---
			gaussDescLayout = gaussProgram->GetDescriptorSetLayout();

			VkPipelineLayoutCreateInfo gaussLayoutInfo = {};
			gaussLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			gaussLayoutInfo.setLayoutCount = 1;
			gaussLayoutInfo.pSetLayouts = &gaussDescLayout;

			if (vkCreatePipelineLayout(device->GetDevice(), &gaussLayoutInfo, nullptr, &gaussLayout) != VK_SUCCESS) {
				SPRaise("Failed to create gauss pipeline layout");
			}

			auto gaussStages = gaussProgram->GetShaderStages();
			pipelineInfo.stageCount = static_cast<uint32_t>(gaussStages.size());
			pipelineInfo.pStages = gaussStages.data();
			pipelineInfo.layout = gaussLayout;
			pipelineInfo.renderPass = cocRenderPass; // R8 output for CoC blur

			if (vkCreateGraphicsPipelines(device->GetDevice(), renderer.GetPipelineCache(), 1, &pipelineInfo, nullptr, &gaussPipeline) != VK_SUCCESS) {
				SPRaise("Failed to create gauss pipeline");
			}

			// --- Final mix pipeline ---
			finalMixDescLayout = finalMixProgram->GetDescriptorSetLayout();

			VkPipelineLayoutCreateInfo finalMixLayoutInfo = {};
			finalMixLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			finalMixLayoutInfo.setLayoutCount = 1;
			finalMixLayoutInfo.pSetLayouts = &finalMixDescLayout;

			if (vkCreatePipelineLayout(device->GetDevice(), &finalMixLayoutInfo, nullptr, &finalMixLayout) != VK_SUCCESS) {
				SPRaise("Failed to create final mix pipeline layout");
			}

			auto finalMixStages = finalMixProgram->GetShaderStages();
			pipelineInfo.stageCount = static_cast<uint32_t>(finalMixStages.size());
			pipelineInfo.pStages = finalMixStages.data();
			pipelineInfo.layout = finalMixLayout;
			pipelineInfo.renderPass = blurRenderPass;

			if (vkCreateGraphicsPipelines(device->GetDevice(), renderer.GetPipelineCache(), 1, &pipelineInfo, nullptr, &finalMixPipeline) != VK_SUCCESS) {
				SPRaise("Failed to create final mix pipeline");
			}
		}

		void VulkanDepthOfFieldFilter::Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output) {
			Filter(commandBuffer, input, output, 10.0f, 0.0f, 0.0f, 0.0f, 1.0f);
		}

		void VulkanDepthOfFieldFilter::Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output,
		                                      float blurDepthRange, float vignetteBlur, float globalBlur,
		                                      float nearBlur, float farBlur) {
			SPADES_MARK_FUNCTION();

			int w = input->GetWidth();
			int h = input->GetHeight();

			// Clamp globalBlur
			globalBlur = std::min(globalBlur * 3.0f, 1.0f);

			// Generate CoC (Circle of Confusion)
			Handle<VulkanImage> coc = GenerateCoC(commandBuffer, (w + 3) / 4, (h + 3) / 4,
				blurDepthRange, vignetteBlur, globalBlur, nearBlur, farBlur);

			// Calculate max CoC for blur offset
			float maxCoc = (float)std::max(w, h) * 0.05f; // Simplified
			maxCoc *= 0.7f + vignetteBlur * 0.5f;
			maxCoc *= 1.0f + 3.0f * globalBlur;

			float cos60 = cosf(static_cast<float>(M_PI) / 3.0f);
			float sin60 = sinf(static_cast<float>(M_PI) / 3.0f);

			// Apply directional blur passes
			Handle<VulkanImage> blur1 = BlurWithCoC(commandBuffer, input, coc.GetPointerOrNull(),
				0.0f, -maxCoc / (float)h, w, h);

			Handle<VulkanImage> blur2 = BlurWithCoC(commandBuffer, input, coc.GetPointerOrNull(),
				-sin60 * maxCoc / (float)w, cos60 * maxCoc / (float)h, w, h);

			// Final mix - create output framebuffer
			VkFramebuffer outputFramebuffer;
			VkImageView outputAttachments[] = { output->GetImageView() };
			VkFramebufferCreateInfo outputFbInfo = {};
			outputFbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			outputFbInfo.renderPass = blurRenderPass;
			outputFbInfo.attachmentCount = 1;
			outputFbInfo.pAttachments = outputAttachments;
			outputFbInfo.width = w;
			outputFbInfo.height = h;
			outputFbInfo.layers = 1;

			if (vkCreateFramebuffer(device->GetDevice(), &outputFbInfo, nullptr, &outputFramebuffer) != VK_SUCCESS) {
				SPRaise("Failed to create DoF output framebuffer");
			}

			// Update final mix uniforms
			FinalMixUniforms finalUniforms;
			finalUniforms.blurredOnly = 0;
			finalUniforms._pad0 = 0.0f;
			finalUniforms._pad1 = 0.0f;
			finalUniforms._pad2 = 0.0f;

			void* data = finalMixUniformBuffer->Map();
			memcpy(data, &finalUniforms, sizeof(finalUniforms));
			finalMixUniformBuffer->Unmap();

			// Allocate descriptor set
			VkDescriptorSetAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &finalMixDescLayout;

			VkDescriptorSet descriptorSet;
			if (vkAllocateDescriptorSets(device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
				SPRaise("Failed to allocate final mix descriptor set");
			}

			// Update descriptor set
			VkDescriptorBufferInfo bufferInfo = {};
			bufferInfo.buffer = finalMixUniformBuffer->GetBuffer();
			bufferInfo.offset = 0;
			bufferInfo.range = sizeof(FinalMixUniforms);

			VkDescriptorImageInfo mainImageInfo = {};
			mainImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			mainImageInfo.imageView = input->GetImageView();
			mainImageInfo.sampler = input->GetSampler();

			VkDescriptorImageInfo blur1ImageInfo = {};
			blur1ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			blur1ImageInfo.imageView = blur1->GetImageView();
			blur1ImageInfo.sampler = blur1->GetSampler();

			VkDescriptorImageInfo blur2ImageInfo = {};
			blur2ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			blur2ImageInfo.imageView = blur2->GetImageView();
			blur2ImageInfo.sampler = blur2->GetSampler();

			VkDescriptorImageInfo cocImageInfo = {};
			cocImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			cocImageInfo.imageView = coc->GetImageView();
			cocImageInfo.sampler = coc->GetSampler();

			VkWriteDescriptorSet descriptorWrites[5] = {};
			descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[0].dstSet = descriptorSet;
			descriptorWrites[0].dstBinding = 0;
			descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descriptorWrites[0].descriptorCount = 1;
			descriptorWrites[0].pBufferInfo = &bufferInfo;

			descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[1].dstSet = descriptorSet;
			descriptorWrites[1].dstBinding = 1;
			descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrites[1].descriptorCount = 1;
			descriptorWrites[1].pImageInfo = &mainImageInfo;

			descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[2].dstSet = descriptorSet;
			descriptorWrites[2].dstBinding = 2;
			descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrites[2].descriptorCount = 1;
			descriptorWrites[2].pImageInfo = &blur1ImageInfo;

			descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[3].dstSet = descriptorSet;
			descriptorWrites[3].dstBinding = 3;
			descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrites[3].descriptorCount = 1;
			descriptorWrites[3].pImageInfo = &blur2ImageInfo;

			descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[4].dstSet = descriptorSet;
			descriptorWrites[4].dstBinding = 4;
			descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrites[4].descriptorCount = 1;
			descriptorWrites[4].pImageInfo = &cocImageInfo;

			vkUpdateDescriptorSets(device->GetDevice(), 5, descriptorWrites, 0, nullptr);

			// Pipeline barriers for blur textures
			VkImageMemoryBarrier barriers[2] = {};
			barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barriers[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barriers[0].image = blur1->GetImage();
			barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barriers[0].subresourceRange.levelCount = 1;
			barriers[0].subresourceRange.layerCount = 1;
			barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			barriers[1] = barriers[0];
			barriers[1].image = blur2->GetImage();

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 2, barriers);

			// Begin render pass
			VkRenderPassBeginInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = blurRenderPass;
			renderPassInfo.framebuffer = outputFramebuffer;
			renderPassInfo.renderArea.extent = { (uint32_t)w, (uint32_t)h };

			vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, finalMixPipeline);

			VkViewport viewport = {};
			viewport.width = (float)w;
			viewport.height = (float)h;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

			VkRect2D scissor = {};
			scissor.extent = { (uint32_t)w, (uint32_t)h };
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				finalMixLayout, 0, 1, &descriptorSet, 0, nullptr);

			VkBuffer vertexBuffers[] = { quadVertexBuffer->GetBuffer() };
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
			vkCmdBindIndexBuffer(commandBuffer, quadIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);
			vkCmdDrawIndexed(commandBuffer, 6, 1, 0, 0, 0);

			vkCmdEndRenderPass(commandBuffer);

			vkFreeDescriptorSets(device->GetDevice(), descriptorPool, 1, &descriptorSet);
			vkDestroyFramebuffer(device->GetDevice(), outputFramebuffer, nullptr);
		}

		Handle<VulkanImage> VulkanDepthOfFieldFilter::GenerateCoC(VkCommandBuffer commandBuffer, int width, int height,
			float blurDepthRange, float vignetteBlur, float globalBlur, float nearBlur, float farBlur) {

			Handle<VulkanImage> output = new VulkanImage(device, width, height, VK_FORMAT_R8_UNORM,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			VkFramebuffer framebuffer;
			VkImageView attachments[] = { output->GetImageView() };
			VkFramebufferCreateInfo framebufferInfo = {};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = cocRenderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = attachments;
			framebufferInfo.width = width;
			framebufferInfo.height = height;
			framebufferInfo.layers = 1;

			if (vkCreateFramebuffer(device->GetDevice(), &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
				SPRaise("Failed to create CoC framebuffer");
			}

			const client::SceneDefinition& def = renderer.GetSceneDef();
			int fullW = (int)renderer.ScreenWidth();
			int fullH = (int)renderer.ScreenHeight();

			CoCGenUniforms uniforms;
			uniforms.zNearFar[0] = def.zNear;
			uniforms.zNearFar[1] = def.zFar;
			uniforms.pixelShift[0] = 1.0f / (float)fullW;
			uniforms.pixelShift[1] = 1.0f / (float)fullH;
			uniforms.depthScale = 1.0f / blurDepthRange;
			uniforms.maxVignetteBlur = sinf(std::max(def.fovX, def.fovY) * 0.5f) * vignetteBlur;
			if (fullH > fullW) {
				uniforms.vignetteScale[0] = 2.0f * (float)fullW / (float)fullH;
				uniforms.vignetteScale[1] = 2.0f;
			} else {
				uniforms.vignetteScale[0] = 2.0f;
				uniforms.vignetteScale[1] = 2.0f * (float)fullH / (float)fullW;
			}
			uniforms.globalBlur = globalBlur;
			uniforms.nearBlur = nearBlur;
			uniforms.farBlur = -farBlur;
			uniforms._pad0 = 0.0f;

			void* data = cocGenUniformBuffer->Map();
			memcpy(data, &uniforms, sizeof(uniforms));
			cocGenUniformBuffer->Unmap();

			VkDescriptorSetAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &cocGenDescLayout;

			VkDescriptorSet descriptorSet;
			if (vkAllocateDescriptorSets(device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
				SPRaise("Failed to allocate CoC descriptor set");
			}

			VulkanImage* depthImage = renderer.GetFramebufferManager()->GetDepthImage().GetPointerOrNull();

			VkDescriptorBufferInfo bufferInfo = {};
			bufferInfo.buffer = cocGenUniformBuffer->GetBuffer();
			bufferInfo.offset = 0;
			bufferInfo.range = sizeof(CoCGenUniforms);

			VkDescriptorImageInfo depthImageInfo = {};
			depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			depthImageInfo.imageView = depthImage->GetImageView();
			depthImageInfo.sampler = depthImage->GetSampler();

			VkWriteDescriptorSet descriptorWrites[2] = {};
			descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[0].dstSet = descriptorSet;
			descriptorWrites[0].dstBinding = 0;
			descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descriptorWrites[0].descriptorCount = 1;
			descriptorWrites[0].pBufferInfo = &bufferInfo;

			descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[1].dstSet = descriptorSet;
			descriptorWrites[1].dstBinding = 1;
			descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrites[1].descriptorCount = 1;
			descriptorWrites[1].pImageInfo = &depthImageInfo;

			vkUpdateDescriptorSets(device->GetDevice(), 2, descriptorWrites, 0, nullptr);

			VkRenderPassBeginInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = cocRenderPass;
			renderPassInfo.framebuffer = framebuffer;
			renderPassInfo.renderArea.extent = { (uint32_t)width, (uint32_t)height };

			vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, cocGenPipeline);

			VkViewport viewport = {};
			viewport.width = (float)width;
			viewport.height = (float)height;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

			VkRect2D scissor = {};
			scissor.extent = { (uint32_t)width, (uint32_t)height };
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				cocGenLayout, 0, 1, &descriptorSet, 0, nullptr);

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

		Handle<VulkanImage> VulkanDepthOfFieldFilter::BlurWithCoC(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* coc,
			float offsetX, float offsetY, int width, int height) {

			Handle<VulkanImage> output = new VulkanImage(device, width, height, VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			VkFramebuffer framebuffer;
			VkImageView attachments[] = { output->GetImageView() };
			VkFramebufferCreateInfo framebufferInfo = {};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = blurRenderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = attachments;
			framebufferInfo.width = width;
			framebufferInfo.height = height;
			framebufferInfo.layers = 1;

			if (vkCreateFramebuffer(device->GetDevice(), &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
				SPRaise("Failed to create blur framebuffer");
			}

			BlurUniforms uniforms;
			uniforms.offset[0] = offsetX;
			uniforms.offset[1] = offsetY;
			uniforms._pad0[0] = 0.0f;
			uniforms._pad0[1] = 0.0f;

			void* data = blurUniformBuffer->Map();
			memcpy(data, &uniforms, sizeof(uniforms));
			blurUniformBuffer->Unmap();

			VkDescriptorSetAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &blurDescLayout;

			VkDescriptorSet descriptorSet;
			if (vkAllocateDescriptorSets(device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
				SPRaise("Failed to allocate blur descriptor set");
			}

			VkDescriptorBufferInfo bufferInfo = {};
			bufferInfo.buffer = blurUniformBuffer->GetBuffer();
			bufferInfo.offset = 0;
			bufferInfo.range = sizeof(BlurUniforms);

			VkDescriptorImageInfo mainImageInfo = {};
			mainImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			mainImageInfo.imageView = input->GetImageView();
			mainImageInfo.sampler = input->GetSampler();

			VkDescriptorImageInfo cocImageInfo = {};
			cocImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			cocImageInfo.imageView = coc->GetImageView();
			cocImageInfo.sampler = coc->GetSampler();

			VkWriteDescriptorSet descriptorWrites[3] = {};
			descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[0].dstSet = descriptorSet;
			descriptorWrites[0].dstBinding = 0;
			descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descriptorWrites[0].descriptorCount = 1;
			descriptorWrites[0].pBufferInfo = &bufferInfo;

			descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[1].dstSet = descriptorSet;
			descriptorWrites[1].dstBinding = 1;
			descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrites[1].descriptorCount = 1;
			descriptorWrites[1].pImageInfo = &mainImageInfo;

			descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[2].dstSet = descriptorSet;
			descriptorWrites[2].dstBinding = 2;
			descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrites[2].descriptorCount = 1;
			descriptorWrites[2].pImageInfo = &cocImageInfo;

			vkUpdateDescriptorSets(device->GetDevice(), 3, descriptorWrites, 0, nullptr);

			// Transition CoC image
			VkImageMemoryBarrier barrier = {};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = coc->GetImage();
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.layerCount = 1;
			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			VkRenderPassBeginInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = blurRenderPass;
			renderPassInfo.framebuffer = framebuffer;
			renderPassInfo.renderArea.extent = { (uint32_t)width, (uint32_t)height };

			vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline);

			VkViewport viewport = {};
			viewport.width = (float)width;
			viewport.height = (float)height;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

			VkRect2D scissor = {};
			scissor.extent = { (uint32_t)width, (uint32_t)height };
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				blurLayout, 0, 1, &descriptorSet, 0, nullptr);

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

		Handle<VulkanImage> VulkanDepthOfFieldFilter::GaussBlur(VkCommandBuffer commandBuffer, VulkanImage* input, bool horizontal, float spread) {
			// Simplified gauss blur - not fully implemented
			return input;
		}
	}
}
