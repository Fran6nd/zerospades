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

SPADES_SETTING(r_highPrec);
SPADES_SETTING(r_hdr);
SPADES_SETTING(r_srgb);
SPADES_SETTING(r_water);

namespace spades {
	namespace draw {

		VulkanFramebufferManager::VulkanFramebufferManager(Handle<gui::SDLVulkanDevice> dev,
		                                                   int renderWidth, int renderHeight)
		    : device(dev),
		      doingPostProcessing(false),
		      renderWidth(renderWidth),
		      renderHeight(renderHeight),
		      renderFramebuffer(VK_NULL_HANDLE),
		      mirrorFramebuffer(VK_NULL_HANDLE),
		      renderPass(VK_NULL_HANDLE),
		      waterRenderPass(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			SPLog("Initializing Vulkan framebuffer manager");

			useHighPrec = (bool)(int)r_highPrec;
			useHdr = (bool)(int)r_hdr;
			useSRGB = (bool)(int)r_srgb;

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

			// Choose depth format: prefer D24_UNORM_S8_UINT, but fall back to D32_SFLOAT_S8_UINT
			// if not supported (common on Apple Silicon / MoltenVK)
			VkFormatProperties formatProps;
			vkGetPhysicalDeviceFormatProperties(device->GetPhysicalDevice(),
			                                     VK_FORMAT_D24_UNORM_S8_UINT, &formatProps);

			if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
				fbDepthFormat = VK_FORMAT_D24_UNORM_S8_UINT;
				SPLog("Using D24_UNORM_S8_UINT depth format");
			} else {
				fbDepthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
				SPLog("D24_UNORM_S8_UINT not supported, using D32_SFLOAT_S8_UINT depth format");
			}

			CreateRenderPass();

			// Create main render framebuffer
			SPLog("Creating main render framebuffer");
			renderColorImage = Handle<VulkanImage>::New(
			    device, renderWidth, renderHeight, fbColorFormat,
			    VK_IMAGE_TILING_OPTIMAL,
			    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
			        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			renderColorImage->CreateImageView(VK_IMAGE_ASPECT_COLOR_BIT);
			renderColorImage->CreateSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR,
			                                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);

			renderDepthImage = Handle<VulkanImage>::New(
			    device, renderWidth, renderHeight, fbDepthFormat,
			    VK_IMAGE_TILING_OPTIMAL,
			    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
			        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			renderDepthImage->CreateImageView(VK_IMAGE_ASPECT_DEPTH_BIT);
			renderDepthImage->CreateSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST,
			                                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);

			VkImageView attachments[] = {
			    renderColorImage->GetImageView(),
			    renderDepthImage->GetImageView()
			};

			VkFramebufferCreateInfo fbInfo = {};
			fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.renderPass = renderPass;
			fbInfo.attachmentCount = 2;
			fbInfo.pAttachments = attachments;
			fbInfo.width = renderWidth;
			fbInfo.height = renderHeight;
			fbInfo.layers = 1;

			VkDevice vkDevice = device->GetDevice();
			if (vkCreateFramebuffer(vkDevice, &fbInfo, nullptr, &renderFramebuffer) != VK_SUCCESS) {
				SPRaise("Failed to create Vulkan render framebuffer");
			}
			SPLog("Main render framebuffer created");

			// Create mirror framebuffer for water reflections
			// Always create these - water shader needs them for reflections at all quality levels
			// r_water < 2: mirror images filled via scene copy
			// r_water >= 2: mirror images filled via reflected scene rendering
			{
				SPLog("Creating mirror framebuffer for water reflections");
				mirrorColorImage = Handle<VulkanImage>::New(
				    device, renderWidth, renderHeight, fbColorFormat,
				    VK_IMAGE_TILING_OPTIMAL,
				    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
				        VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				mirrorColorImage->CreateImageView(VK_IMAGE_ASPECT_COLOR_BIT);
				mirrorColorImage->CreateSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR,
				                                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);

				mirrorDepthImage = Handle<VulkanImage>::New(
				    device, renderWidth, renderHeight, fbDepthFormat,
				    VK_IMAGE_TILING_OPTIMAL,
				    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				mirrorDepthImage->CreateImageView(VK_IMAGE_ASPECT_DEPTH_BIT);
				mirrorDepthImage->CreateSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST,
				                                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);

				VkImageView mirrorAttachments[] = {
				    mirrorColorImage->GetImageView(),
				    mirrorDepthImage->GetImageView()
				};

				VkFramebufferCreateInfo mirrorFbInfo = {};
				mirrorFbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				mirrorFbInfo.renderPass = renderPass;
				mirrorFbInfo.attachmentCount = 2;
				mirrorFbInfo.pAttachments = mirrorAttachments;
				mirrorFbInfo.width = renderWidth;
				mirrorFbInfo.height = renderHeight;
				mirrorFbInfo.layers = 1;

				if (vkCreateFramebuffer(vkDevice, &mirrorFbInfo, nullptr, &mirrorFramebuffer) != VK_SUCCESS) {
					SPRaise("Failed to create Vulkan mirror framebuffer");
				}
				SPLog("Mirror framebuffer created");
			}

			// Create screen copy images for water refraction sampling
			// The water shader needs to read the scene, but it also renders to the same
			// framebuffer. We copy the scene to these images before the water pass.
			SPLog("Creating screen copy images for water sampling");
			screenCopyColorImage = Handle<VulkanImage>::New(
			    device, renderWidth, renderHeight, fbColorFormat,
			    VK_IMAGE_TILING_OPTIMAL,
			    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			screenCopyColorImage->CreateImageView(VK_IMAGE_ASPECT_COLOR_BIT);
			screenCopyColorImage->CreateSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR,
			                                    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);

			screenCopyDepthImage = Handle<VulkanImage>::New(
			    device, renderWidth, renderHeight, fbDepthFormat,
			    VK_IMAGE_TILING_OPTIMAL,
			    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			screenCopyDepthImage->CreateImageView(VK_IMAGE_ASPECT_DEPTH_BIT);
			screenCopyDepthImage->CreateSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST,
			                                    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
			SPLog("Screen copy images created");

			// Add main render buffer to managed buffers
			Buffer buf;
			buf.framebuffer = renderFramebuffer;
			buf.colorImage = renderColorImage;
			buf.depthImage = renderDepthImage;
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

			if (mirrorFramebuffer != VK_NULL_HANDLE) {
				vkDestroyFramebuffer(vkDevice, mirrorFramebuffer, nullptr);
			}

			if (renderPass != VK_NULL_HANDLE) {
				vkDestroyRenderPass(vkDevice, renderPass, nullptr);
			}

			if (waterRenderPass != VK_NULL_HANDLE) {
				vkDestroyRenderPass(vkDevice, waterRenderPass, nullptr);
			}

			buffers.clear();
		}

		void VulkanFramebufferManager::CreateRenderPass() {
			SPADES_MARK_FUNCTION();

			VkAttachmentDescription colorAttachment = {};
			colorAttachment.format = fbColorFormat;
			colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkAttachmentDescription depthAttachment = {};
			depthAttachment.format = fbDepthFormat;
			depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkAttachmentReference colorAttachmentRef = {};
			colorAttachmentRef.attachment = 0;
			colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkAttachmentReference depthAttachmentRef = {};
			depthAttachmentRef.attachment = 1;
			depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorAttachmentRef;
			subpass.pDepthStencilAttachment = &depthAttachmentRef;

			VkSubpassDependency dependency = {};
			dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass = 0;
			dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			dependency.srcAccessMask = 0;
			dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
			                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

			VkAttachmentDescription attachments[] = {colorAttachment, depthAttachment};

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = 2;
			renderPassInfo.pAttachments = attachments;
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = 1;
			renderPassInfo.pDependencies = &dependency;

			VkDevice vkDevice = device->GetDevice();
			if (vkCreateRenderPass(vkDevice, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
				SPRaise("Failed to create Vulkan render pass");
			}

			// Create water render pass with LOAD_OP to preserve existing framebuffer content
			VkAttachmentDescription waterColorAttachment = colorAttachment;
			waterColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			waterColorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkAttachmentDescription waterDepthAttachment = depthAttachment;
			waterDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			waterDepthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkAttachmentDescription waterAttachments[] = {waterColorAttachment, waterDepthAttachment};

			VkRenderPassCreateInfo waterRenderPassInfo = renderPassInfo;
			waterRenderPassInfo.pAttachments = waterAttachments;

			if (vkCreateRenderPass(vkDevice, &waterRenderPassInfo, nullptr, &waterRenderPass) != VK_SUCCESS) {
				SPRaise("Failed to create Vulkan water render pass");
			}
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

		VulkanFramebufferManager::BufferHandle
		VulkanFramebufferManager::PrepareForWaterRendering(VkCommandBuffer commandBuffer) {
			SPADES_MARK_FUNCTION();

			// Create or reuse a buffer for water rendering
			BufferHandle handle = CreateBufferHandle(-1, -1, true);

			// Copy current render target to the water buffer
			// This will be used as input for water rendering
			// (Implementation depends on how water rendering works)

			return handle;
		}

		void VulkanFramebufferManager::ClearMirrorImage(VkCommandBuffer commandBuffer, Vector3 bgCol) {
			SPADES_MARK_FUNCTION();

			VkRenderPassBeginInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = renderPass;
			renderPassInfo.framebuffer = mirrorFramebuffer;
			renderPassInfo.renderArea.offset = {0, 0};
			renderPassInfo.renderArea.extent = {(uint32_t)renderWidth, (uint32_t)renderHeight};

			VkClearValue clearValues[2];
			clearValues[0].color = {{bgCol.x, bgCol.y, bgCol.z, 1.0f}};
			clearValues[1].depthStencil = {1.0f, 0};

			renderPassInfo.clearValueCount = 2;
			renderPassInfo.pClearValues = clearValues;

			vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdEndRenderPass(commandBuffer);
		}

		void VulkanFramebufferManager::CopyToMirrorImage(VkCommandBuffer commandBuffer,
		                                                  VkFramebuffer srcFb) {
			SPADES_MARK_FUNCTION();

			if (srcFb == VK_NULL_HANDLE) {
				srcFb = renderFramebuffer;
			}

			// Get source image (from main render buffer)
			Handle<VulkanImage> srcColorImage = renderColorImage;
			Handle<VulkanImage> srcDepthImage = renderDepthImage;

			// Transition source images to TRANSFER_SRC_OPTIMAL
			VkImageMemoryBarrier srcBarriers[2]{};
			srcBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			srcBarriers[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			srcBarriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			srcBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			srcBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			srcBarriers[0].image = srcColorImage->GetImage();
			srcBarriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			srcBarriers[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			srcBarriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			srcBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			srcBarriers[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			srcBarriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			srcBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			srcBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			srcBarriers[1].image = srcDepthImage->GetImage();
			srcBarriers[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
			srcBarriers[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			srcBarriers[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			// Transition destination images to TRANSFER_DST_OPTIMAL
			VkImageMemoryBarrier dstBarriers[2]{};
			dstBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			dstBarriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			dstBarriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			dstBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			dstBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			dstBarriers[0].image = mirrorColorImage->GetImage();
			dstBarriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			dstBarriers[0].srcAccessMask = 0;
			dstBarriers[0].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			dstBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			dstBarriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			dstBarriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			dstBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			dstBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			dstBarriers[1].image = mirrorDepthImage->GetImage();
			dstBarriers[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
			dstBarriers[1].srcAccessMask = 0;
			dstBarriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			VkImageMemoryBarrier allBarriers[4] = {srcBarriers[0], srcBarriers[1], dstBarriers[0], dstBarriers[1]};
			vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, nullptr, 0, nullptr, 4, allBarriers);

			// Copy color attachment
			VkImageCopy colorCopyRegion = {};
			colorCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			colorCopyRegion.srcSubresource.layerCount = 1;
			colorCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			colorCopyRegion.dstSubresource.layerCount = 1;
			colorCopyRegion.extent = {(uint32_t)renderWidth, (uint32_t)renderHeight, 1};

			vkCmdCopyImage(commandBuffer,
			               srcColorImage->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			               mirrorColorImage->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			               1, &colorCopyRegion);

			// Copy depth attachment if needed
			if ((int)r_water >= 3) {
				VkImageCopy depthCopyRegion = {};
				depthCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				depthCopyRegion.srcSubresource.layerCount = 1;
				depthCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				depthCopyRegion.dstSubresource.layerCount = 1;
				depthCopyRegion.extent = {(uint32_t)renderWidth, (uint32_t)renderHeight, 1};

				vkCmdCopyImage(commandBuffer,
				               srcDepthImage->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				               mirrorDepthImage->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				               1, &depthCopyRegion);

			}
			// Transition mirror images to SHADER_READ_ONLY for water shader sampling
			VkImageMemoryBarrier postBarriers[2]{};
			postBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			postBarriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			postBarriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			postBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			postBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			postBarriers[0].image = mirrorColorImage->GetImage();
			postBarriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			postBarriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			postBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			postBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			postBarriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			postBarriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			postBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			postBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			postBarriers[1].image = mirrorDepthImage->GetImage();
			postBarriers[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
			postBarriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			postBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 2, postBarriers);
			}

		void VulkanFramebufferManager::CopySceneForWaterSampling(VkCommandBuffer commandBuffer) {
			SPADES_MARK_FUNCTION();

			// Transition source images to TRANSFER_SRC_OPTIMAL
			VkImageMemoryBarrier srcBarriers[2]{};
			srcBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			srcBarriers[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			srcBarriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			srcBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			srcBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			srcBarriers[0].image = renderColorImage->GetImage();
			srcBarriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			srcBarriers[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			srcBarriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			srcBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			srcBarriers[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			srcBarriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			srcBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			srcBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			srcBarriers[1].image = renderDepthImage->GetImage();
			srcBarriers[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
			srcBarriers[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			srcBarriers[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			// Transition destination images to TRANSFER_DST_OPTIMAL
			VkImageMemoryBarrier dstBarriers[2]{};
			dstBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			dstBarriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			dstBarriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			dstBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			dstBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			dstBarriers[0].image = screenCopyColorImage->GetImage();
			dstBarriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			dstBarriers[0].srcAccessMask = 0;
			dstBarriers[0].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			dstBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			dstBarriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			dstBarriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			dstBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			dstBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			dstBarriers[1].image = screenCopyDepthImage->GetImage();
			dstBarriers[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
			dstBarriers[1].srcAccessMask = 0;
			dstBarriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			VkImageMemoryBarrier allBarriers[4] = {srcBarriers[0], srcBarriers[1], dstBarriers[0], dstBarriers[1]};
			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 4, allBarriers);

			// Copy color
			VkImageCopy colorCopy = {};
			colorCopy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
			colorCopy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
			colorCopy.extent = {(uint32_t)renderWidth, (uint32_t)renderHeight, 1};
			vkCmdCopyImage(commandBuffer,
			               renderColorImage->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			               screenCopyColorImage->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			               1, &colorCopy);

			// Copy depth
			VkImageCopy depthCopy = {};
			depthCopy.srcSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
			depthCopy.dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
			depthCopy.extent = {(uint32_t)renderWidth, (uint32_t)renderHeight, 1};
			vkCmdCopyImage(commandBuffer,
			               renderDepthImage->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			               screenCopyDepthImage->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			               1, &depthCopy);

			// Transition screen copies to SHADER_READ_ONLY and source back to SHADER_READ_ONLY
			VkImageMemoryBarrier postBarriers[4]{};
			postBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			postBarriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			postBarriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			postBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			postBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			postBarriers[0].image = renderColorImage->GetImage();
			postBarriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			postBarriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			postBarriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			postBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			postBarriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			postBarriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			postBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			postBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			postBarriers[1].image = renderDepthImage->GetImage();
			postBarriers[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
			postBarriers[1].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			postBarriers[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

			postBarriers[2].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			postBarriers[2].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			postBarriers[2].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			postBarriers[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			postBarriers[2].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			postBarriers[2].image = screenCopyColorImage->GetImage();
			postBarriers[2].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			postBarriers[2].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			postBarriers[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			postBarriers[3].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			postBarriers[3].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			postBarriers[3].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			postBarriers[3].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			postBarriers[3].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			postBarriers[3].image = screenCopyDepthImage->GetImage();
			postBarriers[3].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
			postBarriers[3].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			postBarriers[3].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 4, postBarriers);
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
