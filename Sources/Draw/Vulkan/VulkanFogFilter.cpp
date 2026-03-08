/*
 Copyright (c) 2021 yvt

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

#include "VulkanFogFilter.h"
#include "VulkanFramebufferManager.h"
#include "VulkanImage.h"
#include "VulkanMapShadowRenderer.h"
#include "VulkanRenderer.h"
#include "VulkanRenderPassUtils.h"
#include "VulkanTemporaryImagePool.h"
#include <Client/SceneDefinition.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/Math.h>
#include <Gui/SDLVulkanDevice.h>
#include <cmath>
#include <cstring>

namespace spades {
	namespace draw {

		// Push constants layout (matches Fog2.vk.vs + Fog2.vk.fs, total = 128 bytes).
		struct FogPushConstants {
			float viewProjInv[16];     // [0..63]  mat4 viewProjectionMatrixInv
			float viewOriginFogDist[4]; // [64..79] xyz=viewOrigin, w=fogDistance
			float sunlightScale[4];    // [80..95] xyz used
			float ambientScale[4];     // [96..111] xyz used
			float ditherFrame[4];      // [112..127] xy=per-frame noise seed
		};
		static_assert(sizeof(FogPushConstants) == 128, "FogPushConstants must be 128 bytes");

		// ─────────────────────────────────────────────────────────────────────────
		//  Construction / destruction
		// ─────────────────────────────────────────────────────────────────────────

		VulkanFogFilter::VulkanFogFilter(VulkanRenderer& r)
		    : VulkanPostProcessFilter(r),
		      colorFormat(VK_FORMAT_UNDEFINED),
		      colorSampler(VK_NULL_HANDLE),
		      ppRenderPass(VK_NULL_HANDLE),
		      triSamplerDSL(VK_NULL_HANDLE),
		      fogLayout(VK_NULL_HANDLE),
		      fogPipeline(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i)
				perFrameDescPool[i] = VK_NULL_HANDLE;

			colorFormat = r.GetFramebufferManager()->GetMainColorFormat();

			InitRenderPass();
			InitDescriptorSetLayout();
			InitPipeline();
			InitDescriptorPools();
		}

		VulkanFogFilter::~VulkanFogFilter() {
			SPADES_MARK_FUNCTION();

			VkDevice dev = device->GetDevice();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				for (VkFramebuffer fb : perFrameFramebuffers[i])
					vkDestroyFramebuffer(dev, fb, nullptr);
				if (perFrameDescPool[i] != VK_NULL_HANDLE)
					vkDestroyDescriptorPool(dev, perFrameDescPool[i], nullptr);
			}

			if (fogPipeline   != VK_NULL_HANDLE) vkDestroyPipeline(dev, fogPipeline, nullptr);
			if (fogLayout     != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, fogLayout, nullptr);
			if (triSamplerDSL != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, triSamplerDSL, nullptr);
			if (colorSampler  != VK_NULL_HANDLE) vkDestroySampler(dev, colorSampler, nullptr);
			if (ppRenderPass  != VK_NULL_HANDLE) vkDestroyRenderPass(dev, ppRenderPass, nullptr);
		}

		// ─────────────────────────────────────────────────────────────────────────
		//  Initialisation
		// ─────────────────────────────────────────────────────────────────────────

		void VulkanFogFilter::InitRenderPass() {
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
				SPRaise("Failed to create fog filter sampler");
		}

		void VulkanFogFilter::InitDescriptorSetLayout() {
			VkDevice dev = device->GetDevice();

			VkDescriptorSetLayoutBinding bindings[3]{};
			for (int i = 0; i < 3; ++i) {
				bindings[i].binding         = static_cast<uint32_t>(i);
				bindings[i].descriptorCount = 1;
				bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
			}

			VkDescriptorSetLayoutCreateInfo info{};
			info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			info.bindingCount = 3;
			info.pBindings    = bindings;

			if (vkCreateDescriptorSetLayout(dev, &info, nullptr, &triSamplerDSL) != VK_SUCCESS)
				SPRaise("Failed to create fog filter descriptor set layout");
		}

		VkShaderModule VulkanFogFilter::LoadSPIRV(const char* path) {
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

		void VulkanFogFilter::InitPipeline() {
			VkDevice      dev   = device->GetDevice();
			VkPipelineCache cache = renderer.GetPipelineCache();

			VkShaderModule vs = LoadSPIRV("Shaders/Vulkan/PostFilters/Fog2.vk.vs.spv");
			VkShaderModule fs = LoadSPIRV("Shaders/Vulkan/PostFilters/Fog2.vk.fs.spv");

			// Pipeline layout: triSamplerDSL + 128-byte push constants (VS+FS)
			VkPushConstantRange pcr{};
			pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			pcr.offset     = 0;
			pcr.size       = sizeof(FogPushConstants);

			VkPipelineLayoutCreateInfo li{};
			li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			li.setLayoutCount         = 1;
			li.pSetLayouts            = &triSamplerDSL;
			li.pushConstantRangeCount = 1;
			li.pPushConstantRanges    = &pcr;
			if (vkCreatePipelineLayout(dev, &li, nullptr, &fogLayout) != VK_SUCCESS)
				SPRaise("Failed to create fog pipeline layout");

			// Fixed pipeline state
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
			pi.layout              = fogLayout;
			pi.renderPass          = ppRenderPass;
			pi.subpass             = 0;

			if (vkCreateGraphicsPipelines(dev, cache, 1, &pi, nullptr, &fogPipeline) != VK_SUCCESS)
				SPRaise("Failed to create fog pipeline");

			vkDestroyShaderModule(dev, vs, nullptr);
			vkDestroyShaderModule(dev, fs, nullptr);
		}

		void VulkanFogFilter::InitDescriptorPools() {
			VkDevice dev = device->GetDevice();

			// One descriptor set per frame, 3 combined-image-sampler bindings each.
			VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8};
			VkDescriptorPoolCreateInfo info{};
			info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			info.poolSizeCount = 1;
			info.pPoolSizes    = &size;
			info.maxSets       = 4;

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				if (vkCreateDescriptorPool(dev, &info, nullptr, &perFrameDescPool[i]) != VK_SUCCESS)
					SPRaise("Failed to create fog descriptor pool");
			}
		}

		// ─────────────────────────────────────────────────────────────────────────
		//  Per-frame helpers
		// ─────────────────────────────────────────────────────────────────────────

		VkFramebuffer VulkanFogFilter::MakeFramebuffer(VulkanImage* image, int frameSlot) {
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
				SPRaise("Failed to create fog framebuffer");
			perFrameFramebuffers[frameSlot].push_back(fb);
			return fb;
		}

		VkDescriptorSet VulkanFogFilter::BindTextures(int           frameSlot,
		                                               VkImageView   colorView,
		                                               VkImageView   depthView,
		                                               VkSampler     depthSampler,
		                                               VkImageView   shadowView,
		                                               VkSampler     shadowSampler) {
			VkDescriptorSetAllocateInfo ai{};
			ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			ai.descriptorPool     = perFrameDescPool[frameSlot];
			ai.descriptorSetCount = 1;
			ai.pSetLayouts        = &triSamplerDSL;
			VkDescriptorSet set;
			if (vkAllocateDescriptorSets(device->GetDevice(), &ai, &set) != VK_SUCCESS)
				SPRaise("Failed to allocate fog descriptor set");

			VkDescriptorImageInfo imgs[3]{
			    {colorSampler,  colorView,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			    {depthSampler,  depthView,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			    {shadowSampler, shadowView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
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

		// ─────────────────────────────────────────────────────────────────────────
		//  Filter()
		// ─────────────────────────────────────────────────────────────────────────

		void VulkanFogFilter::Filter(VkCommandBuffer cmd,
		                              VulkanImage*    input,
		                              VulkanImage*    output) {
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

			// ── Build push constants ──────────────────────────────────────────────

			const client::SceneDefinition& def = renderer.GetSceneDef();

			// View matrix (no translation) × Vulkan projection → then map NDC to UV+depth
			Matrix4 viewMat = def.ToViewMatrix();
			viewMat.m[12]   = 0.0F;
			viewMat.m[13]   = 0.0F;
			viewMat.m[14]   = 0.0F;

			Matrix4 projMat   = renderer.GetProjectionMatrix(); // Vulkan: z ∈ [0,1]
			Matrix4 vp        = projMat * viewMat;

			// Map Vulkan NDC (x,y ∈ [-1,1], z ∈ [0,1]) to UV+depth ([0,1]³):
			//   u = (x+1)*0.5,  v = (y+1)*0.5,  depth = z  (unchanged)
			vp = Matrix4::Translate(1.0F, 1.0F, 0.0F) * vp;
			vp = Matrix4::Scale(0.5F, 0.5F, 1.0F)     * vp;

			Matrix4 vpInv = vp.Inversed();

			Vector3 fogCol = renderer.GetFogColor();
			fogCol *= fogCol; // linearise (GL_SRGB scene, match GL renderer)

			// Mirrors GLFogFilter2 sunlight/ambient scale computation.
			constexpr float sunlightBrightness  = 0.6F;
			constexpr float ambientBrightness   = 1.0F;

			auto fogTransmission1 = [&](float f) {
				return f / (sunlightBrightness + ambientBrightness * f + 1.0e-6F);
			};
			Vector3 ft{fogTransmission1(fogCol.x),
			           fogTransmission1(fogCol.y),
			           fogTransmission1(fogCol.z)};

			FogPushConstants pc{};

			// mat4 (column-major, matching Matrix4 storage)
			std::memcpy(pc.viewProjInv, vpInv.m, sizeof(pc.viewProjInv));

			pc.viewOriginFogDist[0] = def.viewOrigin.x;
			pc.viewOriginFogDist[1] = def.viewOrigin.y;
			pc.viewOriginFogDist[2] = def.viewOrigin.z;
			pc.viewOriginFogDist[3] = renderer.GetFogDistance();

			pc.sunlightScale[0] = ft.x * sunlightBrightness;
			pc.sunlightScale[1] = ft.y * sunlightBrightness;
			pc.sunlightScale[2] = ft.z * sunlightBrightness;

			pc.ambientScale[0] = ft.x * fogCol.x * ambientBrightness;
			pc.ambientScale[1] = ft.y * fogCol.y * ambientBrightness;
			pc.ambientScale[2] = ft.z * fogCol.z * ambientBrightness;

			// Per-frame dither seed — cycle 4 offsets like GL renderer.
			std::uint32_t frame = frameCounter++ % 4;
			pc.ditherFrame[0] = (float)(frame & 1) * 0.5F;
			pc.ditherFrame[1] = (float)((frame >> 1) & 1) * 0.5F;

			// ── Gather image views ────────────────────────────────────────────────

			Handle<VulkanImage> depthImg  = renderer.GetFramebufferManager()->GetDepthImage();
			VulkanImage*        shadowImg = renderer.GetMapShadowRenderer()->GetShadowImage();

			VkDescriptorSet ds = BindTextures(frameSlot,
			    input->GetImageView(),
			    depthImg->GetImageView(), depthImg->GetSampler(),
			    shadowImg->GetImageView(), shadowImg->GetSampler());

			// ── Record draw ───────────────────────────────────────────────────────

			uint32_t rw = static_cast<uint32_t>(output->GetWidth());
			uint32_t rh = static_cast<uint32_t>(output->GetHeight());

			VkFramebuffer fb = MakeFramebuffer(output, frameSlot);

			VkClearValue cv{};
			cv.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

			VkRenderPassBeginInfo rpBegin{};
			rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			rpBegin.renderPass        = ppRenderPass;
			rpBegin.framebuffer       = fb;
			rpBegin.renderArea.extent = {rw, rh};
			rpBegin.clearValueCount   = 1;
			rpBegin.pClearValues      = &cv;

			vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport{0.0f, 0.0f, (float)rw, (float)rh, 0.0f, 1.0f};
			VkRect2D   scissor{{0, 0}, {rw, rh}};
			vkCmdSetViewport(cmd, 0, 1, &viewport);
			vkCmdSetScissor(cmd, 0, 1, &scissor);

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fogPipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			                        fogLayout, 0, 1, &ds, 0, nullptr);
			vkCmdPushConstants(cmd, fogLayout,
			                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			                   0, sizeof(pc), &pc);
			vkCmdDraw(cmd, 3, 1, 0, 0);

			vkCmdEndRenderPass(cmd);
		}

	} // namespace draw
} // namespace spades
