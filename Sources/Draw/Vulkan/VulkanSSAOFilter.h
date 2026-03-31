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
#include <Core/RefCountedObject.h>

namespace spades {
    namespace gui { class SDLVulkanDevice; }
    namespace draw {

        class VulkanRenderer;
        class VulkanImage;

        // HBAO post-process filter.
        //
        // Three-pass implementation:
        //   1. SSAO raw pass    — depth + noise  → R8 occlusion image
        //   2. Bilateral blur H — ssao + depth   → R8 blurred (horizontal)
        //   3. Bilateral blur V — ssao + depth   → R8 blurred (vertical)
        //   4. Apply / composite— color + ssao   → output colour
        //
        // The filter has a non-standard signature (takes an extra depth image) and does
        // not inherit VulkanPostProcessFilter.  It is called directly from RecordCommandBuffer
        // rather than through the generic RunFilter lambda.
        //
        // Both colorIn and depthIn must be in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        // on entry.  colorOut is in UNDEFINED (acquired from the temporary pool).
        // On exit, colorOut is in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL.

        class VulkanSSAOFilter {
        public:
            VulkanSSAOFilter(VulkanRenderer& renderer);
            ~VulkanSSAOFilter();

            // Filter the scene colour using HBAO.
            // colorIn  — scene colour (SHADER_READ_ONLY_OPTIMAL)
            // depthIn  — scene depth  (SHADER_READ_ONLY_OPTIMAL)
            // colorOut — output image (UNDEFINED; finalLayout = SHADER_READ_ONLY_OPTIMAL)
            void Filter(VkCommandBuffer cmd,
                        VulkanImage* colorIn,
                        VulkanImage* depthIn,
                        VulkanImage* colorOut);

        private:
            VulkanRenderer&               renderer;
            Handle<gui::SDLVulkanDevice>  device;

            // Formats
            VkFormat colorFormat;          // colour framebuffer format
            static constexpr VkFormat SSAO_FORMAT = VK_FORMAT_R8_UNORM;

            // Render passes
            VkRenderPass rpSSAO;           // outputs R8_UNORM (SSAO raw + bilateral)
            VkRenderPass rpApply;          // outputs colour format

            // Descriptor set layouts
            VkDescriptorSetLayout dslTwoSamplers;   // set 0: binding 0+1 (sampler2D)
            VkDescriptorSetLayout dslOneSampler;    // set 0: binding 0   (sampler2D)

            // Pipelines
            VkPipelineLayout layoutSSAO;
            VkPipelineLayout layoutBilateral;
            VkPipelineLayout layoutApply;

            VkPipeline pipelineSSAO;
            VkPipeline pipelineBilateral;
            VkPipeline pipelineApply;

            // Samplers
            VkSampler samplerNearest;   // for depth and SSAO reads (nearest)
            VkSampler samplerLinear;    // for colour reads

            // 4x4 tiled rotation noise texture (R8_UNORM, generated in constructor)
            Handle<VulkanImage> noiseImage;

            // Per-frame descriptor pools (reset each frame slot)
            static constexpr int MAX_FRAME_SLOTS = 2;
            VkDescriptorPool perFramePool[MAX_FRAME_SLOTS];
            std::vector<VkFramebuffer> perFrameFBs[MAX_FRAME_SLOTS];

            // ── Initialisation helpers ───────────────────────────────────────────
            VkShaderModule LoadSPIRV(const char* path);
            void InitRenderPasses();
            void InitDescriptorSetLayouts();
            void InitPipelines();
            void InitSamplers();
            void InitDescriptorPools();
            void InitNoiseTexture();

            // ── Per-frame helpers ────────────────────────────────────────────────
            // Reset this slot's transient resources and return the slot index.
            int BeginFrame();

            VkFramebuffer MakeFramebuffer(VkRenderPass rp, VulkanImage* img, int slot);

            // Allocate a descriptor set from perFramePool[slot] using the given layout
            // and bind up to two sampler images (pass VK_NULL_HANDLE for unused images).
            VkDescriptorSet BindImages(int slot,
                                       VkDescriptorSetLayout layout,
                                       VulkanImage* img0, VkSampler s0,
                                       VulkanImage* img1, VkSampler s1);

            // Emit a pipeline barrier that flushes colour attachment writes and makes
            // them visible as shader reads — used between consecutive filter passes.
            static void BarrierColorToShaderRead(VkCommandBuffer cmd, VulkanImage* img);

            // Fullscreen draw helper: begin render pass, set viewport+scissor, bind
            // pipeline, bind descriptor set, push constants, draw 3 verts, end pass.
            void DrawFullscreen(VkCommandBuffer cmd,
                                VkRenderPass rp,
                                VkFramebuffer fb,
                                uint32_t width, uint32_t height,
                                VkPipeline pipeline,
                                VkPipelineLayout layout,
                                VkDescriptorSet ds,
                                const void* pushData, uint32_t pushSize);
        };

    } // namespace draw
} // namespace spades
