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

#include "VulkanSSAOFilter.h"
#include "VulkanFramebufferManager.h"
#include "VulkanImage.h"
#include "VulkanImageWrapper.h"
#include "VulkanRenderer.h"
#include "VulkanRenderPassUtils.h"
#include "VulkanTemporaryImagePool.h"
#include <Client/SceneDefinition.h>
#include <Core/Bitmap.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Gui/SDLVulkanDevice.h>
#include <cmath>
#include <cstring>

namespace spades {
    namespace draw {

        // ── Push constant structs ────────────────────────────────────────────────

        struct SSAOPushConstants {
            float zNear, zFar;
            float tanHalfFovX, tanHalfFovY;
            float pixelShiftX, pixelShiftY;
            float worldRadius;
            float angleBias;
            float strength;
            float _pad[3]; // 48 bytes total, 16-byte alignment satisfied
        };
        static_assert(sizeof(SSAOPushConstants) == 48, "");

        struct BilateralPushConstants {
            float unitShiftX, unitShiftY;
            float zNear, zFar;
        };
        static_assert(sizeof(BilateralPushConstants) == 16, "");

        // ── Construction / destruction ───────────────────────────────────────────

        VulkanSSAOFilter::VulkanSSAOFilter(VulkanRenderer& r)
            : renderer(r),
              device(r.GetDevice()),
              colorFormat(VK_FORMAT_UNDEFINED),
              rpSSAO(VK_NULL_HANDLE),
              rpApply(VK_NULL_HANDLE),
              dslTwoSamplers(VK_NULL_HANDLE),
              dslOneSampler(VK_NULL_HANDLE),
              layoutSSAO(VK_NULL_HANDLE),
              layoutBilateral(VK_NULL_HANDLE),
              layoutApply(VK_NULL_HANDLE),
              pipelineSSAO(VK_NULL_HANDLE),
              pipelineBilateral(VK_NULL_HANDLE),
              pipelineApply(VK_NULL_HANDLE),
              samplerNearest(VK_NULL_HANDLE),
              samplerLinear(VK_NULL_HANDLE) {
            SPADES_MARK_FUNCTION();

            for (int i = 0; i < MAX_FRAME_SLOTS; ++i)
                perFramePool[i] = VK_NULL_HANDLE;

            colorFormat = r.GetFramebufferManager()->GetMainColorFormat();

            InitRenderPasses();
            InitDescriptorSetLayouts();
            InitSamplers();
            InitPipelines();
            InitDescriptorPools();
            InitNoiseTexture();
        }

        VulkanSSAOFilter::~VulkanSSAOFilter() {
            SPADES_MARK_FUNCTION();
            VkDevice dev = device->GetDevice();

            for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
                for (VkFramebuffer fb : perFrameFBs[i])
                    vkDestroyFramebuffer(dev, fb, nullptr);
                if (perFramePool[i] != VK_NULL_HANDLE)
                    vkDestroyDescriptorPool(dev, perFramePool[i], nullptr);
            }

            noiseImage = Handle<VulkanImage>();

            if (pipelineApply    != VK_NULL_HANDLE) vkDestroyPipeline(dev, pipelineApply, nullptr);
            if (pipelineBilateral!= VK_NULL_HANDLE) vkDestroyPipeline(dev, pipelineBilateral, nullptr);
            if (pipelineSSAO     != VK_NULL_HANDLE) vkDestroyPipeline(dev, pipelineSSAO, nullptr);
            if (layoutApply      != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, layoutApply, nullptr);
            if (layoutBilateral  != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, layoutBilateral, nullptr);
            if (layoutSSAO       != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, layoutSSAO, nullptr);
            if (dslTwoSamplers   != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, dslTwoSamplers, nullptr);
            if (dslOneSampler    != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, dslOneSampler, nullptr);
            if (samplerLinear    != VK_NULL_HANDLE) vkDestroySampler(dev, samplerLinear, nullptr);
            if (samplerNearest   != VK_NULL_HANDLE) vkDestroySampler(dev, samplerNearest, nullptr);
            if (rpApply          != VK_NULL_HANDLE) vkDestroyRenderPass(dev, rpApply, nullptr);
            if (rpSSAO           != VK_NULL_HANDLE) vkDestroyRenderPass(dev, rpSSAO, nullptr);
        }

        // ── Initialisation ───────────────────────────────────────────────────────

        VkShaderModule VulkanSSAOFilter::LoadSPIRV(const char* path) {
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

        void VulkanSSAOFilter::InitRenderPasses() {
            VkDevice dev = device->GetDevice();

            // Common subpass dependencies: previous colour write → this fragment read,
            // and this colour write → next fragment read.
            VkSubpassDependency deps[2]{};
            deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
            deps[0].dstSubpass    = 0;
            deps[0].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            deps[0].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            deps[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            deps[1].srcSubpass    = 0;
            deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
            deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            // R8_UNORM render pass (SSAO raw + bilateral)
            {
                VkAttachmentDescription att{};
                att.format         = SSAO_FORMAT;
                att.samples        = VK_SAMPLE_COUNT_1_BIT;
                att.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
                att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
                att.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
                VkSubpassDescription  sub{};
                sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
                sub.colorAttachmentCount = 1;
                sub.pColorAttachments    = &ref;

                VkRenderPassCreateInfo rpi{};
                rpi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
                rpi.attachmentCount = 1;
                rpi.pAttachments    = &att;
                rpi.subpassCount    = 1;
                rpi.pSubpasses      = &sub;
                rpi.dependencyCount = 2;
                rpi.pDependencies   = deps;

                if (vkCreateRenderPass(dev, &rpi, nullptr, &rpSSAO) != VK_SUCCESS)
                    SPRaise("Failed to create SSAO render pass");
            }

            // Colour render pass (apply / composite)
            {
                VkAttachmentDescription att{};
                att.format         = colorFormat;
                att.samples        = VK_SAMPLE_COUNT_1_BIT;
                att.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
                att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
                att.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
                VkSubpassDescription  sub{};
                sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
                sub.colorAttachmentCount = 1;
                sub.pColorAttachments    = &ref;

                VkRenderPassCreateInfo rpi{};
                rpi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
                rpi.attachmentCount = 1;
                rpi.pAttachments    = &att;
                rpi.subpassCount    = 1;
                rpi.pSubpasses      = &sub;
                rpi.dependencyCount = 2;
                rpi.pDependencies   = deps;

                if (vkCreateRenderPass(dev, &rpi, nullptr, &rpApply) != VK_SUCCESS)
                    SPRaise("Failed to create SSAO apply render pass");
            }
        }

        void VulkanSSAOFilter::InitDescriptorSetLayouts() {
            VkDevice dev = device->GetDevice();
            auto MakeDSL = [&](uint32_t count, VkDescriptorSetLayout& out) {
                VkDescriptorSetLayoutBinding bindings[2]{};
                for (uint32_t i = 0; i < count; ++i) {
                    bindings[i].binding         = i;
                    bindings[i].descriptorCount = 1;
                    bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
                }
                VkDescriptorSetLayoutCreateInfo info{};
                info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                info.bindingCount = count;
                info.pBindings    = bindings;
                if (vkCreateDescriptorSetLayout(dev, &info, nullptr, &out) != VK_SUCCESS)
                    SPRaise("Failed to create SSAO descriptor set layout");
            };
            MakeDSL(2, dslTwoSamplers);
            MakeDSL(1, dslOneSampler);
        }

        void VulkanSSAOFilter::InitSamplers() {
            VkDevice dev = device->GetDevice();
            auto MakeSampler = [&](VkFilter filter, VkSampler& out) {
                VkSamplerCreateInfo si{};
                si.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                si.magFilter     = filter;
                si.minFilter     = filter;
                si.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                si.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                si.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                si.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                if (vkCreateSampler(dev, &si, nullptr, &out) != VK_SUCCESS)
                    SPRaise("Failed to create SSAO sampler");
            };
            MakeSampler(VK_FILTER_NEAREST, samplerNearest);
            MakeSampler(VK_FILTER_LINEAR,  samplerLinear);
        }

        static VkPipelineLayout MakeLayout(VkDevice dev,
                                           VkDescriptorSetLayout dsl,
                                           uint32_t pcSize,
                                           VkShaderStageFlagBits pcStage) {
            VkPushConstantRange pcr{};
            pcr.stageFlags = pcStage;
            pcr.offset     = 0;
            pcr.size       = pcSize;

            VkPipelineLayoutCreateInfo li{};
            li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            li.setLayoutCount         = 1;
            li.pSetLayouts            = &dsl;
            li.pushConstantRangeCount = (pcSize > 0) ? 1 : 0;
            li.pPushConstantRanges    = (pcSize > 0) ? &pcr : nullptr;

            VkPipelineLayout layout;
            if (vkCreatePipelineLayout(dev, &li, nullptr, &layout) != VK_SUCCESS)
                SPRaise("Failed to create pipeline layout");
            return layout;
        }

        void VulkanSSAOFilter::InitPipelines() {
            VkDevice dev      = device->GetDevice();
            VkPipelineCache cache = renderer.GetPipelineCache();

            VkShaderModule vs = LoadSPIRV("Shaders/Vulkan/PostFilters/PassThrough.vk.vs.spv");

            VkShaderModule fsSSAO       = LoadSPIRV("Shaders/Vulkan/PostFilters/SSAO.vk.fs.spv");
            VkShaderModule fsBilateral  = LoadSPIRV("Shaders/Vulkan/PostFilters/SSAOBilateral.vk.fs.spv");
            VkShaderModule fsApply      = LoadSPIRV("Shaders/Vulkan/PostFilters/SSAOApply.vk.fs.spv");

            layoutSSAO      = MakeLayout(dev, dslTwoSamplers, sizeof(SSAOPushConstants),    VK_SHADER_STAGE_FRAGMENT_BIT);
            layoutBilateral = MakeLayout(dev, dslTwoSamplers, sizeof(BilateralPushConstants),VK_SHADER_STAGE_FRAGMENT_BIT);
            layoutApply     = MakeLayout(dev, dslTwoSamplers, 0, VK_SHADER_STAGE_FRAGMENT_BIT);

            auto MakePipeline = [&](VkShaderModule fs, VkPipelineLayout layout,
                                    VkRenderPass rp, VkPipeline& out) {
                VkPipelineShaderStageCreateInfo stages[2]{};
                stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                              VK_SHADER_STAGE_VERTEX_BIT,   vs, "main", nullptr};
                stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                              VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};

                VkPipelineVertexInputStateCreateInfo vi{};
                vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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

                VkGraphicsPipelineCreateInfo pi{};
                pi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pi.stageCount          = 2;
                pi.pStages             = stages;
                pi.pVertexInputState   = &vi;
                pi.pInputAssemblyState = &ia;
                pi.pViewportState      = &vp;
                pi.pRasterizationState = &rs;
                pi.pMultisampleState   = &ms;
                pi.pDepthStencilState  = &dss;
                pi.pColorBlendState    = &blend;
                pi.pDynamicState       = &dyn;
                pi.layout              = layout;
                pi.renderPass          = rp;
                pi.subpass             = 0;

                if (vkCreateGraphicsPipelines(dev, cache, 1, &pi, nullptr, &out) != VK_SUCCESS)
                    SPRaise("Failed to create SSAO pipeline");
            };

            MakePipeline(fsSSAO,      layoutSSAO,      rpSSAO,  pipelineSSAO);
            MakePipeline(fsBilateral, layoutBilateral, rpSSAO,  pipelineBilateral);
            MakePipeline(fsApply,     layoutApply,     rpApply, pipelineApply);

            vkDestroyShaderModule(dev, fsApply,     nullptr);
            vkDestroyShaderModule(dev, fsBilateral, nullptr);
            vkDestroyShaderModule(dev, fsSSAO,      nullptr);
            vkDestroyShaderModule(dev, vs,          nullptr);
        }

        void VulkanSSAOFilter::InitDescriptorPools() {
            VkDevice dev = device->GetDevice();
            // Each frame slot needs up to ~6 descriptor sets (raw, bil-H, bil-V, apply)
            // with up to 2 bindings each.
            VkDescriptorPoolSize sz{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16};
            VkDescriptorPoolCreateInfo info{};
            info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            info.poolSizeCount = 1;
            info.pPoolSizes    = &sz;
            info.maxSets       = 8;
            for (int i = 0; i < MAX_FRAME_SLOTS; ++i)
                if (vkCreateDescriptorPool(dev, &info, nullptr, &perFramePool[i]) != VK_SUCCESS)
                    SPRaise("Failed to create SSAO descriptor pool");
        }

        void VulkanSSAOFilter::InitNoiseTexture() {
            // Build a 4x4 R8_UNORM texture containing dithered random rotation angles.
            // Using the Bayer ordered-dither matrix scaled to [0,255] gives a low-discrepancy
            // distribution that breaks up the visible spiral pattern of the HBAO sampling.
            static const uint8_t bayer4x4[16] = {
                0,   136,  34, 170,
                204,  68, 238, 102,
                51,  187,  17, 153,
                255, 119, 221,  85
            };

            // Create a small bitmap to hand to VulkanRenderer::CreateImage
            Handle<Bitmap> bmp(new Bitmap(4, 4), false);
            uint32_t* pixels = reinterpret_cast<uint32_t*>(bmp->GetPixels());
            for (int i = 0; i < 16; ++i) {
                uint8_t v = bayer4x4[i];
                // Pack as RGBA8: R = noise, GBA = 0
                pixels[i] = static_cast<uint32_t>(v);          // R
                pixels[i] |= (static_cast<uint32_t>(v) << 8);  // G (ignored by shader)
                pixels[i] |= (static_cast<uint32_t>(v) << 16); // B
                pixels[i] |= (0xFFu << 24);                    // A
            }

            // CreateImage uploads the bitmap and returns a VulkanImageWrapper-backed IImage.
            Handle<client::IImage> img = renderer.CreateImage(*bmp);
            VulkanImageWrapper* wrapper = dynamic_cast<VulkanImageWrapper*>(img.GetPointerOrNull());
            if (!wrapper)
                SPRaise("VulkanSSAOFilter: failed to get VulkanImageWrapper for noise texture");
            noiseImage = Handle<VulkanImage>(wrapper->GetVulkanImage());
            if (!noiseImage)
                SPRaise("VulkanSSAOFilter: failed to get VulkanImage for noise texture");
        }

        // ── Per-frame helpers ────────────────────────────────────────────────────

        int VulkanSSAOFilter::BeginFrame() {
            int slot = static_cast<int>(renderer.GetCurrentFrameIndex()) % MAX_FRAME_SLOTS;
            VkDevice dev = device->GetDevice();
            for (VkFramebuffer fb : perFrameFBs[slot])
                vkDestroyFramebuffer(dev, fb, nullptr);
            perFrameFBs[slot].clear();
            vkResetDescriptorPool(dev, perFramePool[slot], 0);
            return slot;
        }

        VkFramebuffer VulkanSSAOFilter::MakeFramebuffer(VkRenderPass rp, VulkanImage* img, int slot) {
            VkImageView view = img->GetImageView();
            VkFramebufferCreateInfo info{};
            info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            info.renderPass      = rp;
            info.attachmentCount = 1;
            info.pAttachments    = &view;
            info.width           = static_cast<uint32_t>(img->GetWidth());
            info.height          = static_cast<uint32_t>(img->GetHeight());
            info.layers          = 1;
            VkFramebuffer fb;
            if (vkCreateFramebuffer(device->GetDevice(), &info, nullptr, &fb) != VK_SUCCESS)
                SPRaise("Failed to create SSAO framebuffer");
            perFrameFBs[slot].push_back(fb);
            return fb;
        }

        VkDescriptorSet VulkanSSAOFilter::BindImages(int slot,
                                                     VkDescriptorSetLayout layout,
                                                     VulkanImage* img0, VkSampler s0,
                                                     VulkanImage* img1, VkSampler s1) {
            VkDevice dev = device->GetDevice();

            VkDescriptorSetAllocateInfo ai{};
            ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool     = perFramePool[slot];
            ai.descriptorSetCount = 1;
            ai.pSetLayouts        = &layout;
            VkDescriptorSet ds;
            if (vkAllocateDescriptorSets(dev, &ai, &ds) != VK_SUCCESS)
                SPRaise("Failed to allocate SSAO descriptor set");

            VkDescriptorImageInfo imgInfos[2]{};
            uint32_t count = 0;

            if (img0) {
                imgInfos[count].sampler     = s0;
                imgInfos[count].imageView   = img0->GetImageView();
                imgInfos[count].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                ++count;
            }
            if (img1) {
                imgInfos[count].sampler     = s1;
                imgInfos[count].imageView   = img1->GetImageView();
                imgInfos[count].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                ++count;
            }

            VkWriteDescriptorSet writes[2]{};
            for (uint32_t i = 0; i < count; ++i) {
                writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet          = ds;
                writes[i].dstBinding      = i;
                writes[i].descriptorCount = 1;
                writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[i].pImageInfo      = &imgInfos[i];
            }
            vkUpdateDescriptorSets(dev, count, writes, 0, nullptr);
            return ds;
        }

        void VulkanSSAOFilter::BarrierColorToShaderRead(VkCommandBuffer cmd, VulkanImage* img) {
            VkImageMemoryBarrier b{};
            b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image               = img->GetImage();
            b.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            b.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            b.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &b);
        }

        void VulkanSSAOFilter::DrawFullscreen(VkCommandBuffer cmd,
                                              VkRenderPass rp, VkFramebuffer fb,
                                              uint32_t w, uint32_t h,
                                              VkPipeline pipeline, VkPipelineLayout layout,
                                              VkDescriptorSet ds,
                                              const void* pushData, uint32_t pushSize) {
            VkRenderPassBeginInfo rpInfo{};
            rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpInfo.renderPass      = rp;
            rpInfo.framebuffer     = fb;
            rpInfo.renderArea      = {{0, 0}, {w, h}};
            rpInfo.clearValueCount = 0;
            vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport vp{0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f};
            vkCmdSetViewport(cmd, 0, 1, &vp);
            VkRect2D sc{{0,0},{w,h}};
            vkCmdSetScissor(cmd, 0, 1, &sc);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    layout, 0, 1, &ds, 0, nullptr);
            if (pushData && pushSize > 0)
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, pushSize, pushData);
            vkCmdDraw(cmd, 3, 1, 0, 0);
            vkCmdEndRenderPass(cmd);
        }

        // ── Filter ───────────────────────────────────────────────────────────────

        void VulkanSSAOFilter::Filter(VkCommandBuffer cmd,
                                      VulkanImage* colorIn,
                                      VulkanImage* depthIn,
                                      VulkanImage* colorOut) {
            SPADES_MARK_FUNCTION();

            int slot = BeginFrame();

            const uint32_t w = static_cast<uint32_t>(colorIn->GetWidth());
            const uint32_t h = static_cast<uint32_t>(colorIn->GetHeight());

            // Allocate two temporary R8 images for the SSAO buffer and bilateral ping-pong.
            VulkanTemporaryImagePool* pool = renderer.GetTemporaryImagePool();
            Handle<VulkanImage> ssaoRaw  = pool->Acquire(w, h, SSAO_FORMAT);
            Handle<VulkanImage> ssaoPing = pool->Acquire(w, h, SSAO_FORMAT);

            VulkanImage* rawPtr  = ssaoRaw.GetPointerOrNull();
            VulkanImage* pingPtr = ssaoPing.GetPointerOrNull();

            // Retrieve scene parameters for view-space reconstruction.
            const client::SceneDefinition& def = renderer.GetSceneDef();
            float tanHalfFovX = std::tan(def.fovX * 0.5f);
            float tanHalfFovY = std::tan(def.fovY * 0.5f);

            // AO kernel radius: 1.5 world units, standard for player-scale scenes.
            // Angle bias of ~8° suppresses self-occlusion on flat surfaces.
            SSAOPushConstants ssaoPC{};
            ssaoPC.zNear        = def.zNear;
            ssaoPC.zFar         = def.zFar;
            ssaoPC.tanHalfFovX  = tanHalfFovX;
            ssaoPC.tanHalfFovY  = tanHalfFovY;
            ssaoPC.pixelShiftX  = 1.0f / static_cast<float>(w);
            ssaoPC.pixelShiftY  = 1.0f / static_cast<float>(h);
            ssaoPC.worldRadius  = 1.5f;
            ssaoPC.angleBias    = 0.14f; // ~8°
            ssaoPC.strength     = 1.5f;

            // ── Pass 1: raw SSAO ──────────────────────────────────────────────────
            {
                VkDescriptorSet ds = BindImages(slot, dslTwoSamplers,
                    depthIn,    samplerNearest,
                    noiseImage.GetPointerOrNull(), samplerNearest);
                VkFramebuffer fb   = MakeFramebuffer(rpSSAO, rawPtr, slot);
                DrawFullscreen(cmd, rpSSAO, fb, w, h,
                    pipelineSSAO, layoutSSAO, ds,
                    &ssaoPC, sizeof(ssaoPC));
            }

            // Ensure pass 1 writes are visible before the bilateral reads.
            BarrierColorToShaderRead(cmd, rawPtr);

            BilateralPushConstants bilPC{};
            bilPC.zNear = def.zNear;
            bilPC.zFar  = def.zFar;

            // ── Pass 2: bilateral blur — horizontal ───────────────────────────────
            {
                bilPC.unitShiftX = 1.0f / static_cast<float>(w);
                bilPC.unitShiftY = 0.0f;
                VkDescriptorSet ds = BindImages(slot, dslTwoSamplers,
                    rawPtr,  samplerNearest,
                    depthIn, samplerNearest);
                VkFramebuffer fb   = MakeFramebuffer(rpSSAO, pingPtr, slot);
                DrawFullscreen(cmd, rpSSAO, fb, w, h,
                    pipelineBilateral, layoutBilateral, ds,
                    &bilPC, sizeof(bilPC));
            }

            BarrierColorToShaderRead(cmd, pingPtr);

            // ── Pass 3: bilateral blur — vertical ─────────────────────────────────
            {
                bilPC.unitShiftX = 0.0f;
                bilPC.unitShiftY = 1.0f / static_cast<float>(h);
                VkDescriptorSet ds = BindImages(slot, dslTwoSamplers,
                    pingPtr, samplerNearest,
                    depthIn, samplerNearest);
                VkFramebuffer fb   = MakeFramebuffer(rpSSAO, rawPtr, slot);
                DrawFullscreen(cmd, rpSSAO, fb, w, h,
                    pipelineBilateral, layoutBilateral, ds,
                    &bilPC, sizeof(bilPC));
            }

            BarrierColorToShaderRead(cmd, rawPtr);

            // ── Pass 4: apply — multiply occlusion onto colour ────────────────────
            {
                VkDescriptorSet ds = BindImages(slot, dslTwoSamplers,
                    colorIn, samplerLinear,
                    rawPtr,  samplerNearest);
                VkFramebuffer fb   = MakeFramebuffer(rpApply, colorOut, slot);
                DrawFullscreen(cmd, rpApply, fb, w, h,
                    pipelineApply, layoutApply, ds,
                    nullptr, 0);
            }

            pool->Return(rawPtr);
            pool->Return(pingPtr);
        }

    } // namespace draw
} // namespace spades
