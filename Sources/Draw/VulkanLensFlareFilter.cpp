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

#include "VulkanLensFlareFilter.h"
#include "VulkanRenderer.h"
#include "VulkanImage.h"
#include "VulkanBuffer.h"
#include "VulkanProgram.h"
#include "VulkanFramebufferManager.h"
#include "VulkanRenderPassUtils.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <cstring>
#include <cmath>

namespace spades {
	namespace draw {

		struct ScannerUniforms {
			float scanRange[4];
			float drawRange[4];
			float scanZ;
			float radius;
			float _pad[2];
		};

		struct DrawUniforms {
			float drawRange[4];
			float color[3];
			float _pad;
		};

		VulkanLensFlareFilter::VulkanLensFlareFilter(VulkanRenderer& r)
			: VulkanPostProcessFilter(r),
			  blurPipeline(VK_NULL_HANDLE),
			  scannerPipeline(VK_NULL_HANDLE),
			  drawPipeline(VK_NULL_HANDLE),
			  blurLayout(VK_NULL_HANDLE),
			  scannerLayout(VK_NULL_HANDLE),
			  drawLayout(VK_NULL_HANDLE),
			  blurDescLayout(VK_NULL_HANDLE),
			  scannerDescLayout(VK_NULL_HANDLE),
			  drawDescLayout(VK_NULL_HANDLE),
			  scannerRenderPass(VK_NULL_HANDLE),
			  drawRenderPass(VK_NULL_HANDLE),
			  descriptorPool(VK_NULL_HANDLE),
			  visibilityFramebuffer(VK_NULL_HANDLE) {
			CreateQuadBuffers();
			CreateDescriptorPool();
			CreateRenderPass();
			CreatePipeline();
			CreateVisibilityBuffer();
			LoadFlareTextures();
		}

		VulkanLensFlareFilter::~VulkanLensFlareFilter() {
			vkDeviceWaitIdle(device->GetDevice());

			DestroyVisibilityBuffer();

			if (blurPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), blurPipeline, nullptr);
			}
			if (scannerPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), scannerPipeline, nullptr);
			}
			if (drawPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), drawPipeline, nullptr);
			}
			if (blurLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), blurLayout, nullptr);
			}
			if (scannerLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), scannerLayout, nullptr);
			}
			if (drawLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), drawLayout, nullptr);
			}
			if (blurDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), blurDescLayout, nullptr);
			}
			if (scannerDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), scannerDescLayout, nullptr);
			}
			if (drawDescLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), drawDescLayout, nullptr);
			}
			if (scannerRenderPass != VK_NULL_HANDLE) {
				vkDestroyRenderPass(device->GetDevice(), scannerRenderPass, nullptr);
			}
			if (drawRenderPass != VK_NULL_HANDLE) {
				vkDestroyRenderPass(device->GetDevice(), drawRenderPass, nullptr);
			}
			if (descriptorPool != VK_NULL_HANDLE) {
				vkDestroyDescriptorPool(device->GetDevice(), descriptorPool, nullptr);
			}

			DestroyResources();
		}

		void VulkanLensFlareFilter::CreateQuadBuffers() {
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

			scannerUniformBuffer = new VulkanBuffer(device, sizeof(ScannerUniforms),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			drawUniformBuffer = new VulkanBuffer(device, sizeof(DrawUniforms),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		}

		void VulkanLensFlareFilter::CreateDescriptorPool() {
			VkDescriptorPoolSize poolSizes[] = {
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 20 },
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 60 }
			};

			VkDescriptorPoolCreateInfo poolInfo = {};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 2;
			poolInfo.pPoolSizes = poolSizes;
			poolInfo.maxSets = 40;
			poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

			if (vkCreateDescriptorPool(device->GetDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
				SPRaise("Failed to create lens flare descriptor pool");
			}
		}

		void VulkanLensFlareFilter::CreateVisibilityBuffer() {
			visibilityBuffer = new VulkanImage(device, 64, 64, VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			VkImageView attachments[] = { visibilityBuffer->GetImageView() };
			VkFramebufferCreateInfo framebufferInfo = {};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = scannerRenderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = attachments;
			framebufferInfo.width = 64;
			framebufferInfo.height = 64;
			framebufferInfo.layers = 1;

			if (vkCreateFramebuffer(device->GetDevice(), &framebufferInfo, nullptr, &visibilityFramebuffer) != VK_SUCCESS) {
				SPRaise("Failed to create visibility framebuffer");
			}
		}

		void VulkanLensFlareFilter::DestroyVisibilityBuffer() {
			if (visibilityFramebuffer != VK_NULL_HANDLE) {
				vkDestroyFramebuffer(device->GetDevice(), visibilityFramebuffer, nullptr);
				visibilityFramebuffer = VK_NULL_HANDLE;
			}
		}

		void VulkanLensFlareFilter::CreateRenderPass() {
			// Scanner render pass
			scannerRenderPass = CreateSimpleColorRenderPass(
				device->GetDevice(),
				VK_FORMAT_R8G8B8A8_UNORM,
				VK_ATTACHMENT_LOAD_OP_CLEAR
			);

			// Draw render pass (additive blending onto existing framebuffer)
			drawRenderPass = CreateSimpleColorRenderPass(
				device->GetDevice(),
				VK_FORMAT_R8G8B8A8_UNORM,
				VK_ATTACHMENT_LOAD_OP_LOAD,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			);

			// Base class render pass (for blur)
			renderPass = CreateSimpleColorRenderPass(
				device->GetDevice(),
				VK_FORMAT_R8G8B8A8_UNORM
			);
		}

		void VulkanLensFlareFilter::CreatePipeline() {
			// Load programs
			blurProgram = renderer.RegisterProgram("Shaders/PostFilters/Gauss1D.vk.program");
			scannerProgram = renderer.RegisterProgram("Shaders/LensFlare/Scanner.vk.program");
			drawProgram = renderer.RegisterProgram("Shaders/LensFlare/Draw.vk.program");

			// Common vertex input
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

			VkPipelineDepthStencilStateCreateInfo depthStencil = {};
			depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencil.depthTestEnable = VK_FALSE;
			depthStencil.depthWriteEnable = VK_FALSE;
			depthStencil.stencilTestEnable = VK_FALSE;

			VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dynamicState = {};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = 2;
			dynamicState.pDynamicStates = dynamicStates;

			// --- Scanner pipeline (no blending) ---
			scannerDescLayout = scannerProgram->GetDescriptorSetLayout();

			VkPipelineLayoutCreateInfo scannerLayoutInfo = {};
			scannerLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			scannerLayoutInfo.setLayoutCount = 1;
			scannerLayoutInfo.pSetLayouts = &scannerDescLayout;

			if (vkCreatePipelineLayout(device->GetDevice(), &scannerLayoutInfo, nullptr, &scannerLayout) != VK_SUCCESS) {
				SPRaise("Failed to create scanner pipeline layout");
			}

			VkPipelineColorBlendAttachmentState noBlendAttachment = {};
			noBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
				VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			noBlendAttachment.blendEnable = VK_FALSE;

			VkPipelineColorBlendStateCreateInfo noBlending = {};
			noBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			noBlending.logicOpEnable = VK_FALSE;
			noBlending.attachmentCount = 1;
			noBlending.pAttachments = &noBlendAttachment;

			auto scannerStages = scannerProgram->GetShaderStages();

			VkGraphicsPipelineCreateInfo pipelineInfo = {};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = static_cast<uint32_t>(scannerStages.size());
			pipelineInfo.pStages = scannerStages.data();
			pipelineInfo.pVertexInputState = &vertexInputInfo;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterizer;
			pipelineInfo.pMultisampleState = &multisampling;
			pipelineInfo.pDepthStencilState = &depthStencil;
			pipelineInfo.pColorBlendState = &noBlending;
			pipelineInfo.pDynamicState = &dynamicState;
			pipelineInfo.layout = scannerLayout;
			pipelineInfo.renderPass = scannerRenderPass;
			pipelineInfo.subpass = 0;

			if (vkCreateGraphicsPipelines(device->GetDevice(), renderer.GetPipelineCache(), 1, &pipelineInfo, nullptr, &scannerPipeline) != VK_SUCCESS) {
				SPRaise("Failed to create scanner pipeline");
			}

			// --- Draw pipeline (additive blending) ---
			drawDescLayout = drawProgram->GetDescriptorSetLayout();

			VkPipelineLayoutCreateInfo drawLayoutInfo = {};
			drawLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			drawLayoutInfo.setLayoutCount = 1;
			drawLayoutInfo.pSetLayouts = &drawDescLayout;

			if (vkCreatePipelineLayout(device->GetDevice(), &drawLayoutInfo, nullptr, &drawLayout) != VK_SUCCESS) {
				SPRaise("Failed to create draw pipeline layout");
			}

			VkPipelineColorBlendAttachmentState additiveBlendAttachment = {};
			additiveBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
				VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			additiveBlendAttachment.blendEnable = VK_TRUE;
			additiveBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
			additiveBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
			additiveBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
			additiveBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			additiveBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			additiveBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

			VkPipelineColorBlendStateCreateInfo additiveBlending = {};
			additiveBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			additiveBlending.logicOpEnable = VK_FALSE;
			additiveBlending.attachmentCount = 1;
			additiveBlending.pAttachments = &additiveBlendAttachment;

			auto drawStages = drawProgram->GetShaderStages();

			pipelineInfo.stageCount = static_cast<uint32_t>(drawStages.size());
			pipelineInfo.pStages = drawStages.data();
			pipelineInfo.pColorBlendState = &additiveBlending;
			pipelineInfo.layout = drawLayout;
			pipelineInfo.renderPass = drawRenderPass;

			if (vkCreateGraphicsPipelines(device->GetDevice(), renderer.GetPipelineCache(), 1, &pipelineInfo, nullptr, &drawPipeline) != VK_SUCCESS) {
				SPRaise("Failed to create draw pipeline");
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
			pipelineInfo.pColorBlendState = &noBlending;
			pipelineInfo.layout = blurLayout;
			pipelineInfo.renderPass = renderPass;

			if (vkCreateGraphicsPipelines(device->GetDevice(), renderer.GetPipelineCache(), 1, &pipelineInfo, nullptr, &blurPipeline) != VK_SUCCESS) {
				SPRaise("Failed to create blur pipeline");
			}

			SPLog("VulkanLensFlareFilter pipelines created");
		}

		void VulkanLensFlareFilter::LoadFlareTextures() {
			flare1 = renderer.RegisterImage("Gfx/LensFlare/1.png").Cast<VulkanImage>();
			flare2 = renderer.RegisterImage("Gfx/LensFlare/2.png").Cast<VulkanImage>();
			flare3 = renderer.RegisterImage("Gfx/LensFlare/3.png").Cast<VulkanImage>();
			flare4 = renderer.RegisterImage("Gfx/LensFlare/4.jpg").Cast<VulkanImage>();
			mask1 = renderer.RegisterImage("Gfx/LensFlare/mask1.png").Cast<VulkanImage>();
			mask2 = renderer.RegisterImage("Gfx/LensFlare/mask2.png").Cast<VulkanImage>();
			mask3 = renderer.RegisterImage("Gfx/LensFlare/mask3.png").Cast<VulkanImage>();
			white = renderer.RegisterImage("Gfx/White.tga").Cast<VulkanImage>();

			SPLog("VulkanLensFlareFilter textures loaded");
		}

		void VulkanLensFlareFilter::Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output) {
			Draw(commandBuffer);
		}

		void VulkanLensFlareFilter::Draw(VkCommandBuffer commandBuffer) {
			auto sunCol = MakeVector3(1.0f, 0.9f, 0.8f);
			auto sunDir = MakeVector3(0.0f, -1.0f, -1.0f);
			Draw(commandBuffer, sunDir, true, sunCol, true);
		}

		void VulkanLensFlareFilter::Draw(VkCommandBuffer commandBuffer, Vector3 direction, bool reflections,
		                                 Vector3 sunColor, bool infinityDistance) {
			SPADES_MARK_FUNCTION();

			client::SceneDefinition def = renderer.GetSceneDef();

			// Transform sun into view space
			Vector3 sunWorld = direction;
			Vector3 sunView = {
				Vector3::Dot(sunWorld, def.viewAxis[0]),
				Vector3::Dot(sunWorld, def.viewAxis[1]),
				Vector3::Dot(sunWorld, def.viewAxis[2])
			};

			if (sunView.z <= 0.0f)
				return;

			Vector2 fov = {tanf(def.fovX * 0.5f), tanf(def.fovY * 0.5f)};
			Vector2 sunScreen;
			sunScreen.x = sunView.x / (sunView.z * fov.x);
			sunScreen.y = sunView.y / (sunView.z * fov.y);

			const float sunRadiusTan = tanf(0.53f * 0.5f * static_cast<float>(M_PI) / 180.0f);
			Vector2 sunSize = {sunRadiusTan / fov.x, sunRadiusTan / fov.y};

			VkBuffer vertexBuffers[] = { quadVertexBuffer->GetBuffer() };
			VkDeviceSize offsets[] = { 0 };

			// Step 1: Occlusion test
			{
				ScannerUniforms uniforms;
				Vector2 sunTexPos = sunScreen * 0.5f + 0.5f;
				Vector2 sunTexSize = sunSize * 0.5f;
				// Flip Y for depth texture which was rendered with flipped viewport
				float flippedTexPosY = 1.0f - sunTexPos.y;
				uniforms.scanRange[0] = sunTexPos.x - sunTexSize.x;
				uniforms.scanRange[1] = flippedTexPosY - sunTexSize.y;
				uniforms.scanRange[2] = sunTexPos.x + sunTexSize.x;
				uniforms.scanRange[3] = flippedTexPosY + sunTexSize.y;
				uniforms.drawRange[0] = -0.5f;
				uniforms.drawRange[1] = -0.5f;
				uniforms.drawRange[2] = 0.5f;
				uniforms.drawRange[3] = 0.5f;
				uniforms.radius = 32.0f;

				if (infinityDistance) {
					uniforms.scanZ = 0.9999999f;
				} else {
					float far = def.zFar;
					float near = def.zNear;
					float depth = sunView.z;
					uniforms.scanZ = far * (near - depth) / (depth * (near - far));
				}

				void* data = scannerUniformBuffer->Map();
				memcpy(data, &uniforms, sizeof(uniforms));
				scannerUniformBuffer->Unmap();

				// Allocate descriptor set
				VkDescriptorSetAllocateInfo allocInfo = {};
				allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				allocInfo.descriptorPool = descriptorPool;
				allocInfo.descriptorSetCount = 1;
				allocInfo.pSetLayouts = &scannerDescLayout;

				VkDescriptorSet descriptorSet;
				if (vkAllocateDescriptorSets(device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
					SPRaise("Failed to allocate scanner descriptor set");
				}

				// Get depth texture from renderer
				VulkanImage* depthImage = renderer.GetDepthImageWrapper();

				VkDescriptorBufferInfo bufferInfo = {};
				bufferInfo.buffer = scannerUniformBuffer->GetBuffer();
				bufferInfo.offset = 0;
				bufferInfo.range = sizeof(ScannerUniforms);

				VkDescriptorImageInfo imageInfo = {};
				imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				imageInfo.imageView = depthImage->GetImageView();
				imageInfo.sampler = depthImage->GetSampler();

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

				VkClearValue clearColor = {};
				clearColor.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

				VkRenderPassBeginInfo renderPassInfo = {};
				renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				renderPassInfo.renderPass = scannerRenderPass;
				renderPassInfo.framebuffer = visibilityFramebuffer;
				renderPassInfo.renderArea.offset = { 0, 0 };
				renderPassInfo.renderArea.extent = { 64, 64 };
				renderPassInfo.clearValueCount = 1;
				renderPassInfo.pClearValues = &clearColor;

				vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, scannerPipeline);

				VkViewport viewport = {};
				viewport.x = 0.0f;
				viewport.y = 0.0f;
				viewport.width = 64.0f;
				viewport.height = 64.0f;
				viewport.minDepth = 0.0f;
				viewport.maxDepth = 1.0f;
				vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

				VkRect2D scissor = {};
				scissor.offset = { 0, 0 };
				scissor.extent = { 64, 64 };
				vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
					scannerLayout, 0, 1, &descriptorSet, 0, nullptr);

				vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
				vkCmdBindIndexBuffer(commandBuffer, quadIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);
				vkCmdDrawIndexed(commandBuffer, 6, 1, 0, 0, 0);

				vkCmdEndRenderPass(commandBuffer);

				vkFreeDescriptorSets(device->GetDevice(), descriptorPool, 1, &descriptorSet);
			}

			// Step 2: Blur visibility buffer (simplified - just use as-is for now)
			// The GL version does 3 blur passes, but we'll skip this for initial implementation

			// Lens flare size doesn't follow sun size
			sunSize = MakeVector2(0.01f, 0.01f);
			sunSize.x *= renderer.ScreenHeight() / renderer.ScreenWidth();

			float aroundness = sunScreen.GetSquaredLength() * 0.6f;
			float aroundness2 = std::min(sunScreen.GetSquaredLength() * 3.2f, 1.0f);

			// Step 3: Draw lens flares
			auto drawFlare = [&](VulkanImage* flareTexture, VulkanImage* maskTexture,
			                     Vector3 color, Vector4 range) {
				DrawUniforms uniforms;
				uniforms.drawRange[0] = range.x;
				uniforms.drawRange[1] = range.y;
				uniforms.drawRange[2] = range.z;
				uniforms.drawRange[3] = range.w;
				uniforms.color[0] = color.x;
				uniforms.color[1] = color.y;
				uniforms.color[2] = color.z;

				void* data = drawUniformBuffer->Map();
				memcpy(data, &uniforms, sizeof(uniforms));
				drawUniformBuffer->Unmap();

				VkDescriptorSetAllocateInfo allocInfo = {};
				allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				allocInfo.descriptorPool = descriptorPool;
				allocInfo.descriptorSetCount = 1;
				allocInfo.pSetLayouts = &drawDescLayout;

				VkDescriptorSet descriptorSet;
				if (vkAllocateDescriptorSets(device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
					return;
				}

				VkDescriptorBufferInfo bufferInfo = {};
				bufferInfo.buffer = drawUniformBuffer->GetBuffer();
				bufferInfo.offset = 0;
				bufferInfo.range = sizeof(DrawUniforms);

				VkDescriptorImageInfo visibilityInfo = {};
				visibilityInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				visibilityInfo.imageView = visibilityBuffer->GetImageView();
				visibilityInfo.sampler = visibilityBuffer->GetSampler();

				VkDescriptorImageInfo maskInfo = {};
				maskInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				maskInfo.imageView = maskTexture->GetImageView();
				maskInfo.sampler = maskTexture->GetSampler();

				VkDescriptorImageInfo flareInfo = {};
				flareInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				flareInfo.imageView = flareTexture->GetImageView();
				flareInfo.sampler = flareTexture->GetSampler();

				VkWriteDescriptorSet descriptorWrites[4] = {};
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
				descriptorWrites[1].pImageInfo = &visibilityInfo;

				descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				descriptorWrites[2].dstSet = descriptorSet;
				descriptorWrites[2].dstBinding = 2;
				descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				descriptorWrites[2].descriptorCount = 1;
				descriptorWrites[2].pImageInfo = &maskInfo;

				descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				descriptorWrites[3].dstSet = descriptorSet;
				descriptorWrites[3].dstBinding = 3;
				descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				descriptorWrites[3].descriptorCount = 1;
				descriptorWrites[3].pImageInfo = &flareInfo;

				vkUpdateDescriptorSets(device->GetDevice(), 4, descriptorWrites, 0, nullptr);

				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
					drawLayout, 0, 1, &descriptorSet, 0, nullptr);

				vkCmdDrawIndexed(commandBuffer, 6, 1, 0, 0, 0);

				vkFreeDescriptorSets(device->GetDevice(), descriptorPool, 1, &descriptorSet);
			};

			// Get output framebuffer from renderer
			VulkanImage* outputImage = renderer.GetFramebufferManager()->GetColorImage().GetPointerOrNull();
			int renderWidth = static_cast<int>(renderer.ScreenWidth());
			int renderHeight = static_cast<int>(renderer.ScreenHeight());

			VkFramebuffer outputFramebuffer;
			VkImageView outputAttachments[] = { outputImage->GetImageView() };
			VkFramebufferCreateInfo fbInfo = {};
			fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.renderPass = drawRenderPass;
			fbInfo.attachmentCount = 1;
			fbInfo.pAttachments = outputAttachments;
			fbInfo.width = renderWidth;
			fbInfo.height = renderHeight;
			fbInfo.layers = 1;

			if (vkCreateFramebuffer(device->GetDevice(), &fbInfo, nullptr, &outputFramebuffer) != VK_SUCCESS) {
				SPLog("Failed to create output framebuffer for lens flare");
				return;
			}

			VkRenderPassBeginInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = drawRenderPass;
			renderPassInfo.framebuffer = outputFramebuffer;
			renderPassInfo.renderArea.offset = { 0, 0 };
			renderPassInfo.renderArea.extent = { (uint32_t)renderWidth, (uint32_t)renderHeight };

			vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, drawPipeline);

			// Use flipped viewport to match main scene rendering (Y-up like OpenGL)
			VkViewport viewport = {};
			viewport.x = 0.0f;
			viewport.y = (float)renderHeight;
			viewport.width = (float)renderWidth;
			viewport.height = -(float)renderHeight;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

			VkRect2D scissor = {};
			scissor.offset = { 0, 0 };
			scissor.extent = { (uint32_t)renderWidth, (uint32_t)renderHeight };
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
			vkCmdBindIndexBuffer(commandBuffer, quadIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

			// Draw main flare
			drawFlare(flare4.GetPointerOrNull(), white.GetPointerOrNull(),
				sunColor * 0.04f,
				MakeVector4(sunScreen.x - sunSize.x * 256.0f, sunScreen.y - sunSize.y * 256.0f,
				            sunScreen.x + sunSize.x * 256.0f, sunScreen.y + sunSize.y * 256.0f));

			drawFlare(white.GetPointerOrNull(), white.GetPointerOrNull(),
				sunColor * 0.3f,
				MakeVector4(sunScreen.x - sunSize.x * 64.0f, sunScreen.y - sunSize.y * 64.0f,
				            sunScreen.x + sunSize.x * 64.0f, sunScreen.y + sunSize.y * 64.0f));

			drawFlare(white.GetPointerOrNull(), white.GetPointerOrNull(),
				sunColor * 0.5f,
				MakeVector4(sunScreen.x - sunSize.x * 32.0f, sunScreen.y - sunSize.y * 32.0f,
				            sunScreen.x + sunSize.x * 32.0f, sunScreen.y + sunSize.y * 32.0f));

			drawFlare(white.GetPointerOrNull(), white.GetPointerOrNull(),
				sunColor * 0.8f,
				MakeVector4(sunScreen.x - sunSize.x * 16.0f, sunScreen.y - sunSize.y * 16.0f,
				            sunScreen.x + sunSize.x * 16.0f, sunScreen.y + sunSize.y * 16.0f));

			drawFlare(white.GetPointerOrNull(), white.GetPointerOrNull(),
				sunColor * 1.0f,
				MakeVector4(sunScreen.x - sunSize.x * 4.0f, sunScreen.y - sunSize.y * 4.0f,
				            sunScreen.x + sunSize.x * 4.0f, sunScreen.y + sunSize.y * 4.0f));

			// Horizontal streak
			drawFlare(white.GetPointerOrNull(), white.GetPointerOrNull(),
				sunColor * MakeVector3(0.1f, 0.05f, 0.1f),
				MakeVector4(sunScreen.x - sunSize.x * 256.0f, sunScreen.y - sunSize.y * 8.0f,
				            sunScreen.x + sunSize.x * 256.0f, sunScreen.y + sunSize.y * 8.0f));

			// Dust
			drawFlare(white.GetPointerOrNull(), mask3.GetPointerOrNull(),
				sunColor * aroundness * 0.4f,
				MakeVector4(sunScreen.x - sunSize.x * 188.0f, sunScreen.y - sunSize.y * 188.0f,
				            sunScreen.x + sunSize.x * 188.0f, sunScreen.y + sunSize.y * 188.0f));

			if (reflections) {
				// Reflection flares
				drawFlare(flare2.GetPointerOrNull(), white.GetPointerOrNull(),
					sunColor,
					MakeVector4(-(sunScreen.x - sunSize.x * 18.0f) * 0.4f,
					            -(sunScreen.y - sunSize.y * 18.0f) * 0.4f,
					            -(sunScreen.x + sunSize.x * 18.0f) * 0.4f,
					            -(sunScreen.y + sunSize.y * 18.0f) * 0.4f));

				drawFlare(flare2.GetPointerOrNull(), white.GetPointerOrNull(),
					sunColor * 0.3f,
					MakeVector4(-(sunScreen.x - sunSize.x * 6.0f) * 0.39f,
					            -(sunScreen.y - sunSize.y * 6.0f) * 0.39f,
					            -(sunScreen.x + sunSize.x * 6.0f) * 0.39f,
					            -(sunScreen.y + sunSize.y * 6.0f) * 0.39f));

				drawFlare(flare2.GetPointerOrNull(), white.GetPointerOrNull(),
					sunColor,
					MakeVector4(-(sunScreen.x - sunSize.x * 6.0f) * 0.3f,
					            -(sunScreen.y - sunSize.y * 6.0f) * 0.3f,
					            -(sunScreen.x + sunSize.x * 6.0f) * 0.3f,
					            -(sunScreen.y + sunSize.y * 6.0f) * 0.3f));

				drawFlare(flare2.GetPointerOrNull(), white.GetPointerOrNull(),
					sunColor * 0.3f,
					MakeVector4((sunScreen.x - sunSize.x * 12.0f) * 0.6f,
					            (sunScreen.y - sunSize.y * 12.0f) * 0.6f,
					            (sunScreen.x + sunSize.x * 12.0f) * 0.6f,
					            (sunScreen.y + sunSize.y * 12.0f) * 0.6f));

				drawFlare(flare1.GetPointerOrNull(), mask2.GetPointerOrNull(),
					MakeVector3(sunColor.x * 0.5f, sunColor.y * 0.4f, sunColor.z * 0.3f),
					MakeVector4((sunScreen.x - sunSize.x * 96.0f) * 2.3f,
					            (sunScreen.y - sunSize.y * 96.0f) * 2.3f,
					            (sunScreen.x + sunSize.x * 96.0f) * 2.3f,
					            (sunScreen.y + sunSize.y * 96.0f) * 2.3f));

				drawFlare(flare1.GetPointerOrNull(), mask2.GetPointerOrNull(),
					MakeVector3(sunColor.x * 0.3f, sunColor.y * 0.2f, sunColor.z * 0.1f),
					MakeVector4((sunScreen.x - sunSize.x * 128.0f) * 0.8f,
					            (sunScreen.y - sunSize.y * 128.0f) * 0.8f,
					            (sunScreen.x + sunSize.x * 128.0f) * 0.8f,
					            (sunScreen.y + sunSize.y * 128.0f) * 0.8f));

				drawFlare(flare3.GetPointerOrNull(), mask2.GetPointerOrNull(),
					sunColor * 0.3f,
					MakeVector4((sunScreen.x - sunSize.x * 18.0f) * 0.5f,
					            (sunScreen.y - sunSize.y * 18.0f) * 0.5f,
					            (sunScreen.x + sunSize.x * 18.0f) * 0.5f,
					            (sunScreen.y + sunSize.y * 18.0f) * 0.5f));

				float reflSize = 50.0f + aroundness2 * 60.0f;
				drawFlare(flare3.GetPointerOrNull(), mask1.GetPointerOrNull(),
					MakeVector3(sunColor.x * 0.8f * aroundness2,
					            sunColor.y * 0.5f * aroundness2,
					            sunColor.z * 0.3f * aroundness2),
					MakeVector4((sunScreen.x - sunSize.x * reflSize) * -2.0f,
					            (sunScreen.y - sunSize.y * reflSize) * -2.0f,
					            (sunScreen.x + sunSize.x * reflSize) * -2.0f,
					            (sunScreen.y + sunSize.y * reflSize) * -2.0f));
			}

			vkCmdEndRenderPass(commandBuffer);

			vkDestroyFramebuffer(device->GetDevice(), outputFramebuffer, nullptr);
		}

		Handle<VulkanImage> VulkanLensFlareFilter::Blur(VkCommandBuffer commandBuffer, VulkanImage* buffer, float spread) {
			// Simplified blur - return input as-is for now
			return Handle<VulkanImage>(buffer, false);
		}
	}
}
