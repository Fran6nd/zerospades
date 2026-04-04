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

#include "VulkanColorCorrectionFilter.h"
#include "VulkanFramebufferManager.h"
#include "VulkanImage.h"
#include "VulkanRenderer.h"
#include "VulkanRenderPassUtils.h"
#include <Client/SceneDefinition.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/Math.h>
#include <Core/Settings.h>
#include <Gui/SDLVulkanDevice.h>
#include <algorithm>
#include <cmath>
#include <cstring>

SPADES_SETTING(r_vk_hdr);
SPADES_SETTING(r_vk_bloom);
SPADES_SETTING(r_vk_fogShadow);
SPADES_SETTING(r_vk_saturation);
SPADES_SETTING(r_vk_exposureValue);

namespace spades {
	namespace draw {

		// Push constants layout (24 bytes, fragment stage only).
		struct ColorCorrectionPushConstants {
			float tint[3];     // [0..11]  vec3 tint
			float saturation;  // [12..15] float saturation
			float enhancement; // [16..19] float enhancement
			float hdrEnabled;  // [20..23] float hdrEnabled
		};
		static_assert(sizeof(ColorCorrectionPushConstants) == 24, "");

		// ─────────────────────────────────────────────────────────────────────────
		//  Construction / destruction
		// ─────────────────────────────────────────────────────────────────────────

		VulkanColorCorrectionFilter::VulkanColorCorrectionFilter(VulkanRenderer& r)
		    : VulkanPostProcessFilter(r),
		      colorFormat(VK_FORMAT_UNDEFINED),
		      colorSampler(VK_NULL_HANDLE),
		      ppRenderPass(VK_NULL_HANDLE),
		      oneSamplerDSL(VK_NULL_HANDLE),
		      ccLayout(VK_NULL_HANDLE),
		      ccPipeline(VK_NULL_HANDLE),
		      smoothedFogColor(MakeVector3(0.5f, 0.5f, 0.5f)) {
			SPADES_MARK_FUNCTION();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i)
				perFrameDescPool[i] = VK_NULL_HANDLE;

			colorFormat = r.GetFramebufferManager()->GetMainColorFormat();

			InitRenderPass();
			InitDescriptorSetLayout();
			InitPipeline();
			InitDescriptorPools();
		}

		VulkanColorCorrectionFilter::~VulkanColorCorrectionFilter() {
			SPADES_MARK_FUNCTION();

			VkDevice dev = device->GetDevice();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				for (VkFramebuffer fb : perFrameFramebuffers[i])
					vkDestroyFramebuffer(dev, fb, nullptr);
				if (perFrameDescPool[i] != VK_NULL_HANDLE)
					vkDestroyDescriptorPool(dev, perFrameDescPool[i], nullptr);
			}

			if (ccPipeline    != VK_NULL_HANDLE) vkDestroyPipeline(dev, ccPipeline, nullptr);
			if (ccLayout      != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, ccLayout, nullptr);
			if (oneSamplerDSL != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, oneSamplerDSL, nullptr);
			if (colorSampler  != VK_NULL_HANDLE) vkDestroySampler(dev, colorSampler, nullptr);
			if (ppRenderPass  != VK_NULL_HANDLE) vkDestroyRenderPass(dev, ppRenderPass, nullptr);
		}

		// ─────────────────────────────────────────────────────────────────────────
		//  Initialisation
		// ─────────────────────────────────────────────────────────────────────────

		void VulkanColorCorrectionFilter::InitRenderPass() {
			VkDevice dev = device->GetDevice();

			VkSubpassDependency dep{};
			dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
			dep.dstSubpass    = 0;
			dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dep.dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			ppRenderPass = CreateSimpleColorRenderPass(
			    dev, colorFormat,
			    VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			    VK_IMAGE_LAYOUT_UNDEFINED,
			    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			    &dep);

			VkSamplerCreateInfo si{};
			si.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			si.magFilter               = VK_FILTER_LINEAR;
			si.minFilter               = VK_FILTER_LINEAR;
			si.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.anisotropyEnable        = VK_FALSE;
			si.maxAnisotropy           = 1.0f;
			si.borderColor             = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			si.unnormalizedCoordinates = VK_FALSE;
			si.compareEnable           = VK_FALSE;
			si.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;

			if (vkCreateSampler(dev, &si, nullptr, &colorSampler) != VK_SUCCESS)
				SPRaise("Failed to create colour correction sampler");
		}

		void VulkanColorCorrectionFilter::InitDescriptorSetLayout() {
			VkDevice dev = device->GetDevice();

			VkDescriptorSetLayoutBinding binding{};
			binding.binding         = 0;
			binding.descriptorCount = 1;
			binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

			VkDescriptorSetLayoutCreateInfo info{};
			info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			info.bindingCount = 1;
			info.pBindings    = &binding;

			if (vkCreateDescriptorSetLayout(dev, &info, nullptr, &oneSamplerDSL) != VK_SUCCESS)
				SPRaise("Failed to create colour correction descriptor set layout");
		}

		VkShaderModule VulkanColorCorrectionFilter::LoadSPIRV(const char* path) {
			std::string data = FileManager::ReadAllBytes(path);
			std::vector<uint32_t> code(data.size() / sizeof(uint32_t));
			std::memcpy(code.data(), data.data(), data.size());

			VkShaderModuleCreateInfo info{};
			info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			info.codeSize = data.size();
			info.pCode    = code.data();

			VkShaderModule mod;
			if (vkCreateShaderModule(device->GetDevice(), &info, nullptr, &mod) != VK_SUCCESS)
				SPRaise("Failed to create shader module: %s", path);
			return mod;
		}

		void VulkanColorCorrectionFilter::InitPipeline() {
			VkDevice      dev   = device->GetDevice();
			VkPipelineCache cache = renderer.GetPipelineCache();

			VkShaderModule vs = LoadSPIRV("Shaders/Vulkan/PostFilters/PassThrough.vk.vs.spv");
			VkShaderModule fs = LoadSPIRV("Shaders/Vulkan/PostFilters/ColorCorrection.vk.fs.spv");

			// Pipeline layout: one sampler binding + 24-byte push constants (fragment stage)
			VkPushConstantRange pcr{};
			pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			pcr.offset     = 0;
			pcr.size       = sizeof(ColorCorrectionPushConstants);

			VkPipelineLayoutCreateInfo li{};
			li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			li.setLayoutCount         = 1;
			li.pSetLayouts            = &oneSamplerDSL;
			li.pushConstantRangeCount = 1;
			li.pPushConstantRanges    = &pcr;
			if (vkCreatePipelineLayout(dev, &li, nullptr, &ccLayout) != VK_SUCCESS)
				SPRaise("Failed to create colour correction pipeline layout");

			VkPipelineVertexInputStateCreateInfo vertexInput{};
			vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

			VkPipelineInputAssemblyStateCreateInfo ia{};
			ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			VkPipelineViewportStateCreateInfo vp{};
			vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			vp.viewportCount = 1;
			vp.scissorCount  = 1;

			VkPipelineRasterizationStateCreateInfo rs{};
			rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rs.polygonMode = VK_POLYGON_MODE_FILL;
			rs.cullMode    = VK_CULL_MODE_NONE;
			rs.lineWidth   = 1.0f;

			VkPipelineMultisampleStateCreateInfo ms{};
			ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			VkPipelineDepthStencilStateCreateInfo dss{};
			dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

			VkDynamicState dynArr[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
			VkPipelineDynamicStateCreateInfo dyn{};
			dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dyn.dynamicStateCount = 2;
			dyn.pDynamicStates    = dynArr;

			VkPipelineColorBlendAttachmentState noBlend{};
			noBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

			VkPipelineColorBlendStateCreateInfo blend{};
			blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			blend.attachmentCount = 1;
			blend.pAttachments    = &noBlend;

			VkPipelineShaderStageCreateInfo stages[2]{};
			stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
			              VK_SHADER_STAGE_VERTEX_BIT,   vs, "main", nullptr};
			stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
			              VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};

			VkGraphicsPipelineCreateInfo pi{};
			pi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pi.stageCount          = 2;
			pi.pStages             = stages;
			pi.pVertexInputState   = &vertexInput;
			pi.pInputAssemblyState = &ia;
			pi.pViewportState      = &vp;
			pi.pRasterizationState = &rs;
			pi.pMultisampleState   = &ms;
			pi.pDepthStencilState  = &dss;
			pi.pColorBlendState    = &blend;
			pi.pDynamicState       = &dyn;
			pi.layout              = ccLayout;
			pi.renderPass          = ppRenderPass;
			pi.subpass             = 0;

			if (vkCreateGraphicsPipelines(dev, cache, 1, &pi, nullptr, &ccPipeline) != VK_SUCCESS)
				SPRaise("Failed to create colour correction pipeline");

			vkDestroyShaderModule(dev, vs, nullptr);
			vkDestroyShaderModule(dev, fs, nullptr);
		}

		void VulkanColorCorrectionFilter::InitDescriptorPools() {
			VkDevice dev = device->GetDevice();

			VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4};
			VkDescriptorPoolCreateInfo info{};
			info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			info.poolSizeCount = 1;
			info.pPoolSizes    = &size;
			info.maxSets       = 4;

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				if (vkCreateDescriptorPool(dev, &info, nullptr, &perFrameDescPool[i]) != VK_SUCCESS)
					SPRaise("Failed to create colour correction descriptor pool");
			}
		}

		// ─────────────────────────────────────────────────────────────────────────
		//  Per-frame helpers
		// ─────────────────────────────────────────────────────────────────────────

		VkFramebuffer VulkanColorCorrectionFilter::MakeFramebuffer(VulkanImage* image, int frameSlot) {
			VkImageView view = image->GetImageView();
			VkFramebufferCreateInfo fbInfo{};
			fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.renderPass      = ppRenderPass;
			fbInfo.attachmentCount = 1;
			fbInfo.pAttachments    = &view;
			fbInfo.width           = image->GetWidth();
			fbInfo.height          = image->GetHeight();
			fbInfo.layers          = 1;

			VkFramebuffer fb;
			if (vkCreateFramebuffer(device->GetDevice(), &fbInfo, nullptr, &fb) != VK_SUCCESS)
				SPRaise("Failed to create colour correction framebuffer");
			perFrameFramebuffers[frameSlot].push_back(fb);
			return fb;
		}

		VkDescriptorSet VulkanColorCorrectionFilter::BindTexture(int frameSlot, VkImageView colorView) {
			VkDevice dev = device->GetDevice();

			VkDescriptorSetAllocateInfo ai{};
			ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			ai.descriptorPool     = perFrameDescPool[frameSlot];
			ai.descriptorSetCount = 1;
			ai.pSetLayouts        = &oneSamplerDSL;

			VkDescriptorSet ds;
			if (vkAllocateDescriptorSets(dev, &ai, &ds) != VK_SUCCESS)
				SPRaise("Failed to allocate colour correction descriptor set");

			VkDescriptorImageInfo imgInfo{colorSampler, colorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

			VkWriteDescriptorSet write{};
			write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet          = ds;
			write.dstBinding      = 0;
			write.descriptorCount = 1;
			write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write.pImageInfo      = &imgInfo;

			vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);
			return ds;
		}

		// ─────────────────────────────────────────────────────────────────────────
		//  Filter
		// ─────────────────────────────────────────────────────────────────────────

		void VulkanColorCorrectionFilter::Filter(VkCommandBuffer cmd,
		                                          VulkanImage*    input,
		                                          VulkanImage*    output) {
			SPADES_MARK_FUNCTION();

			int frameSlot = static_cast<int>(renderer.GetCurrentFrameIndex()) % MAX_FRAME_SLOTS;

			// Reclaim per-frame resources from the previous use of this slot.
			{
				VkDevice dev = device->GetDevice();
				for (VkFramebuffer fb : perFrameFramebuffers[frameSlot])
					vkDestroyFramebuffer(dev, fb, nullptr);
				perFrameFramebuffers[frameSlot].clear();
				vkResetDescriptorPool(dev, perFrameDescPool[frameSlot], 0);
			}

			// ── Compute push constant values (mirrors GLColorCorrectionFilter) ──────

			Vector3 fogColor = renderer.GetFogColor();

			// Seed smoothedFogColor on first call to avoid a pop on startup.
			if (!smoothedFogColorInitialized) {
				smoothedFogColor             = fogColor;
				smoothedFogColorInitialized  = true;
			}

			// White-balance tint derived from the temporally smoothed fog colour.
			// Reciprocal of (fogColor + 0.5), biased 20 % toward neutral white.
			// Normalised so the brightest channel == 1.
			Vector3 tint = smoothedFogColor + MakeVector3(1.f, 1.f, 1.f) * 0.5f;
			tint = MakeVector3(1.f, 1.f, 1.f) / tint;
			tint = Mix(tint, MakeVector3(1.f, 1.f, 1.f), 0.2f);
			float tintMax = std::max({tint.x, tint.y, tint.z});
			if (tintMax > 0.0f)
				tint *= 1.0f / tintMax;

			// Exposure from r_vk_exposureValue (EV stops; default 0 → factor 1.0).
			float exposure = std::pow(2.0f, static_cast<float>(r_vk_exposureValue) * 0.5f);
			tint *= exposure;

			// Saturation (scene saturation × global setting).
			const client::SceneDefinition& def = renderer.GetSceneDef();
			float saturation = def.saturation * static_cast<float>(r_vk_saturation);

			// Enhancement (S-curve strength) — mirrors GL logic.
			float enhancement;
			bool  hdrOn = (int)r_vk_hdr != 0;
			if (hdrOn) {
				enhancement = (int)r_vk_bloom != 0 ? 0.1f : 0.0f;
				saturation  *= (int)r_vk_bloom != 0 ? 0.8f : 0.9f;
			} else {
				enhancement = (int)r_vk_bloom != 0 ? 0.7f : 0.3f;
				saturation  *= (int)r_vk_bloom != 0 ? 0.85f : 1.0f;
			}

			// ── Record draw ───────────────────────────────────────────────────────

			uint32_t rw = static_cast<uint32_t>(output->GetWidth());
			uint32_t rh = static_cast<uint32_t>(output->GetHeight());

			VkFramebuffer   fb = MakeFramebuffer(output, frameSlot);
			VkDescriptorSet ds = BindTexture(frameSlot, input->GetImageView());

			VkRenderPassBeginInfo rpInfo{};
			rpInfo.sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			rpInfo.renderPass  = ppRenderPass;
			rpInfo.framebuffer = fb;
			rpInfo.renderArea  = {{0, 0}, {rw, rh}};

			vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport{0.f, 0.f, static_cast<float>(rw), static_cast<float>(rh), 0.f, 1.f};
			VkRect2D   scissor{{0, 0}, {rw, rh}};
			vkCmdSetViewport(cmd, 0, 1, &viewport);
			vkCmdSetScissor(cmd, 0, 1, &scissor);

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ccPipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			                        ccLayout, 0, 1, &ds, 0, nullptr);

			ColorCorrectionPushConstants pc{};
			pc.tint[0]     = tint.x;
			pc.tint[1]     = tint.y;
			pc.tint[2]     = tint.z;
			pc.saturation  = saturation;
			pc.enhancement = enhancement;
			pc.hdrEnabled  = hdrOn ? 1.0f : 0.0f;

			vkCmdPushConstants(cmd, ccLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
			                   0, sizeof(pc), &pc);

			vkCmdDraw(cmd, 3, 1, 0, 0);

			vkCmdEndRenderPass(cmd);

			// Update smoothed fog colour toward the current fog colour (factor matches GL).
			smoothedFogColor = Mix(smoothedFogColor, fogColor, 0.002f);
		}

	} // namespace draw
} // namespace spades
