/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

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

#include <algorithm>
#include <cmath>
#include <cstring>

#include "VulkanFramebufferManager.h"
#include "VulkanImage.h"
#include "VulkanImageWrapper.h"
#include "VulkanRenderPassUtils.h"
#include "VulkanRenderer.h"
#include "VulkanTemporaryImagePool.h"
#include <Client/SceneDefinition.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/Math.h>
#include <Gui/SDLVulkanDevice.h>

namespace spades {
	namespace draw {

		namespace {
			constexpr uint32_t kVisibilityDim = 64;

			// Push-constant blocks (must match the GLSL layout).

			struct ScannerPC {
				float scanRange[4]; // xy = min, zw = max  (uv in depth texture)
				float scanZ;
			};

			struct BlurPC {
				float unitShift[2];
			};

			struct FlareDrawPC {
				float drawRange[4]; // xy = min, zw = max  (clip space, +Y down)
				float color[4];     // rgb tint, a unused
			};

			VulkanImage* GetVulkanImage(const Handle<client::IImage>& h) {
				auto* wrapper = dynamic_cast<VulkanImageWrapper*>(h.GetPointerOrNull());
				return wrapper ? wrapper->GetVulkanImage() : nullptr;
			}
		} // namespace

		// ─────────────────────────────────────────────────────────────
		//  Construction / destruction
		// ─────────────────────────────────────────────────────────────

		VulkanLensFlareFilter::VulkanLensFlareFilter(VulkanRenderer& r)
		    : VulkanPostProcessFilter(r),
		      colorFormat(VK_FORMAT_UNDEFINED),
		      linearSampler(VK_NULL_HANDLE),
		      depthShadowSampler(VK_NULL_HANDLE),
		      scannerRenderPass(VK_NULL_HANDLE),
		      blurRenderPass(VK_NULL_HANDLE),
		      finalRenderPass(VK_NULL_HANDLE),
		      shadowSamplerDSL(VK_NULL_HANDLE),
		      singleSamplerDSL(VK_NULL_HANDLE),
		      tripleSamplerDSL(VK_NULL_HANDLE),
		      scannerLayout(VK_NULL_HANDLE),
		      blurLayout(VK_NULL_HANDLE),
		      passthroughLayout(VK_NULL_HANDLE),
		      flareDrawLayout(VK_NULL_HANDLE),
		      scannerPipeline(VK_NULL_HANDLE),
		      blurPipeline(VK_NULL_HANDLE),
		      passthroughPipeline(VK_NULL_HANDLE),
		      flareDrawPipeline(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i)
				perFrameDescPool[i] = VK_NULL_HANDLE;

			colorFormat = r.GetFramebufferManager()->GetMainColorFormat();

			InitSamplers();
			InitRenderPasses();
			InitDescriptorSetLayouts();
			InitPipelines();
			InitDescriptorPools();
			LoadSpriteTextures();
		}

		VulkanLensFlareFilter::~VulkanLensFlareFilter() {
			SPADES_MARK_FUNCTION();

			VkDevice dev = device->GetDevice();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				for (VkFramebuffer fb : perFrameFramebuffers[i])
					vkDestroyFramebuffer(dev, fb, nullptr);
				if (perFrameDescPool[i] != VK_NULL_HANDLE)
					vkDestroyDescriptorPool(dev, perFrameDescPool[i], nullptr);
			}

			if (flareDrawPipeline    != VK_NULL_HANDLE) vkDestroyPipeline(dev, flareDrawPipeline,    nullptr);
			if (passthroughPipeline  != VK_NULL_HANDLE) vkDestroyPipeline(dev, passthroughPipeline,  nullptr);
			if (blurPipeline         != VK_NULL_HANDLE) vkDestroyPipeline(dev, blurPipeline,         nullptr);
			if (scannerPipeline      != VK_NULL_HANDLE) vkDestroyPipeline(dev, scannerPipeline,      nullptr);

			if (flareDrawLayout    != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, flareDrawLayout,    nullptr);
			if (passthroughLayout  != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, passthroughLayout,  nullptr);
			if (blurLayout         != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, blurLayout,         nullptr);
			if (scannerLayout      != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, scannerLayout,      nullptr);

			if (tripleSamplerDSL != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, tripleSamplerDSL, nullptr);
			if (singleSamplerDSL != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, singleSamplerDSL, nullptr);
			if (shadowSamplerDSL != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, shadowSamplerDSL, nullptr);

			if (finalRenderPass   != VK_NULL_HANDLE) vkDestroyRenderPass(dev, finalRenderPass,   nullptr);
			if (blurRenderPass    != VK_NULL_HANDLE) vkDestroyRenderPass(dev, blurRenderPass,    nullptr);
			if (scannerRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(dev, scannerRenderPass, nullptr);

			if (depthShadowSampler != VK_NULL_HANDLE) vkDestroySampler(dev, depthShadowSampler, nullptr);
			if (linearSampler      != VK_NULL_HANDLE) vkDestroySampler(dev, linearSampler,      nullptr);
		}

		// ─────────────────────────────────────────────────────────────
		//  Initialisation
		// ─────────────────────────────────────────────────────────────

		void VulkanLensFlareFilter::InitSamplers() {
			VkDevice dev = device->GetDevice();

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

			if (vkCreateSampler(dev, &si, nullptr, &linearSampler) != VK_SUCCESS)
				SPRaise("Failed to create lens flare linear sampler");

			// Shadow-compare sampler for sampler2DShadow on the depth texture.
			// Bilinear filtering of the comparison result gives a soft visibility
			// disc, matching the GL `sampler2DShadow` + `CompareRefToTexture` path.
			VkSamplerCreateInfo ss = si;
			ss.compareEnable = VK_TRUE;
			ss.compareOp     = VK_COMPARE_OP_LESS;

			if (vkCreateSampler(dev, &ss, nullptr, &depthShadowSampler) != VK_SUCCESS)
				SPRaise("Failed to create lens flare depth-shadow sampler");
		}

		void VulkanLensFlareFilter::InitRenderPasses() {
			VkDevice dev = device->GetDevice();

			VkSubpassDependency dep{};
			dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
			dep.dstSubpass    = 0;
			dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dep.dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			// Scanner writes a fresh visibility texture; CLEAR the attachment so
			// the un-drawn outer border stays black for the subsequent blur.
			scannerRenderPass = CreateSimpleColorRenderPass(
			    dev, colorFormat,
			    VK_ATTACHMENT_LOAD_OP_CLEAR,
			    VK_IMAGE_LAYOUT_UNDEFINED,
			    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			    &dep);

			blurRenderPass = CreateSimpleColorRenderPass(
			    dev, colorFormat,
			    VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			    VK_IMAGE_LAYOUT_UNDEFINED,
			    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			    &dep);

			finalRenderPass = CreateSimpleColorRenderPass(
			    dev, colorFormat,
			    VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			    VK_IMAGE_LAYOUT_UNDEFINED,
			    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			    &dep);
		}

		void VulkanLensFlareFilter::InitDescriptorSetLayouts() {
			VkDevice dev = device->GetDevice();

			auto MakeDSL = [&](uint32_t bindingCount) {
				VkDescriptorSetLayoutBinding bindings[3]{};
				for (uint32_t i = 0; i < bindingCount; ++i) {
					bindings[i].binding         = i;
					bindings[i].descriptorCount = 1;
					bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
				}
				VkDescriptorSetLayoutCreateInfo info{};
				info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
				info.bindingCount = bindingCount;
				info.pBindings    = bindings;
				VkDescriptorSetLayout dsl;
				if (vkCreateDescriptorSetLayout(dev, &info, nullptr, &dsl) != VK_SUCCESS)
					SPRaise("Failed to create lens flare descriptor set layout");
				return dsl;
			};

			shadowSamplerDSL = MakeDSL(1); // depth (compare sampler)
			singleSamplerDSL = MakeDSL(1); // generic single sampler
			tripleSamplerDSL = MakeDSL(3); // visibility, modulation, flare
		}

		VkShaderModule VulkanLensFlareFilter::LoadSPIRV(const char* path) {
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

		void VulkanLensFlareFilter::InitPipelines() {
			VkDevice dev          = device->GetDevice();
			VkPipelineCache cache = renderer.GetPipelineCache();

			VkShaderModule passVS    = LoadSPIRV("Shaders/Vulkan/PostFilters/PassThrough.vk.vs.spv");
			VkShaderModule passFS    = LoadSPIRV("Shaders/Vulkan/PostFilters/PassThrough.vk.fs.spv");
			VkShaderModule scannerVS = LoadSPIRV("Shaders/Vulkan/PostFilters/LensFlareScanner.vk.vs.spv");
			VkShaderModule scannerFS = LoadSPIRV("Shaders/Vulkan/PostFilters/LensFlareScanner.vk.fs.spv");
			VkShaderModule blurFS    = LoadSPIRV("Shaders/Vulkan/PostFilters/LensFlareBlur.vk.fs.spv");
			VkShaderModule drawVS    = LoadSPIRV("Shaders/Vulkan/PostFilters/LensFlareDraw.vk.vs.spv");
			VkShaderModule drawFS    = LoadSPIRV("Shaders/Vulkan/PostFilters/LensFlareDraw.vk.fs.spv");

			// Shared fixed state ──────────────────────────────────

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

			VkPipelineDepthStencilStateCreateInfo ds{};
			ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

			VkDynamicState dynArr[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
			VkPipelineDynamicStateCreateInfo dyn{};
			dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dyn.dynamicStateCount = 2;
			dyn.pDynamicStates    = dynArr;

			VkPipelineColorBlendAttachmentState noBlend{};
			noBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

			VkPipelineColorBlendAttachmentState addBlend{};
			addBlend.blendEnable         = VK_TRUE;
			addBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
			addBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
			addBlend.colorBlendOp        = VK_BLEND_OP_ADD;
			addBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			addBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			addBlend.alphaBlendOp        = VK_BLEND_OP_ADD;
			addBlend.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

			// Pipeline layouts ───────────────────────────────────

			{
				VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ScannerPC)};
				VkPipelineLayoutCreateInfo li{};
				li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				li.setLayoutCount         = 1;
				li.pSetLayouts            = &shadowSamplerDSL;
				li.pushConstantRangeCount = 1;
				li.pPushConstantRanges    = &pcr;
				if (vkCreatePipelineLayout(dev, &li, nullptr, &scannerLayout) != VK_SUCCESS)
					SPRaise("Failed to create lens flare scanner pipeline layout");
			}
			{
				VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(BlurPC)};
				VkPipelineLayoutCreateInfo li{};
				li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				li.setLayoutCount         = 1;
				li.pSetLayouts            = &singleSamplerDSL;
				li.pushConstantRangeCount = 1;
				li.pPushConstantRanges    = &pcr;
				if (vkCreatePipelineLayout(dev, &li, nullptr, &blurLayout) != VK_SUCCESS)
					SPRaise("Failed to create lens flare blur pipeline layout");
			}
			{
				VkPipelineLayoutCreateInfo li{};
				li.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				li.setLayoutCount = 1;
				li.pSetLayouts    = &singleSamplerDSL;
				if (vkCreatePipelineLayout(dev, &li, nullptr, &passthroughLayout) != VK_SUCCESS)
					SPRaise("Failed to create lens flare passthrough pipeline layout");
			}
			{
				VkPushConstantRange pcr{
				    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				    0, sizeof(FlareDrawPC)};
				VkPipelineLayoutCreateInfo li{};
				li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				li.setLayoutCount         = 1;
				li.pSetLayouts            = &tripleSamplerDSL;
				li.pushConstantRangeCount = 1;
				li.pPushConstantRanges    = &pcr;
				if (vkCreatePipelineLayout(dev, &li, nullptr, &flareDrawLayout) != VK_SUCCESS)
					SPRaise("Failed to create lens flare draw pipeline layout");
			}

			auto MakePipeline = [&](VkShaderModule vsModule,
			                         VkShaderModule fsModule,
			                         VkPipelineLayout layout,
			                         VkRenderPass rp,
			                         const VkPipelineColorBlendAttachmentState& blendState) {
				VkPipelineShaderStageCreateInfo stages[2]{};
				stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
				              VK_SHADER_STAGE_VERTEX_BIT, vsModule, "main", nullptr};
				stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
				              VK_SHADER_STAGE_FRAGMENT_BIT, fsModule, "main", nullptr};

				VkPipelineColorBlendStateCreateInfo blend{};
				blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
				blend.attachmentCount = 1;
				blend.pAttachments    = &blendState;

				VkGraphicsPipelineCreateInfo pi{};
				pi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
				pi.stageCount          = 2;
				pi.pStages             = stages;
				pi.pVertexInputState   = &vertexInput;
				pi.pInputAssemblyState = &ia;
				pi.pViewportState      = &vp;
				pi.pRasterizationState = &rs;
				pi.pMultisampleState   = &ms;
				pi.pDepthStencilState  = &ds;
				pi.pColorBlendState    = &blend;
				pi.pDynamicState       = &dyn;
				pi.layout              = layout;
				pi.renderPass          = rp;
				pi.subpass             = 0;

				VkPipeline p;
				if (vkCreateGraphicsPipelines(dev, cache, 1, &pi, nullptr, &p) != VK_SUCCESS)
					SPRaise("Failed to create lens flare pipeline");
				return p;
			};

			scannerPipeline     = MakePipeline(scannerVS, scannerFS, scannerLayout,
			                                    scannerRenderPass, noBlend);
			blurPipeline        = MakePipeline(passVS,    blurFS,    blurLayout,
			                                    blurRenderPass,    noBlend);
			passthroughPipeline = MakePipeline(passVS,    passFS,    passthroughLayout,
			                                    finalRenderPass,   noBlend);
			flareDrawPipeline   = MakePipeline(drawVS,    drawFS,    flareDrawLayout,
			                                    finalRenderPass,   addBlend);

			vkDestroyShaderModule(dev, drawFS,    nullptr);
			vkDestroyShaderModule(dev, drawVS,    nullptr);
			vkDestroyShaderModule(dev, blurFS,    nullptr);
			vkDestroyShaderModule(dev, scannerFS, nullptr);
			vkDestroyShaderModule(dev, scannerVS, nullptr);
			vkDestroyShaderModule(dev, passFS,    nullptr);
			vkDestroyShaderModule(dev, passVS,    nullptr);
		}

		void VulkanLensFlareFilter::InitDescriptorPools() {
			VkDevice dev = device->GetDevice();

			// Per Filter() call:
			//   1 (scanner) + 6 (blurs) + 1 (passthrough) + up to 15 (flare draws)
			//   ≈ 23 sets × ~2.5 image-info → round up to 32 sets / 64 samplers.
			VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 96};
			VkDescriptorPoolCreateInfo info{};
			info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			info.poolSizeCount = 1;
			info.pPoolSizes    = &size;
			info.maxSets       = 32;

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				if (vkCreateDescriptorPool(dev, &info, nullptr, &perFrameDescPool[i]) != VK_SUCCESS)
					SPRaise("Failed to create lens flare descriptor pool");
			}
		}

		void VulkanLensFlareFilter::LoadSpriteTextures() {
			flare1 = renderer.RegisterImage("Gfx/LensFlare/1.png");
			flare2 = renderer.RegisterImage("Gfx/LensFlare/2.png");
			flare3 = renderer.RegisterImage("Gfx/LensFlare/3.png");
			flare4 = renderer.RegisterImage("Gfx/LensFlare/4.jpg");
			mask1  = renderer.RegisterImage("Gfx/LensFlare/mask1.png");
			mask2  = renderer.RegisterImage("Gfx/LensFlare/mask2.png");
			mask3  = renderer.RegisterImage("Gfx/LensFlare/mask3.png");
			white  = renderer.RegisterImage("Gfx/White.tga");
		}

		// ─────────────────────────────────────────────────────────────
		//  Per-frame helpers
		// ─────────────────────────────────────────────────────────────

		VkFramebuffer VulkanLensFlareFilter::MakeFramebuffer(VkRenderPass rp,
		                                                     VulkanImage* image,
		                                                     int frameSlot) {
			VkImageView view = image->GetImageView();
			VkFramebufferCreateInfo fbInfo{};
			fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.renderPass      = rp;
			fbInfo.attachmentCount = 1;
			fbInfo.pAttachments    = &view;
			fbInfo.width           = image->GetWidth();
			fbInfo.height          = image->GetHeight();
			fbInfo.layers          = 1;

			VkFramebuffer fb;
			if (vkCreateFramebuffer(device->GetDevice(), &fbInfo, nullptr, &fb) != VK_SUCCESS)
				SPRaise("Failed to create lens flare framebuffer");
			perFrameFramebuffers[frameSlot].push_back(fb);
			return fb;
		}

		VkDescriptorSet VulkanLensFlareFilter::BindShadowDepth(int frameSlot, VkImageView depthView) {
			VkDescriptorSetAllocateInfo ai{};
			ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			ai.descriptorPool     = perFrameDescPool[frameSlot];
			ai.descriptorSetCount = 1;
			ai.pSetLayouts        = &shadowSamplerDSL;
			VkDescriptorSet set;
			if (vkAllocateDescriptorSets(device->GetDevice(), &ai, &set) != VK_SUCCESS)
				SPRaise("Failed to allocate lens flare shadow descriptor set");

			VkDescriptorImageInfo img{depthShadowSampler, depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
			VkWriteDescriptorSet w{};
			w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			w.dstSet          = set;
			w.dstBinding      = 0;
			w.descriptorCount = 1;
			w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			w.pImageInfo      = &img;
			vkUpdateDescriptorSets(device->GetDevice(), 1, &w, 0, nullptr);
			return set;
		}

		VkDescriptorSet VulkanLensFlareFilter::BindSingleTexture(int frameSlot, VkImageView view) {
			VkDescriptorSetAllocateInfo ai{};
			ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			ai.descriptorPool     = perFrameDescPool[frameSlot];
			ai.descriptorSetCount = 1;
			ai.pSetLayouts        = &singleSamplerDSL;
			VkDescriptorSet set;
			if (vkAllocateDescriptorSets(device->GetDevice(), &ai, &set) != VK_SUCCESS)
				SPRaise("Failed to allocate lens flare single descriptor set");

			VkDescriptorImageInfo img{linearSampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
			VkWriteDescriptorSet w{};
			w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			w.dstSet          = set;
			w.dstBinding      = 0;
			w.descriptorCount = 1;
			w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			w.pImageInfo      = &img;
			vkUpdateDescriptorSets(device->GetDevice(), 1, &w, 0, nullptr);
			return set;
		}

		VkDescriptorSet VulkanLensFlareFilter::BindFlareTextures(int frameSlot,
		                                                          VkImageView visibility,
		                                                          VkImageView modulation,
		                                                          VkImageView flare) {
			VkDescriptorSetAllocateInfo ai{};
			ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			ai.descriptorPool     = perFrameDescPool[frameSlot];
			ai.descriptorSetCount = 1;
			ai.pSetLayouts        = &tripleSamplerDSL;
			VkDescriptorSet set;
			if (vkAllocateDescriptorSets(device->GetDevice(), &ai, &set) != VK_SUCCESS)
				SPRaise("Failed to allocate lens flare triple descriptor set");

			VkDescriptorImageInfo imgs[3]{
			    {linearSampler, visibility,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			    {linearSampler, modulation,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			    {linearSampler, flare,       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			};
			VkWriteDescriptorSet writes[3]{};
			for (int i = 0; i < 3; ++i) {
				writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[i].dstSet          = set;
				writes[i].dstBinding      = static_cast<uint32_t>(i);
				writes[i].descriptorCount = 1;
				writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[i].pImageInfo      = &imgs[i];
			}
			vkUpdateDescriptorSets(device->GetDevice(), 3, writes, 0, nullptr);
			return set;
		}

		void VulkanLensFlareFilter::DrawFlareQuad(VkCommandBuffer cmd, int frameSlot,
		                                           VkImageView visibility,
		                                           const Handle<client::IImage>& modulation,
		                                           const Handle<client::IImage>& flare,
		                                           const float drawRange[4],
		                                           const float color[3]) {
			VulkanImage* modImg = GetVulkanImage(modulation);
			VulkanImage* flrImg = GetVulkanImage(flare);
			if (!modImg || !flrImg)
				return; // texture failed to load; silently skip this quad

			VkDescriptorSet ds = BindFlareTextures(frameSlot, visibility,
			                                       modImg->GetImageView(),
			                                       flrImg->GetImageView());

			FlareDrawPC pc{};
			pc.drawRange[0] = drawRange[0];
			pc.drawRange[1] = drawRange[1];
			pc.drawRange[2] = drawRange[2];
			pc.drawRange[3] = drawRange[3];
			pc.color[0] = color[0];
			pc.color[1] = color[1];
			pc.color[2] = color[2];
			pc.color[3] = 1.0f;

			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			                        flareDrawLayout, 0, 1, &ds, 0, nullptr);
			vkCmdPushConstants(cmd, flareDrawLayout,
			                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			                   0, sizeof(pc), &pc);
			vkCmdDraw(cmd, 6, 1, 0, 0);
		}

		// ─────────────────────────────────────────────────────────────
		//  Filter()
		// ─────────────────────────────────────────────────────────────

		void VulkanLensFlareFilter::Filter(VkCommandBuffer cmd,
		                                    VulkanImage* input,
		                                    VulkanImage* output) {
			SPADES_MARK_FUNCTION();

			int frameSlot = static_cast<int>(renderer.GetCurrentFrameIndex());

			// Reclaim per-frame resources from the previous use of this slot.
			{
				VkDevice dev = device->GetDevice();
				for (VkFramebuffer fb : perFrameFramebuffers[frameSlot])
					vkDestroyFramebuffer(dev, fb, nullptr);
				perFrameFramebuffers[frameSlot].clear();
				vkResetDescriptorPool(dev, perFrameDescPool[frameSlot], 0);
			}

			const client::SceneDefinition& def = renderer.GetSceneDef();
			const float outW = static_cast<float>(output->GetWidth());
			const float outH = static_cast<float>(output->GetHeight());

			// ── 1. Sun NDC position (GL convention, +Y up) ────────────
			// Mirrors GLLensFlareFilter::Draw()'s hard-coded sun call.
			const Vector3 sunDir = MakeVector3(0.0F, -1.0F, -1.0F);
			const Vector3 sunCol = MakeVector3(1.0F, 0.9F, 0.8F);
			constexpr bool infinityDistance = true;
			constexpr bool renderReflections = true;

			Vector3 sunView = {
			    Vector3::Dot(sunDir, def.viewAxis[0]),
			    Vector3::Dot(sunDir, def.viewAxis[1]),
			    Vector3::Dot(sunDir, def.viewAxis[2]),
			};

			const bool sunVisible = sunView.z > 0.0F;

			float sunScreenGLx = 0.0F;
			float sunScreenGLy = 0.0F;
			float sunTexPosX = 0.5F;
			float sunTexPosY = 0.5F;
			float sunTexSizeX = 0.0F;
			float sunTexSizeY = 0.0F;
			float scanZ = 0.9999999F;

			if (sunVisible) {
				const float fovTanX = std::tan(def.fovX * 0.5F);
				const float fovTanY = std::tan(def.fovY * 0.5F);
				sunScreenGLx = sunView.x / (sunView.z * fovTanX);
				sunScreenGLy = sunView.y / (sunView.z * fovTanY);

				const float sunRadiusTan = std::tan(0.53F * 0.5F * M_PI_F / 180.0F);
				const float sunSizeXNDC  = sunRadiusTan / fovTanX;
				const float sunSizeYNDC  = sunRadiusTan / fovTanY;

				// Convert sunScreen to the offscreen-texture UV space.
				// Vulkan stores row 0 at the top of the displayed scene
				// (negative-height main viewport), so the +Y-up GL NDC
				// needs a vertical flip: vk_y = -gl_y, then *0.5+0.5.
				sunTexPosX = sunScreenGLx * 0.5F + 0.5F;
				sunTexPosY = -sunScreenGLy * 0.5F + 0.5F;
				sunTexSizeX = sunSizeXNDC * 0.5F;
				sunTexSizeY = sunSizeYNDC * 0.5F;

				if (!infinityDistance) {
					const float fnear = def.zNear;
					const float ffar  = def.zFar;
					const float depth = sunView.z;
					scanZ = ffar * (fnear - depth) / (depth * (fnear - ffar));
				}
			}

			VulkanTemporaryImagePool* pool = renderer.GetTemporaryImagePool();

			// ── 2. Scanner pass (skip if sun not visible) ─────────────
			Handle<VulkanImage> visibilityImg;
			std::vector<Handle<VulkanImage>> blurTemps;
			if (sunVisible && pool) {
				visibilityImg = pool->Acquire(kVisibilityDim, kVisibilityDim, colorFormat);
				VkFramebuffer fb = MakeFramebuffer(scannerRenderPass,
				                                    visibilityImg.GetPointerOrNull(), frameSlot);

				Handle<VulkanImage> depthImg = renderer.GetFramebufferManager()->GetResolvedDepthImage();
				VkDescriptorSet depthDS = BindShadowDepth(frameSlot, depthImg->GetImageView());

				VkClearValue cv{};
				cv.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

				VkRenderPassBeginInfo rpBegin{};
				rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				rpBegin.renderPass        = scannerRenderPass;
				rpBegin.framebuffer       = fb;
				rpBegin.renderArea.extent = {kVisibilityDim, kVisibilityDim};
				rpBegin.clearValueCount   = 1;
				rpBegin.pClearValues      = &cv;

				vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport viewport{0.0f, 0.0f, (float)kVisibilityDim, (float)kVisibilityDim, 0.0f, 1.0f};
				VkRect2D   scissor{{0, 0}, {kVisibilityDim, kVisibilityDim}};
				vkCmdSetViewport(cmd, 0, 1, &viewport);
				vkCmdSetScissor(cmd, 0, 1, &scissor);

				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scannerPipeline);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
				                        scannerLayout, 0, 1, &depthDS, 0, nullptr);

				ScannerPC pc{};
				pc.scanRange[0] = sunTexPosX - sunTexSizeX;
				pc.scanRange[1] = sunTexPosY - sunTexSizeY;
				pc.scanRange[2] = sunTexPosX + sunTexSizeX;
				pc.scanRange[3] = sunTexPosY + sunTexSizeY;
				pc.scanZ        = scanZ;
				vkCmdPushConstants(cmd, scannerLayout, VK_SHADER_STAGE_VERTEX_BIT,
				                   0, sizeof(pc), &pc);

				vkCmdDraw(cmd, 6, 1, 0, 0);
				vkCmdEndRenderPass(cmd);

				// ── 3. Three 1D-Gauss blurs (spread 1, 2, 4), x then y ─
				auto BlurPass = [&](VulkanImage* src, VulkanImage* dst, float shiftX, float shiftY) {
					VkFramebuffer fb2 = MakeFramebuffer(blurRenderPass, dst, frameSlot);
					VkDescriptorSet srcDS = BindSingleTexture(frameSlot, src->GetImageView());

					VkRenderPassBeginInfo rb{};
					rb.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
					rb.renderPass        = blurRenderPass;
					rb.framebuffer       = fb2;
					rb.renderArea.extent = {dst->GetWidth(), dst->GetHeight()};
					vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);

					VkViewport vp2{0.0f, 0.0f, (float)dst->GetWidth(), (float)dst->GetHeight(), 0.0f, 1.0f};
					VkRect2D   sc2{{0, 0}, {dst->GetWidth(), dst->GetHeight()}};
					vkCmdSetViewport(cmd, 0, 1, &vp2);
					vkCmdSetScissor(cmd, 0, 1, &sc2);

					vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline);
					vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
					                        blurLayout, 0, 1, &srcDS, 0, nullptr);

					BlurPC bpc{{shiftX, shiftY}};
					vkCmdPushConstants(cmd, blurLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
					                   0, sizeof(bpc), &bpc);

					vkCmdDraw(cmd, 6, 1, 0, 0);
					vkCmdEndRenderPass(cmd);
				};

				const float invDim = 1.0F / (float)kVisibilityDim;
				const float spreads[3] = {1.0F, 2.0F, 4.0F};
				VulkanImage* prev = visibilityImg.GetPointerOrNull();

				for (int i = 0; i < 3; ++i) {
					Handle<VulkanImage> tmpX = pool->Acquire(kVisibilityDim, kVisibilityDim, colorFormat);
					BlurPass(prev, tmpX.GetPointerOrNull(), spreads[i] * invDim, 0.0F);

					Handle<VulkanImage> tmpY = pool->Acquire(kVisibilityDim, kVisibilityDim, colorFormat);
					BlurPass(tmpX.GetPointerOrNull(), tmpY.GetPointerOrNull(), 0.0F, spreads[i] * invDim);

					blurTemps.push_back(std::move(tmpX));
					if (i + 1 < 3) {
						blurTemps.push_back(std::move(visibilityImg));
					}
					visibilityImg = std::move(tmpY);
					prev = visibilityImg.GetPointerOrNull();
				}
			}

			// ── 4. Final pass: passthrough(input → output) + additive flares
			{
				VkFramebuffer fb = MakeFramebuffer(finalRenderPass, output, frameSlot);

				VkRenderPassBeginInfo rb{};
				rb.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				rb.renderPass        = finalRenderPass;
				rb.framebuffer       = fb;
				rb.renderArea.extent = {output->GetWidth(), output->GetHeight()};
				vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport vp2{0.0f, 0.0f, outW, outH, 0.0f, 1.0f};
				VkRect2D   sc2{{0, 0}, {output->GetWidth(), output->GetHeight()}};
				vkCmdSetViewport(cmd, 0, 1, &vp2);
				vkCmdSetScissor(cmd, 0, 1, &sc2);

				// 4a. Passthrough scene → output (no blend)
				{
					VkDescriptorSet inputDS = BindSingleTexture(frameSlot, input->GetImageView());
					vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, passthroughPipeline);
					vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
					                        passthroughLayout, 0, 1, &inputDS, 0, nullptr);
					vkCmdDraw(cmd, 3, 1, 0, 0);
				}

				// 4b. Additive flare quads — only if the sun is in front of us
				if (sunVisible && visibilityImg) {
					vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, flareDrawPipeline);
					VkImageView visView = visibilityImg->GetImageView();

					// Flip the sun's NDC y so quads land at the right pixel row
					// (the Y-down Vulkan framebuffer vs. the +Y-up GL math).
					const float sx = sunScreenGLx;
					const float sy = -sunScreenGLy;

					// Pixel-aspect-corrected sprite size.  Matches GL:
					//   sunSize = (0.01, 0.01); sunSize.x *= ScreenH / ScreenW.
					float szx = 0.01F;
					float szy = 0.01F;
					const float scrW = renderer.ScreenWidth();
					const float scrH = renderer.ScreenHeight();
					if (scrW > 0.0F)
						szx *= scrH / scrW;

					const float sqLen = sx * sx + sy * sy;
					const float aroundness  = sqLen * 0.6F;
					const float aroundness2 = std::min(sqLen * 3.2F, 1.0F);

					auto MakeRange = [&](float xMin, float yMin, float xMax, float yMax,
					                      float out[4]) {
						out[0] = xMin; out[1] = yMin; out[2] = xMax; out[3] = yMax;
					};
					auto CenteredRange = [&](float cx, float cy, float halfW, float halfH,
					                          float out[4]) {
						out[0] = cx - halfW;
						out[1] = cy - halfH;
						out[2] = cx + halfW;
						out[3] = cy + halfH;
					};

					float dr[4];
					float color[3];

					// — Sun glow ring (flare4 + white)
					CenteredRange(sx, sy, szx * 256.0F, szy * 256.0F, dr);
					color[0] = sunCol.x * 0.04F;
					color[1] = sunCol.y * 0.03F;
					color[2] = sunCol.z * 0.04F;
					DrawFlareQuad(cmd, frameSlot, visView, white, flare4, dr, color);

					// — Concentric white sun glows
					CenteredRange(sx, sy, szx * 64.0F, szy * 64.0F, dr);
					color[0] = sunCol.x * 0.3F; color[1] = sunCol.y * 0.3F; color[2] = sunCol.z * 0.3F;
					DrawFlareQuad(cmd, frameSlot, visView, white, white, dr, color);

					CenteredRange(sx, sy, szx * 32.0F, szy * 32.0F, dr);
					color[0] = sunCol.x * 0.5F; color[1] = sunCol.y * 0.5F; color[2] = sunCol.z * 0.5F;
					DrawFlareQuad(cmd, frameSlot, visView, white, white, dr, color);

					CenteredRange(sx, sy, szx * 16.0F, szy * 16.0F, dr);
					color[0] = sunCol.x * 0.8F; color[1] = sunCol.y * 0.8F; color[2] = sunCol.z * 0.8F;
					DrawFlareQuad(cmd, frameSlot, visView, white, white, dr, color);

					CenteredRange(sx, sy, szx * 4.0F, szy * 4.0F, dr);
					color[0] = sunCol.x * 1.0F; color[1] = sunCol.y * 1.0F; color[2] = sunCol.z * 1.0F;
					DrawFlareQuad(cmd, frameSlot, visView, white, white, dr, color);

					// — Horizontal sun stripe (256×8 in NDC)
					CenteredRange(sx, sy, szx * 256.0F, szy * 8.0F, dr);
					color[0] = sunCol.x * 0.1F;
					color[1] = sunCol.y * 0.05F;
					color[2] = sunCol.z * 0.1F;
					DrawFlareQuad(cmd, frameSlot, visView, white, white, dr, color);

					// — Dust ring (mask3 + white), modulated by `aroundness`
					CenteredRange(sx, sy, szx * 188.0F, szy * 188.0F, dr);
					color[0] = sunCol.x * 0.4F * aroundness;
					color[1] = sunCol.y * 0.4F * aroundness;
					color[2] = sunCol.z * 0.4F * aroundness;
					DrawFlareQuad(cmd, frameSlot, visView, mask3, white, dr, color);

					if (renderReflections) {
						// Reflection 1 (white + flare2, mirrored through origin × 0.4)
						MakeRange(-(sx - szx * 18.0F) * 0.4F,
						           -(sy - szy * 18.0F) * 0.4F,
						           -(sx + szx * 18.0F) * 0.4F,
						           -(sy + szy * 18.0F) * 0.4F, dr);
						color[0] = sunCol.x; color[1] = sunCol.y; color[2] = sunCol.z;
						DrawFlareQuad(cmd, frameSlot, visView, white, flare2, dr, color);

						MakeRange(-(sx - szx * 6.0F) * 0.39F,
						           -(sy - szy * 6.0F) * 0.39F,
						           -(sx + szx * 6.0F) * 0.39F,
						           -(sy + szy * 6.0F) * 0.39F, dr);
						color[0] = sunCol.x * 0.3F; color[1] = sunCol.y * 0.3F; color[2] = sunCol.z * 0.3F;
						DrawFlareQuad(cmd, frameSlot, visView, white, flare2, dr, color);

						MakeRange(-(sx - szx * 6.0F) * 0.3F,
						           -(sy - szy * 6.0F) * 0.3F,
						           -(sx + szx * 6.0F) * 0.3F,
						           -(sy + szy * 6.0F) * 0.3F, dr);
						color[0] = sunCol.x; color[1] = sunCol.y; color[2] = sunCol.z;
						DrawFlareQuad(cmd, frameSlot, visView, white, flare2, dr, color);

						MakeRange((sx - szx * 12.0F) * 0.6F,
						           (sy - szy * 12.0F) * 0.6F,
						           (sx + szx * 12.0F) * 0.6F,
						           (sy + szy * 12.0F) * 0.6F, dr);
						color[0] = sunCol.x * 0.3F; color[1] = sunCol.y * 0.3F; color[2] = sunCol.z * 0.3F;
						DrawFlareQuad(cmd, frameSlot, visView, white, flare2, dr, color);

						// — mask2 + flare1 mirrors
						MakeRange((sx - szx * 96.0F) * 2.3F,
						           (sy - szy * 96.0F) * 2.3F,
						           (sx + szx * 96.0F) * 2.3F,
						           (sy + szy * 96.0F) * 2.3F, dr);
						color[0] = sunCol.x * 0.5F;
						color[1] = sunCol.y * 0.4F;
						color[2] = sunCol.z * 0.3F;
						DrawFlareQuad(cmd, frameSlot, visView, mask2, flare1, dr, color);

						MakeRange((sx - szx * 128.0F) * 0.8F,
						           (sy - szy * 128.0F) * 0.8F,
						           (sx + szx * 128.0F) * 0.8F,
						           (sy + szy * 128.0F) * 0.8F, dr);
						color[0] = sunCol.x * 0.3F;
						color[1] = sunCol.y * 0.2F;
						color[2] = sunCol.z * 0.1F;
						DrawFlareQuad(cmd, frameSlot, visView, mask2, flare1, dr, color);

						// — mask2 + flare3 (close mirror)
						MakeRange((sx - szx * 18.0F) * 0.5F,
						           (sy - szy * 18.0F) * 0.5F,
						           (sx + szx * 18.0F) * 0.5F,
						           (sy + szy * 18.0F) * 0.5F, dr);
						color[0] = sunCol.x * 0.3F; color[1] = sunCol.y * 0.3F; color[2] = sunCol.z * 0.3F;
						DrawFlareQuad(cmd, frameSlot, visView, mask2, flare3, dr, color);

						// — mask1 + flare3 large aroundness ring
						const float reflSize = 50.0F + aroundness2 * 60.0F;
						MakeRange((sx - szx * reflSize) * -2.0F,
						           (sy - szy * reflSize) * -2.0F,
						           (sx + szx * reflSize) * -2.0F,
						           (sy + szy * reflSize) * -2.0F, dr);
						color[0] = sunCol.x * 0.8F * aroundness2;
						color[1] = sunCol.y * 0.5F * aroundness2;
						color[2] = sunCol.z * 0.3F * aroundness2;
						DrawFlareQuad(cmd, frameSlot, visView, mask1, flare3, dr, color);
					}
				}

				vkCmdEndRenderPass(cmd);
			}

			// Return temp images only after the command buffer has finished
			// referencing them — defer Return() to end of Filter() (the pool
			// is reused next frame, but this call is the last command-buffer
			// recording in the chain for these images).
			if (pool) {
				if (visibilityImg)
					pool->Return(visibilityImg.GetPointerOrNull());
				for (auto& tmp : blurTemps)
					pool->Return(tmp.GetPointerOrNull());
			}
		}

	} // namespace draw
} // namespace spades
