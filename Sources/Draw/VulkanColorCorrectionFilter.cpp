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

#include <cmath>

#include "VulkanColorCorrectionFilter.h"
#include "VulkanRenderer.h"
#include "VulkanImage.h"
#include "VulkanBuffer.h"
#include "VulkanProgram.h"
#include "VulkanFramebufferManager.h"
#include "VulkanRenderPassUtils.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Core/Settings.h>
#include <Client/SceneDefinition.h>

SPADES_SETTING(r_sharpen);
SPADES_SETTING(r_temporalAA);
SPADES_SETTING(r_hdr);
SPADES_SETTING(r_bloom);
SPADES_SETTING(r_saturation);

namespace spades {
	namespace draw {

		struct ColorCorrectionUniforms {
			float enhancement;
			float saturation;
			float _pad0;
			float _pad1;
			float tint[3];
			float sharpening;
			float sharpeningFinalGain;
			float blurPixelShift;
			int useHDR;
			float _pad2;
		};

		struct Gauss1DUniforms {
			float unitShift[2];
			float _pad[2];
		};

		VulkanColorCorrectionFilter::VulkanColorCorrectionFilter(VulkanRenderer& r)
			: VulkanPostProcessFilter(r),
			  uniformBuffer(nullptr),
			  gaussUniformBuffer(nullptr),
			  quadVertexBuffer(nullptr),
			  quadIndexBuffer(nullptr),
			  descriptorPool(VK_NULL_HANDLE),
			  framebuffer(VK_NULL_HANDLE),
			  gaussPipeline(VK_NULL_HANDLE),
			  gaussPipelineLayout(VK_NULL_HANDLE),
			  gaussDescriptorSetLayout(VK_NULL_HANDLE),
			  gaussRenderPass(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			CreateRenderPass();
			CreateGaussRenderPass();
			CreateQuadBuffers();
			CreatePipeline();
			CreateGaussPipeline();
			CreateDescriptorPool();
		}

		VulkanColorCorrectionFilter::~VulkanColorCorrectionFilter() {
			DestroyResources();

			if (descriptorPool != VK_NULL_HANDLE) {
				vkDestroyDescriptorPool(device->GetDevice(), descriptorPool, nullptr);
				descriptorPool = VK_NULL_HANDLE;
			}

			if (framebuffer != VK_NULL_HANDLE) {
				vkDestroyFramebuffer(device->GetDevice(), framebuffer, nullptr);
				framebuffer = VK_NULL_HANDLE;
			}

			if (gaussPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), gaussPipeline, nullptr);
				gaussPipeline = VK_NULL_HANDLE;
			}

			if (gaussPipelineLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), gaussPipelineLayout, nullptr);
				gaussPipelineLayout = VK_NULL_HANDLE;
			}

			if (gaussDescriptorSetLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), gaussDescriptorSetLayout, nullptr);
				gaussDescriptorSetLayout = VK_NULL_HANDLE;
			}

			if (gaussRenderPass != VK_NULL_HANDLE) {
				vkDestroyRenderPass(device->GetDevice(), gaussRenderPass, nullptr);
				gaussRenderPass = VK_NULL_HANDLE;
			}
		}

		void VulkanColorCorrectionFilter::CreateRenderPass() {
			renderPass = CreateSimpleColorRenderPass(device->GetDevice(), VK_FORMAT_R8G8B8A8_UNORM);
		}

		void VulkanColorCorrectionFilter::CreateGaussRenderPass() {
			gaussRenderPass = CreateSimpleColorRenderPass(device->GetDevice(), VK_FORMAT_R8G8B8A8_UNORM);
		}

		void VulkanColorCorrectionFilter::CreateQuadBuffers() {
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

		void VulkanColorCorrectionFilter::CreatePipeline() {
			SPADES_MARK_FUNCTION();

			VulkanProgram* program = renderer.RegisterProgram("Shaders/PostFilters/ColorCorrection.vk.program");
			if (!program || !program->IsLinked()) {
				SPRaise("Failed to load ColorCorrection shader program");
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
				SPRaise("Failed to create color correction filter pipeline");
			}

			SPLog("VulkanColorCorrectionFilter pipeline created successfully");
		}

		void VulkanColorCorrectionFilter::CreateGaussPipeline() {
			SPADES_MARK_FUNCTION();

			VulkanProgram* program = renderer.RegisterProgram("Shaders/PostFilters/Gauss1D.vk.program");
			if (!program || !program->IsLinked()) {
				SPRaise("Failed to load Gauss1D shader program");
			}

			gaussDescriptorSetLayout = program->GetDescriptorSetLayout();
			gaussPipelineLayout = program->GetPipelineLayout();

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
			pipelineInfo.layout = gaussPipelineLayout;
			pipelineInfo.renderPass = gaussRenderPass;
			pipelineInfo.subpass = 0;

			if (vkCreateGraphicsPipelines(device->GetDevice(), renderer.GetPipelineCache(), 1, &pipelineInfo, nullptr, &gaussPipeline) != VK_SUCCESS) {
				SPRaise("Failed to create gaussian blur pipeline");
			}

			SPLog("VulkanColorCorrectionFilter gauss pipeline created successfully");
		}

		void VulkanColorCorrectionFilter::CreateDescriptorPool() {
			SPADES_MARK_FUNCTION();

			VkDescriptorPoolSize poolSizes[2];
			poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSizes[0].descriptorCount = 20;
			poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			poolSizes[1].descriptorCount = 20;

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 2;
			poolInfo.pPoolSizes = poolSizes;
			poolInfo.maxSets = 20;
			poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

			if (vkCreateDescriptorPool(device->GetDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
				SPRaise("Failed to create color correction filter descriptor pool");
			}
		}

		void VulkanColorCorrectionFilter::Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output) {
			Filter(commandBuffer, input, output, MakeVector3(1.0f, 1.0f, 1.0f), 1.0f);
		}

		void VulkanColorCorrectionFilter::Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output,
		                                         Vector3 tint, float fogLuminance) {
			SPADES_MARK_FUNCTION();

			if (!pipeline || !input || !output) {
				return;
			}

			const client::SceneDefinition& sceneDef = renderer.GetSceneDef();

			// Calculate sharpening parameters
			float sharpeningFinalGainValue = Clamp((float)r_sharpen, 0.0f, 1.0f);
			float sharpeningFloor = 0.0f;

			if (r_temporalAA)
				sharpeningFloor = 1.5f;

			float sharpeningValue = std::max(std::sqrt(fogLuminance) * 2.7f, sharpeningFloor);

			// Calculate saturation and enhancement based on HDR/bloom settings
			float saturationValue, enhancementValue;
			bool useHDR = (bool)r_hdr;

			if (useHDR) {
				if ((bool)r_bloom) {
					saturationValue = 0.8f * sceneDef.saturation * (float)r_saturation;
					enhancementValue = 0.1f;
				} else {
					saturationValue = 0.9f * sceneDef.saturation * (float)r_saturation;
					enhancementValue = 0.0f;
				}
			} else {
				if ((bool)r_bloom) {
					saturationValue = 0.85f * sceneDef.saturation * (float)r_saturation;
					enhancementValue = 0.7f;
				} else {
					saturationValue = 1.0f * sceneDef.saturation * (float)r_saturation;
					enhancementValue = 0.3f;
				}
			}

			// Create blurred input for sharpening (horizontal gaussian blur)
			Handle<VulkanImage> blurredInput;
			VkFramebuffer blurFramebuffer = VK_NULL_HANDLE;

			if (sharpeningFinalGainValue > 0.0f) {
				// Create temporary image for blurred result
				blurredInput = Handle<VulkanImage>::New(
					device,
					input->GetWidth(),
					input->GetHeight(),
					VK_FORMAT_R8G8B8A8_UNORM,
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
				);
				blurredInput->CreateSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);

				// Create framebuffer for blur pass
				VkFramebufferCreateInfo blurFbInfo{};
				blurFbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				blurFbInfo.renderPass = gaussRenderPass;
				blurFbInfo.attachmentCount = 1;
				VkImageView blurAttachments[] = {blurredInput->GetImageView()};
				blurFbInfo.pAttachments = blurAttachments;
				blurFbInfo.width = input->GetWidth();
				blurFbInfo.height = input->GetHeight();
				blurFbInfo.layers = 1;

				if (vkCreateFramebuffer(device->GetDevice(), &blurFbInfo, nullptr, &blurFramebuffer) != VK_SUCCESS) {
					SPRaise("Failed to create blur framebuffer");
				}

				// Setup gaussian blur uniforms
				Gauss1DUniforms gaussUniforms;
				gaussUniforms.unitShift[0] = 1.0f / (float)input->GetWidth();
				gaussUniforms.unitShift[1] = 0.0f;

				if (!gaussUniformBuffer) {
					gaussUniformBuffer = Handle<VulkanBuffer>::New(
						device,
						sizeof(Gauss1DUniforms),
						VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
						VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
					);
				}
				gaussUniformBuffer->UpdateData(&gaussUniforms, sizeof(gaussUniforms));

				// Allocate descriptor set for blur pass
				VkDescriptorSetAllocateInfo gaussAllocInfo{};
				gaussAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				gaussAllocInfo.descriptorPool = descriptorPool;
				gaussAllocInfo.descriptorSetCount = 1;
				gaussAllocInfo.pSetLayouts = &gaussDescriptorSetLayout;

				VkDescriptorSet gaussDescriptorSet;
				if (vkAllocateDescriptorSets(device->GetDevice(), &gaussAllocInfo, &gaussDescriptorSet) != VK_SUCCESS) {
					vkDestroyFramebuffer(device->GetDevice(), blurFramebuffer, nullptr);
					SPLog("Warning: Failed to allocate gauss descriptor set");
					return;
				}

				// Update descriptor set for blur
				VkDescriptorImageInfo inputImageInfo{};
				inputImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				inputImageInfo.imageView = input->GetImageView();
				inputImageInfo.sampler = input->GetSampler();

				VkDescriptorBufferInfo gaussBufferInfo{};
				gaussBufferInfo.buffer = gaussUniformBuffer->GetBuffer();
				gaussBufferInfo.offset = 0;
				gaussBufferInfo.range = sizeof(Gauss1DUniforms);

				VkWriteDescriptorSet gaussWrites[2] = {};
				gaussWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				gaussWrites[0].dstSet = gaussDescriptorSet;
				gaussWrites[0].dstBinding = 0;
				gaussWrites[0].dstArrayElement = 0;
				gaussWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				gaussWrites[0].descriptorCount = 1;
				gaussWrites[0].pImageInfo = &inputImageInfo;

				gaussWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				gaussWrites[1].dstSet = gaussDescriptorSet;
				gaussWrites[1].dstBinding = 1;
				gaussWrites[1].dstArrayElement = 0;
				gaussWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				gaussWrites[1].descriptorCount = 1;
				gaussWrites[1].pBufferInfo = &gaussBufferInfo;

				vkUpdateDescriptorSets(device->GetDevice(), 2, gaussWrites, 0, nullptr);

				// Execute blur pass
				VkRenderPassBeginInfo blurRenderPassInfo{};
				blurRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				blurRenderPassInfo.renderPass = gaussRenderPass;
				blurRenderPassInfo.framebuffer = blurFramebuffer;
				blurRenderPassInfo.renderArea.offset = {0, 0};
				blurRenderPassInfo.renderArea.extent = {input->GetWidth(), input->GetHeight()};

				vkCmdBeginRenderPass(commandBuffer, &blurRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gaussPipeline);

				VkViewport blurViewport{};
				blurViewport.x = 0.0f;
				blurViewport.y = 0.0f;
				blurViewport.width = static_cast<float>(input->GetWidth());
				blurViewport.height = static_cast<float>(input->GetHeight());
				blurViewport.minDepth = 0.0f;
				blurViewport.maxDepth = 1.0f;
				vkCmdSetViewport(commandBuffer, 0, 1, &blurViewport);

				VkRect2D blurScissor{};
				blurScissor.offset = {0, 0};
				blurScissor.extent = {input->GetWidth(), input->GetHeight()};
				vkCmdSetScissor(commandBuffer, 0, 1, &blurScissor);

				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gaussPipelineLayout,
				                       0, 1, &gaussDescriptorSet, 0, nullptr);

				VkBuffer vertexBuffers[] = {quadVertexBuffer->GetBuffer()};
				VkDeviceSize offsets[] = {0};
				vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
				vkCmdBindIndexBuffer(commandBuffer, quadIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

				vkCmdDrawIndexed(commandBuffer, 6, 1, 0, 0, 0);

				vkCmdEndRenderPass(commandBuffer);

				vkFreeDescriptorSets(device->GetDevice(), descriptorPool, 1, &gaussDescriptorSet);
			}

			// Setup color correction uniforms
			ColorCorrectionUniforms uniforms;
			uniforms.enhancement = enhancementValue;
			uniforms.saturation = saturationValue;
			uniforms.tint[0] = tint.x;
			uniforms.tint[1] = tint.y;
			uniforms.tint[2] = tint.z;
			uniforms.sharpening = sharpeningValue;
			uniforms.sharpeningFinalGain = sharpeningFinalGainValue;
			uniforms.blurPixelShift = 1.0f / (float)input->GetHeight();
			uniforms.useHDR = useHDR ? 1 : 0;

			if (!uniformBuffer) {
				uniformBuffer = Handle<VulkanBuffer>::New(
					device,
					sizeof(uniforms),
					VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
				);
			}
			uniformBuffer->UpdateData(&uniforms, sizeof(uniforms));

			// Allocate descriptor set for color correction
			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &descriptorSetLayout;

			VkDescriptorSet descriptorSet;
			if (vkAllocateDescriptorSets(device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
				if (blurFramebuffer != VK_NULL_HANDLE) {
					vkDestroyFramebuffer(device->GetDevice(), blurFramebuffer, nullptr);
				}
				SPLog("Warning: Failed to allocate color correction descriptor set");
				return;
			}

			// Update descriptor set
			VkDescriptorImageInfo mainImageInfo{};
			mainImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			mainImageInfo.imageView = input->GetImageView();
			mainImageInfo.sampler = input->GetSampler();

			VkDescriptorImageInfo blurredImageInfo{};
			blurredImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			if (blurredInput) {
				blurredImageInfo.imageView = blurredInput->GetImageView();
				blurredImageInfo.sampler = blurredInput->GetSampler();
			} else {
				blurredImageInfo.imageView = input->GetImageView();
				blurredImageInfo.sampler = input->GetSampler();
			}

			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = uniformBuffer->GetBuffer();
			bufferInfo.offset = 0;
			bufferInfo.range = sizeof(uniforms);

			VkWriteDescriptorSet descriptorWrites[3] = {};
			descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[0].dstSet = descriptorSet;
			descriptorWrites[0].dstBinding = 0;
			descriptorWrites[0].dstArrayElement = 0;
			descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrites[0].descriptorCount = 1;
			descriptorWrites[0].pImageInfo = &mainImageInfo;

			descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[1].dstSet = descriptorSet;
			descriptorWrites[1].dstBinding = 1;
			descriptorWrites[1].dstArrayElement = 0;
			descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrites[1].descriptorCount = 1;
			descriptorWrites[1].pImageInfo = &blurredImageInfo;

			descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[2].dstSet = descriptorSet;
			descriptorWrites[2].dstBinding = 2;
			descriptorWrites[2].dstArrayElement = 0;
			descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descriptorWrites[2].descriptorCount = 1;
			descriptorWrites[2].pBufferInfo = &bufferInfo;

			vkUpdateDescriptorSets(device->GetDevice(), 3, descriptorWrites, 0, nullptr);

			// Create output framebuffer
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
				if (blurFramebuffer != VK_NULL_HANDLE) {
					vkDestroyFramebuffer(device->GetDevice(), blurFramebuffer, nullptr);
				}
				SPRaise("Failed to create color correction filter framebuffer");
			}

			// Execute color correction pass
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

			if (blurFramebuffer != VK_NULL_HANDLE) {
				vkDestroyFramebuffer(device->GetDevice(), blurFramebuffer, nullptr);
			}
		}
	}
}
