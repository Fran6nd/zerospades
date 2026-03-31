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

#pragma once

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>
#include "VulkanPostProcessFilter.h"
#include <Core/Math.h>

namespace spades {
	namespace draw {

		// Colour correction post-process filter.
		//
		// Algorithm (mirrors GLColorCorrectionFilter):
		//   - White-balance tint derived from a temporally smoothed fog colour.
		//   - Saturation control via r_saturation and sceneDef.saturation.
		//   - S-curve contrast enhancement.
		//   - ACES tone mapping + linearise/delinearise when r_hdr is active.
		//
		// Descriptor bindings (set 0):
		//   0 — colorTexture (combined sampler, SHADER_READ_ONLY_OPTIMAL)
		//
		// Push constants (fragment stage, 24 bytes):
		//   vec3  tint        [offset  0]
		//   float saturation  [offset 12]
		//   float enhancement [offset 16]
		//   float hdrEnabled  [offset 20]
		//
		// Call Filter(cmd, input, output).  input must be in
		// SHADER_READ_ONLY_OPTIMAL; output ends up in SHADER_READ_ONLY_OPTIMAL.

		class VulkanColorCorrectionFilter : public VulkanPostProcessFilter {

			VkFormat  colorFormat;
			VkSampler colorSampler;

			VkRenderPass ppRenderPass;

			VkDescriptorSetLayout oneSamplerDSL;

			VkPipelineLayout ccLayout;
			VkPipeline       ccPipeline;

			static constexpr int MAX_FRAME_SLOTS = 2;
			VkDescriptorPool           perFrameDescPool[MAX_FRAME_SLOTS];
			std::vector<VkFramebuffer> perFrameFramebuffers[MAX_FRAME_SLOTS];

			// Temporal smoothing of fog colour (mirrors GLRenderer::smoothedFogColor).
			Vector3 smoothedFogColor;
			bool    smoothedFogColorInitialized{false};

			void InitRenderPass();
			void InitDescriptorSetLayout();
			void InitPipeline();
			void InitDescriptorPools();

			VkShaderModule LoadSPIRV(const char* path);

			VkFramebuffer   MakeFramebuffer(VulkanImage* image, int frameSlot);
			VkDescriptorSet BindTexture(int frameSlot, VkImageView colorView);

			void CreatePipeline() override {}
			void CreateRenderPass() override {}

		public:
			VulkanColorCorrectionFilter(VulkanRenderer& renderer);
			~VulkanColorCorrectionFilter();

			void Filter(VkCommandBuffer cmd,
			            VulkanImage* input, VulkanImage* output) override;
		};

	} // namespace draw
} // namespace spades
