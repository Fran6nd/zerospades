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
#include "VulkanBuffer.h"
#include "VulkanProgram.h"
#include "VulkanFramebufferManager.h"
#include "VulkanFXAAFilter.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Client/SceneDefinition.h>

namespace spades {
	namespace draw {

		struct TemporalAAUniforms {
			float inverseVP[2];
			float fogDistance;
			float _pad0;
			float reprojectionMatrix[16];
			float viewProjectionMatrixInv[16];
		};

		VulkanTemporalAAFilter::VulkanTemporalAAFilter(VulkanRenderer& r)
			: VulkanPostProcessFilter(r),
			  uniformBuffer(nullptr),
			  quadVertexBuffer(nullptr),
			  quadIndexBuffer(nullptr),
			  descriptorPool(VK_NULL_HANDLE),
			  framebuffer(VK_NULL_HANDLE),
			  copyRenderPass(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			prevMatrix = Matrix4::Identity();
			prevViewOrigin = Vector3(0.0f, 0.0f, 0.0f);
			historyBuffer.valid = false;
			historyBuffer.framebuffer = VK_NULL_HANDLE;

			CreateRenderPass();
			CreateCopyRenderPass();
			CreateQuadBuffers();
			CreatePipeline();
			CreateDescriptorPool();
		}

		VulkanTemporalAAFilter::~VulkanTemporalAAFilter() {
			DeleteHistoryBuffer();
			DestroyResources();

			if (descriptorPool != VK_NULL_HANDLE) {
				vkDestroyDescriptorPool(device->GetDevice(), descriptorPool, nullptr);
				descriptorPool = VK_NULL_HANDLE;
			}

			if (framebuffer != VK_NULL_HANDLE) {
				vkDestroyFramebuffer(device->GetDevice(), framebuffer, nullptr);
				framebuffer = VK_NULL_HANDLE;
			}

			if (copyRenderPass != VK_NULL_HANDLE) {
				vkDestroyRenderPass(device->GetDevice(), copyRenderPass, nullptr);
				copyRenderPass = VK_NULL_HANDLE;
			}
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

		void VulkanTemporalAAFilter::CreateCopyRenderPass() {
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

			if (vkCreateRenderPass(device->GetDevice(), &renderPassInfo, nullptr, &copyRenderPass) != VK_SUCCESS) {
				SPRaise("Failed to create temporal AA copy render pass");
			}
		}

		void VulkanTemporalAAFilter::CreateQuadBuffers() {
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

		void VulkanTemporalAAFilter::CreatePipeline() {
			SPADES_MARK_FUNCTION();

			VulkanProgram* program = renderer.RegisterProgram("Shaders/PostFilters/TemporalAA.vk.program");
			if (!program || !program->IsLinked()) {
				SPRaise("Failed to load TemporalAA shader program");
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
				SPRaise("Failed to create temporal AA filter pipeline");
			}

			SPLog("VulkanTemporalAAFilter pipeline created successfully");
		}

		void VulkanTemporalAAFilter::CreateDescriptorPool() {
			SPADES_MARK_FUNCTION();

			VkDescriptorPoolSize poolSizes[2];
			poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSizes[0].descriptorCount = 40;
			poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			poolSizes[1].descriptorCount = 10;

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 2;
			poolInfo.pPoolSizes = poolSizes;
			poolInfo.maxSets = 10;
			poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

			if (vkCreateDescriptorPool(device->GetDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
				SPRaise("Failed to create temporal AA filter descriptor pool");
			}
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

			if (!pipeline || !input || !output) {
				return;
			}

			// Calculate the current view-projection matrix
			const client::SceneDefinition& def = renderer.GetSceneDef();

			Matrix4 viewMatrix = def.ToViewMatrix();
			Matrix4 projMatrix = def.ToOpenGLProjectionMatrix();

			// Exclude translation from view matrix
			viewMatrix.m[12] = 0.0f;
			viewMatrix.m[13] = 0.0f;
			viewMatrix.m[14] = 0.0f;

			Matrix4 newMatrix = projMatrix * viewMatrix;

			// Convert from [-1,1] to [0,1] range
			newMatrix = Matrix4::Translate(1.0f, 1.0f, 1.0f) * newMatrix;
			newMatrix = Matrix4::Scale(0.5f, 0.5f, 0.5f) * newMatrix;

			// Camera translation incorporated separately
			Matrix4 translationMatrix = Matrix4::Translate(def.viewOrigin - prevViewOrigin);

			// Compute the reprojection matrix
			Matrix4 inverseNewMatrix = newMatrix.Inversed();
			Matrix4 diffMatrix = prevMatrix * translationMatrix * inverseNewMatrix;
			prevMatrix = newMatrix;
			prevViewOrigin = def.viewOrigin;

			// Check if history buffer needs recreation
			if (!historyBuffer.valid || historyBuffer.width != (int)input->GetWidth() ||
			    historyBuffer.height != (int)input->GetHeight()) {
				DeleteHistoryBuffer();

				historyBuffer.width = input->GetWidth();
				historyBuffer.height = input->GetHeight();

				// Create history buffer image
				historyBuffer.image = Handle<VulkanImage>::New(
					device,
					input->GetWidth(),
					input->GetHeight(),
					VK_FORMAT_R8G8B8A8_UNORM,
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
				);
				historyBuffer.image->CreateSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);

				// Create framebuffer for history buffer
				VkFramebufferCreateInfo fbInfo{};
				fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				fbInfo.renderPass = copyRenderPass;
				fbInfo.attachmentCount = 1;
				VkImageView attachments[] = {historyBuffer.image->GetImageView()};
				fbInfo.pAttachments = attachments;
				fbInfo.width = input->GetWidth();
				fbInfo.height = input->GetHeight();
				fbInfo.layers = 1;

				if (vkCreateFramebuffer(device->GetDevice(), &fbInfo, nullptr, &historyBuffer.framebuffer) != VK_SUCCESS) {
					SPRaise("Failed to create history buffer framebuffer");
				}

				historyBuffer.valid = true;

				SPLog("Created temporal AA history buffer %dx%d", historyBuffer.width, historyBuffer.height);

				// Initialize with 0.5 mix rate in alpha
				// For simplicity, just return the input as output on the first frame
				// The history buffer will be filled after the first render
				return;
			}

			// Setup uniforms
			TemporalAAUniforms uniforms;
			uniforms.inverseVP[0] = 1.0f / (float)input->GetWidth();
			uniforms.inverseVP[1] = 1.0f / (float)input->GetHeight();
			uniforms.fogDistance = 128.0f;

			for (int i = 0; i < 16; i++) {
				uniforms.reprojectionMatrix[i] = diffMatrix.m[i];
				uniforms.viewProjectionMatrixInv[i] = inverseNewMatrix.m[i];
			}

			if (!uniformBuffer) {
				uniformBuffer = Handle<VulkanBuffer>::New(
					device,
					sizeof(uniforms),
					VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
				);
			}
			uniformBuffer->UpdateData(&uniforms, sizeof(uniforms));

			// Get depth texture from framebuffer manager
			Handle<VulkanImage> depthImage = renderer.GetFramebufferManager()->GetDepthImage();

			// Allocate descriptor set
			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &descriptorSetLayout;

			VkDescriptorSet descriptorSet;
			if (vkAllocateDescriptorSets(device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
				SPLog("Warning: Failed to allocate temporal AA descriptor set");
				return;
			}

			// Update descriptor set
			VkDescriptorImageInfo inputImageInfo{};
			inputImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			inputImageInfo.imageView = input->GetImageView();
			inputImageInfo.sampler = input->GetSampler();

			VkDescriptorImageInfo previousImageInfo{};
			previousImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			previousImageInfo.imageView = historyBuffer.image->GetImageView();
			previousImageInfo.sampler = historyBuffer.image->GetSampler();

			VkDescriptorImageInfo processedInputImageInfo{};
			processedInputImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			processedInputImageInfo.imageView = input->GetImageView();
			processedInputImageInfo.sampler = input->GetSampler();

			VkDescriptorImageInfo depthImageInfo{};
			depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			depthImageInfo.imageView = depthImage->GetImageView();
			depthImageInfo.sampler = depthImage->GetSampler();

			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = uniformBuffer->GetBuffer();
			bufferInfo.offset = 0;
			bufferInfo.range = sizeof(uniforms);

			VkWriteDescriptorSet descriptorWrites[5] = {};
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
			descriptorWrites[1].pImageInfo = &previousImageInfo;

			descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[2].dstSet = descriptorSet;
			descriptorWrites[2].dstBinding = 2;
			descriptorWrites[2].dstArrayElement = 0;
			descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrites[2].descriptorCount = 1;
			descriptorWrites[2].pImageInfo = &processedInputImageInfo;

			descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[3].dstSet = descriptorSet;
			descriptorWrites[3].dstBinding = 3;
			descriptorWrites[3].dstArrayElement = 0;
			descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrites[3].descriptorCount = 1;
			descriptorWrites[3].pImageInfo = &depthImageInfo;

			descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[4].dstSet = descriptorSet;
			descriptorWrites[4].dstBinding = 4;
			descriptorWrites[4].dstArrayElement = 0;
			descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descriptorWrites[4].descriptorCount = 1;
			descriptorWrites[4].pBufferInfo = &bufferInfo;

			vkUpdateDescriptorSets(device->GetDevice(), 5, descriptorWrites, 0, nullptr);

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
				SPRaise("Failed to create temporal AA filter framebuffer");
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

			// Copy output to history buffer using image copy
			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = historyBuffer.image->GetImage();
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			vkCmdPipelineBarrier(commandBuffer,
			                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                    VK_PIPELINE_STAGE_TRANSFER_BIT,
			                    0, 0, nullptr, 0, nullptr, 1, &barrier);

			VkImageMemoryBarrier srcBarrier{};
			srcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			srcBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			srcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			srcBarrier.image = output->GetImage();
			srcBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			srcBarrier.subresourceRange.baseMipLevel = 0;
			srcBarrier.subresourceRange.levelCount = 1;
			srcBarrier.subresourceRange.baseArrayLayer = 0;
			srcBarrier.subresourceRange.layerCount = 1;
			srcBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			srcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
			                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			                    VK_PIPELINE_STAGE_TRANSFER_BIT,
			                    0, 0, nullptr, 0, nullptr, 1, &srcBarrier);

			VkImageCopy copyRegion{};
			copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copyRegion.srcSubresource.mipLevel = 0;
			copyRegion.srcSubresource.baseArrayLayer = 0;
			copyRegion.srcSubresource.layerCount = 1;
			copyRegion.srcOffset = {0, 0, 0};
			copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copyRegion.dstSubresource.mipLevel = 0;
			copyRegion.dstSubresource.baseArrayLayer = 0;
			copyRegion.dstSubresource.layerCount = 1;
			copyRegion.dstOffset = {0, 0, 0};
			copyRegion.extent = {output->GetWidth(), output->GetHeight(), 1};

			vkCmdCopyImage(commandBuffer,
			              output->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			              historyBuffer.image->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			              1, &copyRegion);

			// Transition images back to shader read optimal
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
			                    VK_PIPELINE_STAGE_TRANSFER_BIT,
			                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                    0, 0, nullptr, 0, nullptr, 1, &barrier);

			srcBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			srcBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			srcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			srcBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
			                    VK_PIPELINE_STAGE_TRANSFER_BIT,
			                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                    0, 0, nullptr, 0, nullptr, 1, &srcBarrier);

			// Cleanup
			vkFreeDescriptorSets(device->GetDevice(), descriptorPool, 1, &descriptorSet);
		}
	}
}
