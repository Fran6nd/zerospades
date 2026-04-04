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

#include "VulkanFramebufferManager.h"
#include "VulkanImage.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/Settings.h>

SPADES_SETTING(r_vk_highPrec);
SPADES_SETTING(r_vk_hdr);
SPADES_SETTING(r_vk_srgb);
SPADES_SETTING(r_vk_multisamples);
namespace spades {
	namespace draw {

		// Picks the highest MSAA sample count from {requested, device maximum} and
		// returns VK_SAMPLE_COUNT_1_BIT if the requested count is not supported.
		static VkSampleCountFlagBits SelectMsaaSampleCount(VkPhysicalDevice physDev, int requested) {
			if (requested <= 1) return VK_SAMPLE_COUNT_1_BIT;

			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties(physDev, &props);
			VkSampleCountFlags supported = props.limits.framebufferColorSampleCounts &
			                               props.limits.framebufferDepthSampleCounts;

			// Try the requested count; fall back to 1 if not supported.
			auto TryCount = [&](VkSampleCountFlagBits bits) -> bool {
				return (supported & bits) != 0;
			};

			if (requested >= 4 && TryCount(VK_SAMPLE_COUNT_4_BIT)) return VK_SAMPLE_COUNT_4_BIT;
			if (requested >= 2 && TryCount(VK_SAMPLE_COUNT_2_BIT)) return VK_SAMPLE_COUNT_2_BIT;

			SPLog("r_vk_multisamples=%d is not supported by this device; falling back to no MSAA", requested);
			return VK_SAMPLE_COUNT_1_BIT;
		}

		VulkanFramebufferManager::VulkanFramebufferManager(Handle<gui::SDLVulkanDevice> dev,
		                                                   int renderWidth, int renderHeight)
		    : device(dev),
		      msaaSamples(VK_SAMPLE_COUNT_1_BIT),
		      doingPostProcessing(false),
		      renderWidth(renderWidth),
		      renderHeight(renderHeight),
		      renderFramebuffer(VK_NULL_HANDLE),
		      renderPass(VK_NULL_HANDLE),
		      spriteRenderPass(VK_NULL_HANDLE),
		      spriteFramebuffer(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			SPLog("Initializing Vulkan framebuffer manager");

			useHighPrec = (bool)(int)r_vk_highPrec;
			useHdr = (bool)(int)r_vk_hdr;
			useSRGB = (bool)(int)r_vk_srgb;

			// Determine color format
			if (useSRGB) {
				SPLog("Using SRGB color format");
				fbColorFormat = VK_FORMAT_R8G8B8A8_SRGB;
				useHighPrec = false;
			} else if (useHdr) {
				SPLog("Using HDR color format");
				fbColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
			} else if (useHighPrec) {
				SPLog("Using high precision color format");
				fbColorFormat = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
			} else {
				SPLog("Using standard RGBA8 color format");
				fbColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
			}

			// Choose depth format: prefer D24_UNORM_S8_UINT, fall back to D32_SFLOAT_S8_UINT
			{
				VkFormatProperties formatProps;
				vkGetPhysicalDeviceFormatProperties(device->GetPhysicalDevice(),
				                                     VK_FORMAT_D24_UNORM_S8_UINT, &formatProps);
				if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
					fbDepthFormat = VK_FORMAT_D24_UNORM_S8_UINT;
					SPLog("Using D24_UNORM_S8_UINT depth format");
				} else {
					fbDepthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
					SPLog("D24_UNORM_S8_UINT not supported, using D32_SFLOAT_S8_UINT");
				}
			}

			// Determine MSAA sample count
			msaaSamples = SelectMsaaSampleCount(device->GetPhysicalDevice(), (int)r_vk_multisamples);
			if (msaaSamples > VK_SAMPLE_COUNT_1_BIT)
				SPLog("MSAA enabled: %d samples", (int)msaaSamples);
			else
				SPLog("MSAA disabled");

			CreateRenderPass();

			VkDevice vkDevice = device->GetDevice();

			// 1-sample resolve targets — always allocated.
			// When MSAA is active these are the resolve targets (not direct render targets).
			SPLog("Creating resolve / non-MSAA color image");
			renderColorImage = Handle<VulkanImage>::New(
			    device, renderWidth, renderHeight, fbColorFormat,
			    VK_IMAGE_TILING_OPTIMAL,
			    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
			        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			renderColorImage->CreateImageView(VK_IMAGE_ASPECT_COLOR_BIT);
			renderColorImage->CreateSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR,
			                                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);

			SPLog("Creating 1-sample depth image (used by post-process filters)");
			renderDepthImage = Handle<VulkanImage>::New(
			    device, renderWidth, renderHeight, fbDepthFormat,
			    VK_IMAGE_TILING_OPTIMAL,
			    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
			        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			renderDepthImage->CreateImageView(VK_IMAGE_ASPECT_DEPTH_BIT);
			renderDepthImage->CreateSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST,
			                                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);

			// When MSAA is enabled, create the multisampled render targets.
			// The MSAA color is resolved into renderColorImage at render-pass end.
			// The MSAA depth is the scene depth buffer; it cannot be sampled in shaders.
			// Features requiring depth sampling (soft particles, fog shadow) are disabled
			// when MSAA is active — see RecordCommandBuffer in VulkanRenderer.cpp.
			if (msaaSamples > VK_SAMPLE_COUNT_1_BIT) {
				SPLog("Creating %d-sample MSAA color image", (int)msaaSamples);
				msaaColorImage = Handle<VulkanImage>::New(
				    device, renderWidth, renderHeight, fbColorFormat,
				    VK_IMAGE_TILING_OPTIMAL,
				    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
				    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				    msaaSamples);
				msaaColorImage->CreateImageView(VK_IMAGE_ASPECT_COLOR_BIT);

				SPLog("Creating %d-sample MSAA depth image", (int)msaaSamples);
				msaaDepthImage = Handle<VulkanImage>::New(
				    device, renderWidth, renderHeight, fbDepthFormat,
				    VK_IMAGE_TILING_OPTIMAL,
				    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
				    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				    msaaSamples);
				msaaDepthImage->CreateImageView(VK_IMAGE_ASPECT_DEPTH_BIT);
			}

			// Build the main render framebuffer.
			// Non-MSAA: [renderColor, renderDepth]
			// MSAA:     [msaaColor, msaaDepth, renderColor(resolve)]
			SPLog("Creating main render framebuffer");
			{
				VkImageView attachments[3];
				uint32_t attachmentCount;
				if (msaaSamples > VK_SAMPLE_COUNT_1_BIT) {
					attachments[0] = msaaColorImage->GetImageView();
					attachments[1] = msaaDepthImage->GetImageView();
					attachments[2] = renderColorImage->GetImageView(); // resolve target
					attachmentCount = 3;
				} else {
					attachments[0] = renderColorImage->GetImageView();
					attachments[1] = renderDepthImage->GetImageView();
					attachmentCount = 2;
				}

				VkFramebufferCreateInfo fbInfo = {};
				fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				fbInfo.renderPass = renderPass;
				fbInfo.attachmentCount = attachmentCount;
				fbInfo.pAttachments = attachments;
				fbInfo.width = renderWidth;
				fbInfo.height = renderHeight;
				fbInfo.layers = 1;

				if (vkCreateFramebuffer(vkDevice, &fbInfo, nullptr, &renderFramebuffer) != VK_SUCCESS)
					SPRaise("Failed to create Vulkan render framebuffer");
				SPLog("Main render framebuffer created");
			}

			// Sprite framebuffer: color-only, 1-sample, for soft sprite rendering.
			// Only used when soft particles are active (requires non-MSAA mode).
			{
				VkImageView spriteAttachments[] = { renderColorImage->GetImageView() };

				VkFramebufferCreateInfo spriteFbInfo = {};
				spriteFbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				spriteFbInfo.renderPass = spriteRenderPass;
				spriteFbInfo.attachmentCount = 1;
				spriteFbInfo.pAttachments = spriteAttachments;
				spriteFbInfo.width = renderWidth;
				spriteFbInfo.height = renderHeight;
				spriteFbInfo.layers = 1;

				if (vkCreateFramebuffer(vkDevice, &spriteFbInfo, nullptr, &spriteFramebuffer) != VK_SUCCESS)
					SPRaise("Failed to create Vulkan sprite framebuffer");
				SPLog("Sprite framebuffer created");
			}

			// Add main render buffer to managed buffers
			Buffer buf;
			buf.framebuffer = renderFramebuffer;
			buf.colorImage = renderColorImage;
			buf.depthImage = (msaaSamples > VK_SAMPLE_COUNT_1_BIT) ? msaaDepthImage : renderDepthImage;
			buf.refCount = 0;
			buf.w = renderWidth;
			buf.h = renderHeight;
			buf.colorFormat = fbColorFormat;
			buffers.push_back(buf);
		}

		VulkanFramebufferManager::~VulkanFramebufferManager() {
			SPADES_MARK_FUNCTION();
			VkDevice vkDevice = device->GetDevice();

			// Clean up managed buffers (except the first one which is the main render buffer)
			for (size_t i = 1; i < buffers.size(); i++) {
				if (buffers[i].framebuffer != VK_NULL_HANDLE) {
					vkDestroyFramebuffer(vkDevice, buffers[i].framebuffer, nullptr);
				}
			}

			if (renderFramebuffer != VK_NULL_HANDLE) {
				vkDestroyFramebuffer(vkDevice, renderFramebuffer, nullptr);
			}

			if (spriteFramebuffer != VK_NULL_HANDLE) {
				vkDestroyFramebuffer(vkDevice, spriteFramebuffer, nullptr);
			}

			if (renderPass != VK_NULL_HANDLE) {
				vkDestroyRenderPass(vkDevice, renderPass, nullptr);
			}

			if (spriteRenderPass != VK_NULL_HANDLE) {
				vkDestroyRenderPass(vkDevice, spriteRenderPass, nullptr);
			}

			buffers.clear();
		}

		void VulkanFramebufferManager::CreateRenderPass() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();
			bool useMsaa = (msaaSamples > VK_SAMPLE_COUNT_1_BIT);

			// ── Main scene render pass ─────────────────────────────────────────────
			//
			// Non-MSAA layout (2 attachments):
			//   0 = color   (SAMPLE_COUNT_1, cleared, stored, UNDEFINED → COLOR_ATTACHMENT)
			//   1 = depth   (SAMPLE_COUNT_1, cleared, stored)
			//
			// MSAA layout (3 attachments):
			//   0 = msaa color  (SAMPLE_COUNT_N, cleared, don't-care after resolve)
			//   1 = msaa depth  (SAMPLE_COUNT_N, cleared, don't-care)
			//   2 = resolve     (SAMPLE_COUNT_1, don't-care, stored, UNDEFINED → SHADER_READ_ONLY)
			//
			// In the MSAA case the resolve attachment ends in SHADER_READ_ONLY so it is
			// already in the right layout for the post-process chain without an extra barrier.

			// Attachment 0: colour (MSAA or 1-sample)
			VkAttachmentDescription colorAttachment = {};
			colorAttachment.format         = fbColorFormat;
			colorAttachment.samples        = msaaSamples;
			colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
			colorAttachment.storeOp        = useMsaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE
			                                         : VK_ATTACHMENT_STORE_OP_STORE;
			colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
			colorAttachment.finalLayout    = useMsaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
			                                         : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			// Attachment 1: depth (MSAA or 1-sample)
			VkAttachmentDescription depthAttachment = {};
			depthAttachment.format         = fbDepthFormat;
			depthAttachment.samples        = msaaSamples;
			depthAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
			depthAttachment.storeOp        = useMsaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE
			                                         : VK_ATTACHMENT_STORE_OP_STORE;
			depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
			depthAttachment.finalLayout    = useMsaa ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
			                                         : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			// Attachment 2 (MSAA only): 1-sample resolve target
			VkAttachmentDescription resolveAttachment = {};
			resolveAttachment.format         = fbColorFormat;
			resolveAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
			resolveAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			resolveAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
			resolveAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			resolveAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			resolveAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
			// Resolve target goes straight to SHADER_READ_ONLY — no extra barrier needed
			// before the post-process chain reads it.
			resolveAttachment.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			VkAttachmentReference colorRef  = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
			VkAttachmentReference depthRef  = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
			VkAttachmentReference resolveRef = {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount    = 1;
			subpass.pColorAttachments       = &colorRef;
			subpass.pDepthStencilAttachment = &depthRef;
			subpass.pResolveAttachments     = useMsaa ? &resolveRef : nullptr;

			VkSubpassDependency dependency = {};
			dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass    = 0;
			dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			dependency.srcAccessMask = 0;
			dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
			                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

			VkAttachmentDescription attachments[3] = {colorAttachment, depthAttachment, resolveAttachment};

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = useMsaa ? 3 : 2;
			renderPassInfo.pAttachments    = attachments;
			renderPassInfo.subpassCount    = 1;
			renderPassInfo.pSubpasses      = &subpass;
			renderPassInfo.dependencyCount = 1;
			renderPassInfo.pDependencies   = &dependency;

			if (vkCreateRenderPass(vkDevice, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS)
				SPRaise("Failed to create Vulkan render pass");

			// ── Sprite render pass (color-only, 1-sample, soft particle rendering) ──
			// Always 1-sample: soft particles are disabled when MSAA is active
			// (the depth texture needed for soft depth-fade is MSAA and cannot be sampled).
			VkAttachmentDescription spriteColorAttachment = {};
			spriteColorAttachment.format         = fbColorFormat;
			spriteColorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
			spriteColorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
			spriteColorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
			spriteColorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			spriteColorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			spriteColorAttachment.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			spriteColorAttachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkAttachmentReference spriteColorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

			VkSubpassDescription spriteSubpass = {};
			spriteSubpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
			spriteSubpass.colorAttachmentCount = 1;
			spriteSubpass.pColorAttachments    = &spriteColorRef;

			VkSubpassDependency spriteDependency = {};
			spriteDependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
			spriteDependency.dstSubpass    = 0;
			spriteDependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			spriteDependency.srcAccessMask = 0;
			spriteDependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			spriteDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			VkRenderPassCreateInfo spriteRenderPassInfo = {};
			spriteRenderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			spriteRenderPassInfo.attachmentCount = 1;
			spriteRenderPassInfo.pAttachments    = &spriteColorAttachment;
			spriteRenderPassInfo.subpassCount    = 1;
			spriteRenderPassInfo.pSubpasses      = &spriteSubpass;
			spriteRenderPassInfo.dependencyCount = 1;
			spriteRenderPassInfo.pDependencies   = &spriteDependency;

			if (vkCreateRenderPass(vkDevice, &spriteRenderPassInfo, nullptr, &spriteRenderPass) != VK_SUCCESS)
				SPRaise("Failed to create Vulkan sprite render pass");
		}

		void VulkanFramebufferManager::PrepareSceneRendering(VkCommandBuffer commandBuffer) {
			SPADES_MARK_FUNCTION();
			doingPostProcessing = false;

			// Begin render pass
			VkRenderPassBeginInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = renderPass;
			renderPassInfo.framebuffer = renderFramebuffer;
			renderPassInfo.renderArea.offset = {0, 0};
			renderPassInfo.renderArea.extent = {(uint32_t)renderWidth, (uint32_t)renderHeight};

			VkClearValue clearValues[2];
			clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
			clearValues[1].depthStencil = {1.0f, 0};

			renderPassInfo.clearValueCount = 2;
			renderPassInfo.pClearValues = clearValues;

			vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
		}

		VulkanFramebufferManager::BufferHandle VulkanFramebufferManager::StartPostProcessing() {
			SPADES_MARK_FUNCTION();
			doingPostProcessing = true;

			// Return the main render buffer for post-processing
			return BufferHandle(this, 0);
		}

		void VulkanFramebufferManager::MakeSureAllBuffersReleased() {
			SPADES_MARK_FUNCTION();

			for (size_t i = 0; i < buffers.size(); i++) {
				SPAssert(buffers[i].refCount == 0);
			}
		}

		VulkanFramebufferManager::BufferHandle
		VulkanFramebufferManager::CreateBufferHandle(int w, int h, bool alpha) {
			VkFormat format;
			if (alpha) {
				format = useSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
			} else {
				format = fbColorFormat;
			}
			return CreateBufferHandle(w, h, format);
		}

		VulkanFramebufferManager::BufferHandle
		VulkanFramebufferManager::CreateBufferHandle(int w, int h, VkFormat colorFormat) {
			SPADES_MARK_FUNCTION();

			if (w < 0)
				w = renderWidth;
			if (h < 0)
				h = renderHeight;

			// During the main rendering pass the first buffer is allocated to the render target
			// and cannot be allocated for pre/postprocessing pass
			for (size_t i = doingPostProcessing ? 0 : 1; i < buffers.size(); i++) {
				Buffer& b = buffers[i];
				if (b.refCount > 0)
					continue;
				if (b.w != w || b.h != h)
					continue;
				if (b.colorFormat != colorFormat)
					continue;
				return BufferHandle(this, i);
			}

			if (buffers.size() > 128) {
				SPRaise("Maximum number of framebuffers exceeded");
			}

			SPLog("New VulkanColorBuffer requested (w = %d, h = %d, format = %d)", w, h,
			      (int)colorFormat);

			// Create new buffer
			Handle<VulkanImage> colorImage = Handle<VulkanImage>::New(
			    device, w, h, colorFormat,
			    VK_IMAGE_TILING_OPTIMAL,
			    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
			        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			colorImage->CreateImageView(VK_IMAGE_ASPECT_COLOR_BIT);
			colorImage->CreateSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR,
			                          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);

			Handle<VulkanImage> depthImage = Handle<VulkanImage>::New(
			    device, w, h, fbDepthFormat,
			    VK_IMAGE_TILING_OPTIMAL,
			    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			depthImage->CreateImageView(VK_IMAGE_ASPECT_DEPTH_BIT);

			VkImageView attachments[] = {
			    colorImage->GetImageView(),
			    depthImage->GetImageView()
			};

			VkFramebufferCreateInfo fbInfo = {};
			fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.renderPass = renderPass;
			fbInfo.attachmentCount = 2;
			fbInfo.pAttachments = attachments;
			fbInfo.width = w;
			fbInfo.height = h;
			fbInfo.layers = 1;

			VkFramebuffer framebuffer;
			VkDevice vkDevice = device->GetDevice();
			if (vkCreateFramebuffer(vkDevice, &fbInfo, nullptr, &framebuffer) != VK_SUCCESS) {
				SPRaise("Failed to create Vulkan framebuffer");
			}
			SPLog("Framebuffer created");

			Buffer buf;
			buf.framebuffer = framebuffer;
			buf.colorImage = colorImage;
			buf.depthImage = depthImage;
			buf.refCount = 0;
			buf.w = w;
			buf.h = h;
			buf.colorFormat = colorFormat;
			buffers.push_back(buf);
			return BufferHandle(this, buffers.size() - 1);
		}

#pragma mark - BufferHandle

		VulkanFramebufferManager::BufferHandle::BufferHandle()
		    : manager(nullptr), bufferIndex(0), valid(false) {}

		VulkanFramebufferManager::BufferHandle::BufferHandle(VulkanFramebufferManager* m, size_t index)
		    : manager(m), bufferIndex(index), valid(true) {
			SPAssert(bufferIndex < manager->buffers.size());
			Buffer& b = manager->buffers[bufferIndex];
			b.refCount++;
		}

		VulkanFramebufferManager::BufferHandle::BufferHandle(const BufferHandle& other)
		    : manager(other.manager), bufferIndex(other.bufferIndex), valid(other.valid) {
			if (valid) {
				Buffer& b = manager->buffers[bufferIndex];
				b.refCount++;
			}
		}

		VulkanFramebufferManager::BufferHandle::~BufferHandle() {
			Release();
		}

		void VulkanFramebufferManager::BufferHandle::operator=(const BufferHandle& other) {
			if (valid)
				manager->buffers[bufferIndex].refCount--;
			manager = other.manager;
			bufferIndex = other.bufferIndex;
			valid = other.valid;
			if (valid)
				manager->buffers[bufferIndex].refCount++;
		}

		void VulkanFramebufferManager::BufferHandle::Release() {
			if (valid) {
				Buffer& b = manager->buffers[bufferIndex];
				SPAssert(b.refCount > 0);
				b.refCount--;
				valid = false;
			}
		}

		VkFramebuffer VulkanFramebufferManager::BufferHandle::GetFramebuffer() {
			SPAssert(valid);
			Buffer& b = manager->buffers[bufferIndex];
			return b.framebuffer;
		}

		Handle<VulkanImage> VulkanFramebufferManager::BufferHandle::GetColorImage() {
			SPAssert(valid);
			Buffer& b = manager->buffers[bufferIndex];
			return b.colorImage;
		}

		Handle<VulkanImage> VulkanFramebufferManager::BufferHandle::GetDepthImage() {
			SPAssert(valid);
			Buffer& b = manager->buffers[bufferIndex];
			return b.depthImage;
		}

		int VulkanFramebufferManager::BufferHandle::GetWidth() {
			SPAssert(valid);
			Buffer& b = manager->buffers[bufferIndex];
			return b.w;
		}

		int VulkanFramebufferManager::BufferHandle::GetHeight() {
			SPAssert(valid);
			Buffer& b = manager->buffers[bufferIndex];
			return b.h;
		}

		VkFormat VulkanFramebufferManager::BufferHandle::GetColorFormat() {
			SPAssert(valid);
			Buffer& b = manager->buffers[bufferIndex];
			return b.colorFormat;
		}

	} // namespace draw
} // namespace spades
