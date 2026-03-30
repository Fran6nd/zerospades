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

namespace spades {
	namespace draw {

		// FXAA post-process filter.
		//
		// Single-pass fullscreen anti-aliasing.  Reads the colour image (binding 0)
		// and writes the smoothed result to the output image.
		//
		// Push constants (fragment stage, 8 bytes):
		//   vec2 inverseVP  — {1/width, 1/height} of the input image
		//
		// Descriptor bindings (set 0):
		//   0 — colorTexture (combined sampler, SHADER_READ_ONLY_OPTIMAL)
		//
		// Input image must be in SHADER_READ_ONLY_OPTIMAL on entry.
		// Output image is left in SHADER_READ_ONLY_OPTIMAL on exit.

		class VulkanFXAAFilter : public VulkanPostProcessFilter {

			VkFormat   colorFormat;
			VkSampler  colorSampler;

			VkRenderPass ppRenderPass;

			VkDescriptorSetLayout oneSamplerDSL;  // binding 0 only

			VkPipelineLayout fxaaLayout;
			VkPipeline       fxaaPipeline;

			static constexpr int MAX_FRAME_SLOTS = 2;
			VkDescriptorPool perFrameDescPool[MAX_FRAME_SLOTS];
			std::vector<VkFramebuffer> perFrameFramebuffers[MAX_FRAME_SLOTS];

			std::uint32_t frameCounter = 0;

			void InitRenderPass();
			void InitDescriptorSetLayout();
			void InitPipeline();
			void InitDescriptorPools();

			VkShaderModule LoadSPIRV(const char* path);

			VkFramebuffer  MakeFramebuffer(VulkanImage* image, int frameSlot);
			VkDescriptorSet BindTexture(int frameSlot, VkImageView colorView);

			void CreatePipeline() override {}
			void CreateRenderPass() override {}

		public:
			VulkanFXAAFilter(VulkanRenderer& renderer);
			~VulkanFXAAFilter();

			void Filter(VkCommandBuffer cmd,
			            VulkanImage* input, VulkanImage* output) override;
		};

	} // namespace draw
} // namespace spades
