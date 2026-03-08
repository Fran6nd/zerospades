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

#pragma once

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>
#include "VulkanPostProcessFilter.h"

namespace spades {
	namespace draw {

		// Fog filter (Fog2 style).
		//
		// Algorithm (mirrors GLFogFilter2):
		//   Raymarches 16 samples along each view ray to accumulate in-scattered
		//   sunlight from the shadow map, compositing the result onto the scene
		//   colour image.  Ambient term is a full-coverage approximation (no
		//   ambientShadowTexture yet).
		//
		// Descriptor bindings (set 0):
		//   0 — colorTexture    (combined sampler, SHADER_READ_ONLY)
		//   1 — depthTexture    (combined sampler, SHADER_READ_ONLY, depth aspect)
		//   2 — shadowMapTexture (combined sampler, SHADER_READ_ONLY)
		//
		// Call Filter(cmd, input, output).  input must be in SHADER_READ_ONLY_OPTIMAL;
		// output ends up in SHADER_READ_ONLY_OPTIMAL.  The depth image and the shadow
		// map image must also be in SHADER_READ_ONLY_OPTIMAL on entry.

		class VulkanFogFilter : public VulkanPostProcessFilter {

			VkFormat colorFormat;
			VkSampler colorSampler; // linear, clamp-to-edge

			VkRenderPass ppRenderPass;

			VkDescriptorSetLayout triSamplerDSL; // bindings 0, 1, 2

			VkPipelineLayout fogLayout;
			VkPipeline       fogPipeline;

			static constexpr int MAX_FRAME_SLOTS = 2;
			VkDescriptorPool perFrameDescPool[MAX_FRAME_SLOTS];
			std::vector<VkFramebuffer> perFrameFramebuffers[MAX_FRAME_SLOTS];

			std::uint32_t frameCounter = 0;

			void InitRenderPass();
			void InitDescriptorSetLayout();
			void InitPipeline();
			void InitDescriptorPools();

			VkShaderModule LoadSPIRV(const char* path);

			VkFramebuffer MakeFramebuffer(VulkanImage* image, int frameSlot);

			VkDescriptorSet BindTextures(int frameSlot,
			                             VkImageView colorView,
			                             VkImageView depthView,
			                             VkSampler   depthSampler,
			                             VkImageView shadowView,
			                             VkSampler   shadowSampler);

			void CreatePipeline() override {}
			void CreateRenderPass() override {}

		public:
			VulkanFogFilter(VulkanRenderer& renderer);
			~VulkanFogFilter();

			void Filter(VkCommandBuffer cmd,
			            VulkanImage* input, VulkanImage* output) override;
		};

	} // namespace draw
} // namespace spades
