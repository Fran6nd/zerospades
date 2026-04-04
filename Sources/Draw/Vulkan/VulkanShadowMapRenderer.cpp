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
#include "VulkanMapRenderer.h"
#include "VulkanModelRenderer.h"
#include "VulkanImage.h"
#include "VulkanBuffer.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Settings.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Client/SceneDefinition.h>
#include <cstring>
#include <vector>

SPADES_SETTING(r_vk_shadowMapSize);

namespace spades {
	namespace draw {

		VulkanShadowMapRenderer::VulkanShadowMapRenderer(VulkanRenderer& r)
		: renderer(r),
		  device(r.GetDevice()),
		  renderPass(VK_NULL_HANDLE),
		  pipelineLayout(VK_NULL_HANDLE),
		  descriptorSetLayout(VK_NULL_HANDLE),
		  pipeline(VK_NULL_HANDLE),
		  descriptorPool(VK_NULL_HANDLE),
		  modelPipeline(VK_NULL_HANDLE),
		  modelPipelineLayout(VK_NULL_HANDLE),
		  cascadeDescriptorSetLayout(VK_NULL_HANDLE),
		  cascadeDescriptorPool(VK_NULL_HANDLE),
		  cascadeDescriptorSet(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			textureSize = (int)r_vk_shadowMapSize;
			if (textureSize < 128) textureSize = 128;
			if (textureSize > 4096) textureSize = 4096;

			SPLog("Creating shadow map renderer with size %dx%d", textureSize, textureSize);

			for (int i = 0; i < NumSlices; i++) {
				framebuffers[i] = VK_NULL_HANDLE;
				shadowMapImages[i] = nullptr;
				descriptorSets[i] = VK_NULL_HANDLE;
				uniformBuffers[i] = nullptr;
			}

			try {
				CreateRenderPass();
				CreateFramebuffers();
				CreatePipeline();
				CreateDescriptorSets();
				CreateCascadeDescriptor();
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

			VkSubpassDependency dependencies[2]{};
			// Wait for previous passes before writing depth
			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			dependencies[0].srcAccessMask = 0;
			dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			// Ensure depth write is visible to fragment shader reads in subsequent passes
			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			VkRenderPassCreateInfo renderPassInfo{};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = 1;
			renderPassInfo.pAttachments = &depthAttachment;
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = 2;
			renderPassInfo.pDependencies = dependencies;

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

				// Replace the default color-aspect view with a depth-aspect view
				shadowMapImages[i]->CreateImageView(VK_IMAGE_ASPECT_DEPTH_BIT);

				// Create a NEAREST sampler so the depth value can be read in shaders
				shadowMapImages[i]->CreateSampler(
					VK_FILTER_NEAREST, VK_FILTER_NEAREST,
					VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
					/*anisotropy=*/false
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

			std::vector<uint32_t> vertCode = LoadSPIRVFile("Shaders/Vulkan/ShadowMap.vert.spv");
			std::vector<uint32_t> fragCode = LoadSPIRVFile("Shaders/Vulkan/ShadowMap.frag.spv");

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

			// Create pipeline layout with push constants for model origin
			VkPushConstantRange pushConstantRange{};
			pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			pushConstantRange.offset = 0;
			pushConstantRange.size = sizeof(Vector3); // modelOrigin (12 bytes)

			VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
			pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutInfo.setLayoutCount = 1;
			pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
			pipelineLayoutInfo.pushConstantRangeCount = 1;
			pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

			if (vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
				vkDestroyShaderModule(vkDevice, fragShaderModule, nullptr);
				SPRaise("Failed to create pipeline layout for shadow map");
			}

			// Vertex input - must match VulkanMapChunk::Vertex format
			// The map chunk vertex buffer has stride 20 bytes, with uint8 x,y,z at offset 0
			VkVertexInputBindingDescription bindingDescription{};
			bindingDescription.binding = 0;
			bindingDescription.stride = 20; // Same as VulkanMapChunk::Vertex
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			VkVertexInputAttributeDescription attributeDescription{};
			attributeDescription.binding = 0;
			attributeDescription.location = 0;
			attributeDescription.format = VK_FORMAT_R8G8B8_UINT; // uint8 x, y, z
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

			VkResult result = vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1, &pipelineInfo, nullptr, &pipeline);

			// Clean up shader modules
			vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
			vkDestroyShaderModule(vkDevice, fragShaderModule, nullptr);

			if (result != VK_SUCCESS) {
				SPRaise("Failed to create shadow map graphics pipeline");
			}

			SPLog("Shadow map pipeline created successfully");

			// ---------------------------------------------------------------
			// Model shadow pipeline — same render pass, different vertex layout
			// Vertex: uvec3 at location 0, stride 12 (VulkanOptimizedVoxelModel::Vertex)
			// Push constants: mat4 modelMatrix (64) + vec3 modelOrigin (12) + float pad (4) = 80 bytes
			// ---------------------------------------------------------------
			{
				std::vector<uint32_t> mVertCode = LoadSPIRVFile("Shaders/Vulkan/ShadowMapModel.vert.spv");
				std::vector<uint32_t> mFragCode = LoadSPIRVFile("Shaders/Vulkan/ShadowMap.frag.spv");

				VkShaderModuleCreateInfo mVertInfo{};
				mVertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				mVertInfo.codeSize = mVertCode.size() * sizeof(uint32_t);
				mVertInfo.pCode = mVertCode.data();
				VkShaderModule mVertModule;
				if (vkCreateShaderModule(vkDevice, &mVertInfo, nullptr, &mVertModule) != VK_SUCCESS)
					SPRaise("Failed to create model shadow vertex shader module");

				VkShaderModuleCreateInfo mFragInfo{};
				mFragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				mFragInfo.codeSize = mFragCode.size() * sizeof(uint32_t);
				mFragInfo.pCode = mFragCode.data();
				VkShaderModule mFragModule;
				if (vkCreateShaderModule(vkDevice, &mFragInfo, nullptr, &mFragModule) != VK_SUCCESS) {
					vkDestroyShaderModule(vkDevice, mVertModule, nullptr);
					SPRaise("Failed to create model shadow fragment shader module");
				}

				VkPipelineShaderStageCreateInfo mStages[2]{};
				mStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
				mStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
				mStages[0].module = mVertModule;
				mStages[0].pName = "main";
				mStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
				mStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
				mStages[1].module = mFragModule;
				mStages[1].pName = "main";

				// Model vertex: uvec3 position at loc 0, stride 12
				VkVertexInputBindingDescription mBinding{};
				mBinding.binding = 0;
				mBinding.stride = 12; // sizeof(VulkanOptimizedVoxelModel::Vertex)
				mBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

				VkVertexInputAttributeDescription mAttr{};
				mAttr.binding = 0;
				mAttr.location = 0;
				mAttr.format = VK_FORMAT_R8G8B8_UINT;
				mAttr.offset = 0;

				VkPipelineVertexInputStateCreateInfo mVertexInput{};
				mVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
				mVertexInput.vertexBindingDescriptionCount = 1;
				mVertexInput.pVertexBindingDescriptions = &mBinding;
				mVertexInput.vertexAttributeDescriptionCount = 1;
				mVertexInput.pVertexAttributeDescriptions = &mAttr;

				// Push constants: mat4 modelMatrix (64) + vec3 modelOrigin (12) + float pad (4) = 80 bytes
				VkPushConstantRange mPush{};
				mPush.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
				mPush.offset = 0;
				mPush.size = 80;

				VkPipelineLayoutCreateInfo mLayoutInfo{};
				mLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				mLayoutInfo.setLayoutCount = 1;
				mLayoutInfo.pSetLayouts = &descriptorSetLayout; // reuse same UBO layout
				mLayoutInfo.pushConstantRangeCount = 1;
				mLayoutInfo.pPushConstantRanges = &mPush;

				if (vkCreatePipelineLayout(vkDevice, &mLayoutInfo, nullptr, &modelPipelineLayout) != VK_SUCCESS) {
					vkDestroyShaderModule(vkDevice, mVertModule, nullptr);
					vkDestroyShaderModule(vkDevice, mFragModule, nullptr);
					SPRaise("Failed to create model shadow pipeline layout");
				}

				VkGraphicsPipelineCreateInfo mPipeInfo{};
				mPipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
				mPipeInfo.stageCount = 2;
				mPipeInfo.pStages = mStages;
				mPipeInfo.pVertexInputState = &mVertexInput;
				mPipeInfo.pInputAssemblyState = &inputAssembly;
				mPipeInfo.pViewportState = &viewportState;
				mPipeInfo.pRasterizationState = &rasterizer;
				mPipeInfo.pMultisampleState = &multisampling;
				mPipeInfo.pDepthStencilState = &depthStencil;
				mPipeInfo.pColorBlendState = &colorBlending;
				mPipeInfo.pDynamicState = &dynamicState;
				mPipeInfo.layout = modelPipelineLayout;
				mPipeInfo.renderPass = renderPass;
				mPipeInfo.subpass = 0;
				mPipeInfo.basePipelineHandle = VK_NULL_HANDLE;

				result = vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1, &mPipeInfo, nullptr, &modelPipeline);
				vkDestroyShaderModule(vkDevice, mVertModule, nullptr);
				vkDestroyShaderModule(vkDevice, mFragModule, nullptr);
				if (result != VK_SUCCESS)
					SPRaise("Failed to create model shadow graphics pipeline");

				SPLog("Model shadow pipeline created successfully");
			}
		}

		void VulkanShadowMapRenderer::CreateDescriptorSets() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			// Create descriptor pool
			VkDescriptorPoolSize poolSize{};
			poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			poolSize.descriptorCount = NumSlices;

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes = &poolSize;
			poolInfo.maxSets = NumSlices;

			if (vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
				SPRaise("Failed to create shadow map descriptor pool");
			}

			// Create uniform buffers and descriptor sets for each cascade
			for (int i = 0; i < NumSlices; i++) {
				// Create uniform buffer
				uniformBuffers[i] = Handle<VulkanBuffer>::New(
					device,
					sizeof(Matrix4),
					VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
				);

				// Allocate descriptor set
				VkDescriptorSetAllocateInfo allocInfo{};
				allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				allocInfo.descriptorPool = descriptorPool;
				allocInfo.descriptorSetCount = 1;
				allocInfo.pSetLayouts = &descriptorSetLayout;

				if (vkAllocateDescriptorSets(vkDevice, &allocInfo, &descriptorSets[i]) != VK_SUCCESS) {
					SPRaise("Failed to allocate shadow map descriptor set %d", i);
				}

				// Update descriptor set
				VkDescriptorBufferInfo bufferInfo{};
				bufferInfo.buffer = uniformBuffers[i]->GetBuffer();
				bufferInfo.offset = 0;
				bufferInfo.range = sizeof(Matrix4);

				VkWriteDescriptorSet descriptorWrite{};
				descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				descriptorWrite.dstSet = descriptorSets[i];
				descriptorWrite.dstBinding = 0;
				descriptorWrite.dstArrayElement = 0;
				descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				descriptorWrite.descriptorCount = 1;
				descriptorWrite.pBufferInfo = &bufferInfo;

				vkUpdateDescriptorSets(vkDevice, 1, &descriptorWrite, 0, nullptr);
			}

			SPLog("Shadow map descriptor sets created successfully");
		}

		void VulkanShadowMapRenderer::CreateCascadeDescriptor() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			// Layout: binding 0 = UBO (3 × mat4 cascade matrices), bindings 1-3 = cascade depth samplers
			VkDescriptorSetLayoutBinding bindings[4]{};

			// Binding 0: UBO with all cascade matrices
			bindings[0].binding = 0;
			bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			bindings[0].descriptorCount = 1;
			bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

			// Bindings 1-3: one depth sampler per cascade
			for (int i = 0; i < NumSlices; i++) {
				bindings[1 + i].binding = 1 + i;
				bindings[1 + i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				bindings[1 + i].descriptorCount = 1;
				bindings[1 + i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			}

			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = 4;
			layoutInfo.pBindings = bindings;

			if (vkCreateDescriptorSetLayout(vkDevice, &layoutInfo, nullptr, &cascadeDescriptorSetLayout) != VK_SUCCESS)
				SPRaise("Failed to create cascade shadow descriptor set layout");

			// UBO: 3 × mat4 (192 bytes)
			struct CascadeUBO { Matrix4 matrices[NumSlices]; };
			cascadeMatrixBuffer = Handle<VulkanBuffer>::New(
				device,
				sizeof(CascadeUBO),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);

			// Descriptor pool: 1 UBO + NumSlices samplers
			VkDescriptorPoolSize poolSizes[2]{};
			poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			poolSizes[0].descriptorCount = 1;
			poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSizes[1].descriptorCount = NumSlices;

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 2;
			poolInfo.pPoolSizes = poolSizes;
			poolInfo.maxSets = 1;

			if (vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &cascadeDescriptorPool) != VK_SUCCESS)
				SPRaise("Failed to create cascade shadow descriptor pool");

			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = cascadeDescriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &cascadeDescriptorSetLayout;

			if (vkAllocateDescriptorSets(vkDevice, &allocInfo, &cascadeDescriptorSet) != VK_SUCCESS)
				SPRaise("Failed to allocate cascade shadow descriptor set");

			// Initialize UBO with "outside range" matrices so unrendered cascades return no-shadow
			{
				CascadeUBO initUBO{};
				for (int i = 0; i < NumSlices; i++) {
					// Matrix that maps any world pos to clip (2, 2, 0, 1) -> UV (1.5, 1.5) = outside [0,1]
					memset(initUBO.matrices[i].m, 0, sizeof(initUBO.matrices[i].m));
					initUBO.matrices[i].m[12] = 2.0f; // col3.x
					initUBO.matrices[i].m[13] = 2.0f; // col3.y
					initUBO.matrices[i].m[15] = 1.0f; // col3.w
				}
				void* p = cascadeMatrixBuffer->Map();
				memcpy(p, &initUBO, sizeof(initUBO));
				cascadeMatrixBuffer->Unmap();
			}

			// Create 1x1 placeholder depth image (depth=1.0 = fully lit, no shadow)
			nullDepthImage = Handle<VulkanImage>::New(
				device, 1, 1,
				VK_FORMAT_D32_SFLOAT,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			);
			nullDepthImage->CreateImageView(VK_IMAGE_ASPECT_DEPTH_BIT);
			nullDepthImage->CreateSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST,
				VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);

			// Initialize null depth image to depth=1.0 via one-shot command buffer
			{
				VkCommandBufferAllocateInfo cbAllocInfo{};
				cbAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
				cbAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
				cbAllocInfo.commandPool = device->GetCommandPool();
				cbAllocInfo.commandBufferCount = 1;

				VkCommandBuffer cb;
				vkAllocateCommandBuffers(vkDevice, &cbAllocInfo, &cb);

				VkCommandBufferBeginInfo beginInfo{};
				beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
				vkBeginCommandBuffer(cb, &beginInfo);

				// Transition UNDEFINED → TRANSFER_DST_OPTIMAL for clear
				VkImageMemoryBarrier barrier{};
				barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.image = nullDepthImage->GetImage();
				barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
				barrier.srcAccessMask = 0;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				vkCmdPipelineBarrier(cb,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &barrier);

				// Clear to depth=1.0
				VkClearDepthStencilValue clearVal{1.0f, 0};
				VkImageSubresourceRange range{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
				vkCmdClearDepthStencilImage(cb, nullDepthImage->GetImage(),
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearVal, 1, &range);

				// Transition TRANSFER_DST_OPTIMAL → DEPTH_STENCIL_READ_ONLY_OPTIMAL
				barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				vkCmdPipelineBarrier(cb,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &barrier);

				vkEndCommandBuffer(cb);

				VkSubmitInfo submitInfo{};
				submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				submitInfo.commandBufferCount = 1;
				submitInfo.pCommandBuffers = &cb;
				vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
				vkQueueWaitIdle(device->GetGraphicsQueue());
				vkFreeCommandBuffers(vkDevice, device->GetCommandPool(), 1, &cb);
			}

			// Write UBO binding and initialize sampler bindings with null depth image
			VkDescriptorBufferInfo bufInfo{};
			bufInfo.buffer = cascadeMatrixBuffer->GetBuffer();
			bufInfo.offset = 0;
			bufInfo.range = sizeof(CascadeUBO);

			VkWriteDescriptorSet writes[4]{};
			writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[0].dstSet = cascadeDescriptorSet;
			writes[0].dstBinding = 0;
			writes[0].dstArrayElement = 0;
			writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writes[0].descriptorCount = 1;
			writes[0].pBufferInfo = &bufInfo;

			VkDescriptorImageInfo nullImgInfos[NumSlices]{};
			for (int i = 0; i < NumSlices; i++) {
				nullImgInfos[i].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
				nullImgInfos[i].imageView = nullDepthImage->GetImageView();
				nullImgInfos[i].sampler = nullDepthImage->GetSampler();

				writes[1 + i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[1 + i].dstSet = cascadeDescriptorSet;
				writes[1 + i].dstBinding = 1 + i;
				writes[1 + i].dstArrayElement = 0;
				writes[1 + i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[1 + i].descriptorCount = 1;
				writes[1 + i].pImageInfo = &nullImgInfos[i];
			}
			vkUpdateDescriptorSets(vkDevice, 4, writes, 0, nullptr);

			SPLog("Cascade shadow descriptor created successfully");
		}

		void VulkanShadowMapRenderer::DestroyResources() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			if (cascadeDescriptorPool != VK_NULL_HANDLE) {
				vkDestroyDescriptorPool(vkDevice, cascadeDescriptorPool, nullptr);
				cascadeDescriptorPool = VK_NULL_HANDLE;
				cascadeDescriptorSet = VK_NULL_HANDLE;
			}
			cascadeMatrixBuffer = nullptr;
			nullDepthImage = nullptr;
			if (cascadeDescriptorSetLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(vkDevice, cascadeDescriptorSetLayout, nullptr);
				cascadeDescriptorSetLayout = VK_NULL_HANDLE;
			}

			if (modelPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, modelPipeline, nullptr);
				modelPipeline = VK_NULL_HANDLE;
			}
			if (modelPipelineLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, modelPipelineLayout, nullptr);
				modelPipelineLayout = VK_NULL_HANDLE;
			}

			if (descriptorPool != VK_NULL_HANDLE) {
				vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
				descriptorPool = VK_NULL_HANDLE;
			}

			for (int i = 0; i < NumSlices; i++) {
				uniformBuffers[i] = nullptr;
				descriptorSets[i] = VK_NULL_HANDLE;
			}

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

				// Update per-cascade UBO with this slice's light-space matrix
				void* data = uniformBuffers[i]->Map();
				memcpy(data, &matrix, sizeof(Matrix4));
				uniformBuffers[i]->Unmap();

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

				vkCmdSetDepthBias(commandBuffer, 1.25f, 0.0f, 1.75f);

				// --- Map chunks (stride 20 pipeline) ---
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
					0, 1, &descriptorSets[i], 0, nullptr);

				VulkanMapRenderer* mapRenderer = renderer.GetMapRenderer();
				if (mapRenderer) {
					mapRenderer->RenderShadowMapPass(commandBuffer, pipelineLayout);
				}

				// --- Model instances (stride 12 pipeline, per-instance push constants) ---
				if (modelPipeline != VK_NULL_HANDLE) {
					vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, modelPipeline);
					// Reuse the same per-slice UBO descriptor (same layout, same binding)
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, modelPipelineLayout,
						0, 1, &descriptorSets[i], 0, nullptr);

					VulkanModelRenderer* modelRenderer = renderer.GetModelRenderer();
					if (modelRenderer) {
						modelRenderer->RenderShadowMapPass(commandBuffer, *this);
					}
				}

				vkCmdEndRenderPass(commandBuffer);
			}

			// Update cascade descriptor with all 3 matrices and shadow map images.
			// This is done after all slices are rendered so the images are fully written.
			if (cascadeDescriptorSet != VK_NULL_HANDLE && cascadeMatrixBuffer) {
				struct CascadeUBO { Matrix4 m[NumSlices]; } ubo;
				for (int i = 0; i < NumSlices; i++) ubo.m[i] = matrices[i];
				void* p = cascadeMatrixBuffer->Map();
				memcpy(p, &ubo, sizeof(ubo));
				cascadeMatrixBuffer->Unmap();

				VkWriteDescriptorSet writes[NumSlices]{};
				VkDescriptorImageInfo imgInfos[NumSlices]{};
				for (int i = 0; i < NumSlices; i++) {
					imgInfos[i].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
					imgInfos[i].imageView = shadowMapImages[i]->GetImageView();
					imgInfos[i].sampler = shadowMapImages[i]->GetSampler();

					writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					writes[i].dstSet = cascadeDescriptorSet;
					writes[i].dstBinding = 1 + i;
					writes[i].dstArrayElement = 0;
					writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					writes[i].descriptorCount = 1;
					writes[i].pImageInfo = &imgInfos[i];
				}
				vkUpdateDescriptorSets(device->GetDevice(), NumSlices, writes, 0, nullptr);
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
