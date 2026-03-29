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

#include "VulkanRenderer.h"
#include "VulkanMapRenderer.h"
#include "VulkanModelRenderer.h"
#include "VulkanSpriteRenderer.h"
#include "VulkanLongSpriteRenderer.h"
#include "VulkanImageRenderer.h"
#include "VulkanFlatMapRenderer.h"
#include "VulkanShadowMapRenderer.h"
#include "VulkanMapShadowRenderer.h"
#include "VulkanFramebufferManager.h"
#include "VulkanImageWrapper.h"
#include "VulkanImageManager.h"
#include "VulkanImage.h"
#include "VulkanBuffer.h"
#include "VulkanOptimizedVoxelModel.h"
#include "VulkanModelManager.h"
#include "VulkanProgramManager.h"
#include "VulkanPipelineCache.h"
#include "VulkanTemporaryImagePool.h"
#include "VulkanAutoExposureFilter.h"
#include "VulkanBloomFilter.h"
#include "VulkanFogFilter.h"
#include "VulkanDepthOfFieldFilter.h"
#include <Gui/SDLVulkanDevice.h>
#include <Client/GameMap.h>
#include <Core/Bitmap.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/Settings.h>
#include <cstring>
#include <vector>

SPADES_SETTING(r_dlights);
SPADES_SETTING(r_hdr);
SPADES_SETTING(r_bloom);
SPADES_SETTING(r_fogShadow);
SPADES_SETTING(r_depthOfField);
SPADES_SETTING(r_softParticles);
SPADES_SETTING(r_outlines);

namespace spades {
	namespace draw {

		VulkanRenderer::VulkanRenderer(Handle<gui::SDLVulkanDevice> dev)
		: device(std::move(dev)),
		  map(nullptr),
		  inited(false),
		  sceneUsedInThisFrame(false),
		  renderPass(VK_NULL_HANDLE),
		  depthImage(VK_NULL_HANDLE),
		  depthImageMemory(VK_NULL_HANDLE),
		  depthImageView(VK_NULL_HANDLE),
		  currentImageIndex(0),
		  currentFrameSlot(0),
		  imageAvailableSemaphore(VK_NULL_HANDLE),
		  renderFinishedSemaphore(VK_NULL_HANDLE),
		  fogDistance(128.0f),
		  fogColor(MakeVector3(0, 0, 0)),
		  drawColorAlphaPremultiplied(MakeVector4(1, 1, 1, 1)),
		  legacyColorPremultiply(false),
		  lastTime(0),
		  frameNumber(0),
		  duringSceneRendering(false),
		  mapRenderer(nullptr),
		  modelRenderer(nullptr),
		  spriteRenderer(nullptr),
		  longSpriteRenderer(nullptr),
			imageRenderer(nullptr),
		flatMapRenderer(nullptr),
		shadowMapRenderer(nullptr),
		mapShadowRenderer(nullptr),
		framebufferManager(nullptr),
		programManager(nullptr),
		imageManager(nullptr),
		skyPipeline(VK_NULL_HANDLE),
		skyPipelineLayout(VK_NULL_HANDLE),
		multiplyColorPipeline(VK_NULL_HANDLE),
		multiplyColorPipelineLayout(VK_NULL_HANDLE),
		debugLinePipeline(VK_NULL_HANDLE),
		debugLinePipelineLayout(VK_NULL_HANDLE) {
		renderWidth = device->ScreenWidth();
		renderHeight = device->ScreenHeight();


		// Initialize program manager, pipeline cache and model manager
		try {
			programManager = Handle<VulkanProgramManager>::New(device);
			pipelineCache = Handle<VulkanPipelineCache>::New(device);
			modelManager = Handle<VulkanModelManager>::New(*this);
			temporaryImagePool = Handle<VulkanTemporaryImagePool>::New(device);
			InitializeVulkanResources();  // Create semaphores for synchronization

			// Create framebuffer manager for offscreen rendering
			framebufferManager = stmp::make_unique<VulkanFramebufferManager>(device, renderWidth, renderHeight);

			depthFormat = FindDepthFormat();
			CreateRenderPass();  // Must create render pass before framebuffers
			CreateDepthResources();  // Create depth buffer for 3D rendering
			CreateFramebuffers();
				CreateCommandBuffers();
			CreateSkyPipeline();
			CreateMultiplyColorPipeline();
			CreateDebugLinePipeline();

				mapRenderer = nullptr;
				modelRenderer = stmp::make_unique<VulkanModelRenderer>(*this);
				spriteRenderer = stmp::make_unique<VulkanSpriteRenderer>(*this);
				longSpriteRenderer = stmp::make_unique<VulkanLongSpriteRenderer>(*this);
				imageRenderer = stmp::make_unique<VulkanImageRenderer>(*this);
			imageManager = stmp::make_unique<VulkanImageManager>(*this, device);

			// Create a 1x1 white image for solid color rendering
			{
				Handle<Bitmap> whiteBmp(new Bitmap(1, 1), false);
				uint32_t* pixel = reinterpret_cast<uint32_t*>(whiteBmp->GetPixels());
				*pixel = 0xFFFFFFFF; // White (RGBA)
				Handle<client::IImage> imgHandle = CreateImage(*whiteBmp);

				// Get VulkanImage from the IImage
				VulkanImageWrapper* wrapper = dynamic_cast<VulkanImageWrapper*>(imgHandle.GetPointerOrNull());
				if (wrapper) {
					whiteImage = Handle<VulkanImage>(wrapper->GetVulkanImage());
				} else {
					whiteImage = Handle<VulkanImage>(dynamic_cast<VulkanImage*>(std::move(imgHandle).Unmanage()));
				}
			}

			// Preload shaders
			VulkanMapRenderer::PreloadShaders(*this);
			VulkanOptimizedVoxelModel::PreloadShaders(*this);

			// Post-process filters
			autoExposureFilter = stmp::make_unique<VulkanAutoExposureFilter>(*this);
			bloomFilter = stmp::make_unique<VulkanBloomFilter>(*this);
			if (mapShadowRenderer)
				fogFilter = stmp::make_unique<VulkanFogFilter>(*this);
			depthOfFieldFilter = stmp::make_unique<VulkanDepthOfFieldFilter>(*this);

			inited = true;
			lastSwapchainGeneration = device->GetSwapchainGeneration();
			vkDeviceWaitIdle(device->GetDevice());
		} catch (const std::exception& ex) {
			CleanupVulkanResources();
			throw;
		}
	}

	void VulkanRenderer::Shutdown() {
			SPADES_MARK_FUNCTION();

			if (!inited) {
				return;
			}

			// Remove this renderer from the GameMap's listener list before cleanup
			SetGameMap(nullptr);

			VkDevice vkDevice = device->GetDevice();
			if (vkDevice != VK_NULL_HANDLE) {
				vkDeviceWaitIdle(vkDevice);
			}

			// Invalidate shared pipeline caches before cleaning up
			VulkanOptimizedVoxelModel::InvalidateSharedPipeline(device.GetPointerOrNull());

			imageRenderer.reset();
			spriteRenderer.reset();
			longSpriteRenderer.reset();
			modelRenderer.reset();
			imageManager.reset();
			mapRenderer.reset();
			flatMapRenderer.reset();
			shadowMapRenderer.reset();
			mapShadowRenderer.reset();
			depthOfFieldFilter.reset();
			fogFilter.reset();
			bloomFilter.reset();
			autoExposureFilter.reset();
			framebufferManager.reset();
			programManager = nullptr;

			if (temporaryImagePool) {
				temporaryImagePool->Clear();
			}
			temporaryImagePool = nullptr;

			CleanupVulkanResources();

			inited = false;
		}

	VulkanRenderer::~VulkanRenderer() {
		// Ensure resources are cleaned up
		if (inited) {
			Shutdown();
		}
	}

		void VulkanRenderer::InitializeVulkanResources() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			// Semaphores are obtained from SDLVulkanDevice per-frame via AcquireNextImage
			// Initialize to null - they will be set when acquiring swapchain images
			imageAvailableSemaphore = VK_NULL_HANDLE;
			renderFinishedSemaphore = VK_NULL_HANDLE;

			// Create fences for frame synchronization (one per frame-in-flight slot)
			uint32_t maxFrames = device->GetMaxFramesInFlight();
			inFlightFences.resize(maxFrames);

			VkFenceCreateInfo fenceInfo{};
			fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start signaled so first frame doesn't wait

			for (size_t i = 0; i < maxFrames; i++) {
				VkResult result = vkCreateFence(vkDevice, &fenceInfo, nullptr, &inFlightFences[i]);
				if (result != VK_SUCCESS) {
					// Clean up previously created fences
					for (size_t j = 0; j < i; j++) {
						vkDestroyFence(vkDevice, inFlightFences[j], nullptr);
					}
					SPRaise("Failed to create fence %zu (error code: %d)", i, result);
				}
			}
		}

		void VulkanRenderer::CreateRenderPass() {
			SPADES_MARK_FUNCTION();

			// Color attachment
			// Note: Using LOAD_OP_LOAD because we blit the offscreen framebuffer before UI rendering
			VkAttachmentDescription colorAttachment{};
			colorAttachment.format = device->GetSwapchainImageFormat();
			colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

			// Depth attachment
			VkAttachmentDescription depthAttachment{};
			depthAttachment.format = depthFormat;
			depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkAttachmentReference colorAttachmentRef{};
			colorAttachmentRef.attachment = 0;
			colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkAttachmentReference depthAttachmentRef{};
			depthAttachmentRef.attachment = 1;
			depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass{};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorAttachmentRef;
			subpass.pDepthStencilAttachment = &depthAttachmentRef;

			VkSubpassDependency dependency{};
			dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass = 0;
			dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			// Include source access for presentation to properly synchronize with previous frame
			dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

			VkAttachmentDescription attachments[] = {colorAttachment, depthAttachment};
			VkRenderPassCreateInfo renderPassInfo{};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = 2;
			renderPassInfo.pAttachments = attachments;
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = 1;
			renderPassInfo.pDependencies = &dependency;

			VkResult result = vkCreateRenderPass(device->GetDevice(), &renderPassInfo, nullptr, &renderPass);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create render pass (error code: %d)", result);
			}

		}

		VkFormat VulkanRenderer::FindDepthFormat() {
		static const VkFormat candidates[] = {
			VK_FORMAT_D32_SFLOAT,
			VK_FORMAT_D24_UNORM_S8_UINT,
			VK_FORMAT_D16_UNORM,
		};

		VkPhysicalDevice physDevice = device->GetPhysicalDevice();
		for (VkFormat fmt : candidates) {
			VkFormatProperties props;
			vkGetPhysicalDeviceFormatProperties(physDevice, fmt, &props);
			if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
				return fmt;
		}
		SPRaise("Failed to find a supported depth format");
	}

	uint32_t VulkanRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
			VkPhysicalDeviceMemoryProperties memProperties;
			vkGetPhysicalDeviceMemoryProperties(device->GetPhysicalDevice(), &memProperties);

			for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
				if ((typeFilter & (1 << i)) &&
				    (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
					return i;
				}
			}

			SPRaise("Failed to find suitable memory type");
		}

		uint32_t VulkanRenderer::FindMemoryTypeWithFallback(uint32_t typeFilter,
		                                                   VkMemoryPropertyFlags preferred,
		                                                   VkMemoryPropertyFlags fallback) {
			VkPhysicalDeviceMemoryProperties memProperties;
			vkGetPhysicalDeviceMemoryProperties(device->GetPhysicalDevice(), &memProperties);

			// Try preferred first
			for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
				if ((typeFilter & (1 << i)) &&
				    (memProperties.memoryTypes[i].propertyFlags & preferred) == preferred) {
					return i;
				}
			}

			// Fall back to less strict requirements
			for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
				if ((typeFilter & (1 << i)) &&
				    (memProperties.memoryTypes[i].propertyFlags & fallback) == fallback) {
					return i;
				}
			}

			SPRaise("Failed to find suitable memory type");
		}

		void VulkanRenderer::CreateDepthResources() {
			SPADES_MARK_FUNCTION();

			VkExtent2D swapchainExtent = device->GetSwapchainExtent();

			// Create depth image with transient attachment flag
			// This allows GPUs to potentially avoid allocating memory for depth data
			// that doesn't need to persist between render passes
			VkImageCreateInfo imageInfo{};
			imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageInfo.imageType = VK_IMAGE_TYPE_2D;
			imageInfo.extent.width = swapchainExtent.width;
			imageInfo.extent.height = swapchainExtent.height;
			imageInfo.extent.depth = 1;
			imageInfo.mipLevels = 1;
			imageInfo.arrayLayers = 1;
			imageInfo.format = depthFormat;
			imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
			                  VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
			imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VkResult result = vkCreateImage(device->GetDevice(), &imageInfo, nullptr, &depthImage);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create depth image (error code: %d)", result);
			}

			// Allocate memory for depth image
			// Try lazily allocated memory first (optimal for transient attachments),
			// fall back to device local if not supported.
			// Always use dedicated allocation to avoid MoltenVK placement heap
			// assertions on Intel GPUs.
			VkMemoryRequirements memRequirements;
			vkGetImageMemoryRequirements(device->GetDevice(), depthImage, &memRequirements);

			VkMemoryDedicatedAllocateInfo dedicatedAllocInfo{};
			dedicatedAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
			dedicatedAllocInfo.image = depthImage;

			VkMemoryAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = memRequirements.size;
			allocInfo.memoryTypeIndex = FindMemoryTypeWithFallback(
				memRequirements.memoryTypeBits,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			allocInfo.pNext = &dedicatedAllocInfo;

			result = vkAllocateMemory(device->GetDevice(), &allocInfo, nullptr, &depthImageMemory);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to allocate depth image memory (error code: %d)", result);
			}

			vkBindImageMemory(device->GetDevice(), depthImage, depthImageMemory, 0);

			// Create depth image view
			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = depthImage;
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = depthFormat;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.baseArrayLayer = 0;
			viewInfo.subresourceRange.layerCount = 1;

			result = vkCreateImageView(device->GetDevice(), &viewInfo, nullptr, &depthImageView);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create depth image view (error code: %d)", result);
			}
		}

		void VulkanRenderer::CreateFramebuffers() {
			SPADES_MARK_FUNCTION();

			const auto& swapchainImageViews = device->GetSwapchainImageViews();
			swapchainFramebuffers.resize(swapchainImageViews.size());

			VkExtent2D swapchainExtent = device->GetSwapchainExtent();

			for (size_t i = 0; i < swapchainImageViews.size(); i++) {
				VkImageView attachments[] = {
					swapchainImageViews[i],
					depthImageView
				};

				VkFramebufferCreateInfo framebufferInfo{};
				framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				framebufferInfo.renderPass = renderPass;
				framebufferInfo.attachmentCount = 2;
				framebufferInfo.pAttachments = attachments;
				framebufferInfo.width = swapchainExtent.width;
				framebufferInfo.height = swapchainExtent.height;
				framebufferInfo.layers = 1;

				VkResult result = vkCreateFramebuffer(device->GetDevice(), &framebufferInfo,
				                                      nullptr, &swapchainFramebuffers[i]);
				if (result != VK_SUCCESS) {
					SPRaise("Failed to create framebuffer (error code: %d)", result);
				}
			}

		}

		void VulkanRenderer::CreateCommandBuffers() {
			SPADES_MARK_FUNCTION();

			const auto& swapchainImageViews = device->GetSwapchainImageViews();
			commandBuffers.resize(swapchainImageViews.size());

			VkCommandBufferAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.commandPool = device->GetCommandPool();
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

			VkResult result = vkAllocateCommandBuffers(device->GetDevice(), &allocInfo, commandBuffers.data());
			if (result != VK_SUCCESS) {
				SPRaise("Failed to allocate command buffers (error code: %d)", result);
			}

		}

		void VulkanRenderer::CleanupVulkanResources() {
			VkDevice vkDevice = device->GetDevice();

			if (!commandBuffers.empty() && vkDevice != VK_NULL_HANDLE) {
				vkFreeCommandBuffers(vkDevice, device->GetCommandPool(),
					static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
				commandBuffers.clear();
			}

			for (auto framebuffer : swapchainFramebuffers) {
				if (framebuffer != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
					vkDestroyFramebuffer(vkDevice, framebuffer, nullptr);
				}
			}
			swapchainFramebuffers.clear();

			// Cleanup depth resources
			if (depthImageView != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroyImageView(vkDevice, depthImageView, nullptr);
				depthImageView = VK_NULL_HANDLE;
			}
			if (depthImage != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroyImage(vkDevice, depthImage, nullptr);
				depthImage = VK_NULL_HANDLE;
			}
			if (depthImageMemory != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkFreeMemory(vkDevice, depthImageMemory, nullptr);
				depthImageMemory = VK_NULL_HANDLE;
			}

			DestroySkyPipeline();
		DestroyMultiplyColorPipeline();
		DestroyDebugLinePipeline();

			if (renderPass != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroyRenderPass(vkDevice, renderPass, nullptr);
				renderPass = VK_NULL_HANDLE;
			}

			// Semaphores are owned by SDLVulkanDevice, not destroyed here
			imageAvailableSemaphore = VK_NULL_HANDLE;
			renderFinishedSemaphore = VK_NULL_HANDLE;

			// Destroy fences
			for (VkFence fence : inFlightFences) {
				if (fence != VK_NULL_HANDLE) {
					vkDestroyFence(vkDevice, fence, nullptr);
				}
			}
			inFlightFences.clear();
		}

		void VulkanRenderer::RecreateSwapchainDependencies() {
		VkDevice vkDevice = device->GetDevice();

		// The device is already idle (SDLVulkanDevice::RecreateSwapchain called vkDeviceWaitIdle),
		// but wait again in case the renderer submitted work between that call and this one.
		vkDeviceWaitIdle(vkDevice);

		// Free command buffers before destroying the framebuffers they reference.
		if (!commandBuffers.empty()) {
			vkFreeCommandBuffers(vkDevice, device->GetCommandPool(),
				static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
			commandBuffers.clear();
		}

		// Destroy swapchain framebuffers (reference stale image views).
		for (auto fb : swapchainFramebuffers) {
			if (fb != VK_NULL_HANDLE)
				vkDestroyFramebuffer(vkDevice, fb, nullptr);
		}
		swapchainFramebuffers.clear();

		// Destroy depth resources (extent-dependent).
		depthImageWrapper = nullptr; // release Handle before destroying the underlying VkImage
		if (depthImageView != VK_NULL_HANDLE) {
			vkDestroyImageView(vkDevice, depthImageView, nullptr);
			depthImageView = VK_NULL_HANDLE;
		}
		if (depthImage != VK_NULL_HANDLE) {
			vkDestroyImage(vkDevice, depthImage, nullptr);
			depthImage = VK_NULL_HANDLE;
		}
		if (depthImageMemory != VK_NULL_HANDLE) {
			vkFreeMemory(vkDevice, depthImageMemory, nullptr);
			depthImageMemory = VK_NULL_HANDLE;
		}

		// Pull new dimensions from the swapchain.
		renderWidth = device->ScreenWidth();
		renderHeight = device->ScreenHeight();

		// Rebuild swapchain-dependent renderer resources.
		CreateDepthResources();
		CreateFramebuffers();
		CreateCommandBuffers();

		framebufferManager = stmp::make_unique<VulkanFramebufferManager>(device, renderWidth, renderHeight);

		lastSwapchainGeneration = device->GetSwapchainGeneration();
		SPLog("Swapchain dependencies recreated (%dx%d)", renderWidth, renderHeight);
	}

	void VulkanRenderer::BuildProjectionMatrix() {
			SPADES_MARK_FUNCTION();
			projectionMatrix = sceneDef.ToVulkanProjectionMatrix();
		}

		void VulkanRenderer::BuildView() {
			SPADES_MARK_FUNCTION();
			viewMatrix = sceneDef.ToViewMatrix();
			projectionViewMatrix = projectionMatrix * viewMatrix;
		}

		void VulkanRenderer::BuildFrustrum() {
			// near/far planes
			frustrum[0] = Plane3::PlaneWithPointOnPlane(sceneDef.viewOrigin, sceneDef.viewAxis[2]);
			frustrum[1] = frustrum[0].Flipped();
			frustrum[0].w -= sceneDef.zNear;
			frustrum[1].w += sceneDef.zFar;

			// side planes
			float cx = cosf(sceneDef.fovX * 0.5F);
			float sx = sinf(sceneDef.fovX * 0.5F);
			float cy = cosf(sceneDef.fovY * 0.5F);
			float sy = sinf(sceneDef.fovY * 0.5F);

			frustrum[2] = Plane3::PlaneWithPointOnPlane(
			  sceneDef.viewOrigin, sceneDef.viewAxis[2] * sx - sceneDef.viewAxis[0] * cx);
			frustrum[3] = Plane3::PlaneWithPointOnPlane(
			  sceneDef.viewOrigin, sceneDef.viewAxis[2] * sx + sceneDef.viewAxis[0] * cx);
			frustrum[4] = Plane3::PlaneWithPointOnPlane(
			  sceneDef.viewOrigin, sceneDef.viewAxis[2] * sy - sceneDef.viewAxis[1] * cy);
			frustrum[5] = Plane3::PlaneWithPointOnPlane(
			  sceneDef.viewOrigin, sceneDef.viewAxis[2] * sy + sceneDef.viewAxis[1] * cy);
		}

		bool VulkanRenderer::BoxFrustrumCull(const AABB3& box) {
			return PlaneCullTest(frustrum[0], box) && PlaneCullTest(frustrum[1], box) &&
			       PlaneCullTest(frustrum[2], box) && PlaneCullTest(frustrum[3], box) &&
			       PlaneCullTest(frustrum[4], box) && PlaneCullTest(frustrum[5], box);
		}

		bool VulkanRenderer::SphereFrustrumCull(const Vector3& center, float radius) {
			for (int i = 0; i < 6; i++) {
				if (frustrum[i].GetDistanceTo(center) < -radius)
					return false;
			}
			return true;
		}

				void VulkanRenderer::EnsureInitialized() {
			if (!inited) {
				SPRaise("Renderer not initialized");
			}
		}

		void VulkanRenderer::EnsureSceneStarted() {
			if (!duringSceneRendering) {
				SPRaise("Not in scene rendering");
			}
		}

		void VulkanRenderer::EnsureSceneNotStarted() {
			if (duringSceneRendering) {
				SPRaise("Already in scene rendering");
			}
		}

	VulkanProgram* VulkanRenderer::RegisterProgram(const std::string& name) {
		if (!programManager)
			return nullptr;
		return programManager->RegisterProgram(name);
	}

	VulkanShader* VulkanRenderer::RegisterShader(const std::string& name) {
		if (!programManager)
			return nullptr;
		return programManager->RegisterShader(name);
	}

		Handle<client::IImage> VulkanRenderer::RegisterImage(const char* filename) {
			SPADES_MARK_FUNCTION();

			if (!imageManager) {
				SPLog("RegisterImage: imageManager not initialized yet");
				return Handle<client::IImage>();
			}

			return imageManager->RegisterImage(filename);
		}

		Handle<client::IModel> VulkanRenderer::RegisterModel(const char* filename) {
			SPADES_MARK_FUNCTION();
			return modelManager->RegisterModel(filename);
		}

	void VulkanRenderer::Init() {
		// Initialization is done in the constructor; make this a no-op if already initialized
		if (inited)
			return;
	}
	Handle<client::IImage> VulkanRenderer::CreateImage(Bitmap& bitmap) {
		SPADES_MARK_FUNCTION();
		try {
			uint32_t width = static_cast<uint32_t>(bitmap.GetWidth());
			uint32_t height = static_cast<uint32_t>(bitmap.GetHeight());

				// Create Vulkan image
				// Use RGBA format to match SDL's ABGR8888 bitmap format (which is RGBA in memory on little-endian)
				Handle<VulkanImage> vkImage(new VulkanImage(
					device, width, height, VK_FORMAT_R8G8B8A8_UNORM,
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), false);

				// Upload bitmap data to the image
				// Flip the bitmap vertically for Vulkan (bitmap is in OpenGL bottom-left format)
				VkDeviceSize imageSize = width * height * 4;
				std::vector<uint8_t> flippedData(imageSize);
				const uint8_t* srcPixels = reinterpret_cast<const uint8_t*>(bitmap.GetPixels());
				uint32_t rowSize = width * 4;

				for (uint32_t y = 0; y < height; y++) {
					const uint8_t* srcRow = srcPixels + y * rowSize;
					uint8_t* dstRow = flippedData.data() + (height - 1 - y) * rowSize;
					std::memcpy(dstRow, srcRow, rowSize);
				}

				Handle<VulkanBuffer> stagingBuffer(new VulkanBuffer(
					device, imageSize,
					VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT), false);

				stagingBuffer->UpdateData(flippedData.data(), imageSize);

				// Create temporary command buffer for upload
				VkCommandBufferAllocateInfo allocInfo{};
				allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
				allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
				allocInfo.commandPool = device->GetCommandPool();
				allocInfo.commandBufferCount = 1;

				VkCommandBuffer commandBuffer;
				if (vkAllocateCommandBuffers(device->GetDevice(), &allocInfo, &commandBuffer) != VK_SUCCESS) {
					SPRaise("Failed to allocate command buffer for screenshot");
				}

				VkCommandBufferBeginInfo beginInfo{};
				beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
				if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
					SPRaise("Failed to begin command buffer for screenshot");
				}

				// Transition image to transfer dst
				vkImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					0, VK_ACCESS_TRANSFER_WRITE_BIT,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

				// Copy buffer to image
				vkImage->CopyFromBuffer(commandBuffer, stagingBuffer->GetBuffer());

				// Transition image to shader read
				vkImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

				vkEndCommandBuffer(commandBuffer);

				// Submit immediately so any subsequent Update() calls via IImage::Update()
				// don't race against a deferred blank upload (which would overwrite glyph data)
				VkSubmitInfo uploadSubmit{};
				uploadSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				uploadSubmit.commandBufferCount = 1;
				uploadSubmit.pCommandBuffers = &commandBuffer;
				vkQueueSubmit(device->GetGraphicsQueue(), 1, &uploadSubmit, VK_NULL_HANDLE);
				vkQueueWaitIdle(device->GetGraphicsQueue());
				vkFreeCommandBuffers(device->GetDevice(), device->GetCommandPool(), 1, &commandBuffer);

				// Create sampler for the image
				vkImage->CreateSampler();

				// Wrap in IImage interface
				return Handle<client::IImage>(
					new VulkanImageWrapper(vkImage, static_cast<float>(width), static_cast<float>(height)),
					false).Cast<client::IImage>();
			} catch (const std::exception& ex) {
				SPLog("Failed to create Vulkan image: %s", ex.what());
				return Handle<client::IImage>();
			}
		}

		Handle<client::IModel> VulkanRenderer::CreateModel(VoxelModel& model) {
			SPADES_MARK_FUNCTION();
			return Handle<VulkanOptimizedVoxelModel>::New(&model, *this).Cast<client::IModel>();
		}

		void VulkanRenderer::SetGameMap(stmp::optional<client::GameMap&> newMap) {
			SPADES_MARK_FUNCTION();

			client::GameMap* newMapPtr = newMap ? &*newMap : nullptr;

			if (map == newMapPtr) {
				return;
			}

			// Note: We intentionally don't call RemoveListener on the old map here.
			// When transitioning between clients (e.g., serverlist -> connect), the old map
			// may already be deleted, making the map pointer stale. The previous Client's
			// destructor already called SetGameMap(nullptr) which should have cleaned up properly.
			// If not, calling RemoveListener on a deleted map causes "mutex lock failed" errors.

			map = newMapPtr;

			if (map) {
				map->AddListener(this);
			}

			// Initialize map shadow renderer (heightmap shadow) BEFORE map renderer
			if (map) {
				mapShadowRenderer = stmp::make_unique<VulkanMapShadowRenderer>(*this, map);
			} else {
				mapShadowRenderer.reset();
			}

			// Initialize map renderer
			if (map) {
				mapRenderer = stmp::make_unique<VulkanMapRenderer>(map, *this);
				mapRenderer->CreatePipelines(framebufferManager->GetRenderPass()); // Use offscreen render pass for 3D rendering
				// Link shadow texture to map renderer
				if (mapShadowRenderer) {
					mapRenderer->UpdateShadowDescriptor(mapShadowRenderer->GetShadowImage());
				}
			} else {
				mapRenderer.reset();
			}

			// Initialize shadow map renderer (cascaded depth maps for fog)
			if (map) {
				shadowMapRenderer = stmp::make_unique<VulkanShadowMapRenderer>(*this);
			} else {
				shadowMapRenderer.reset();
			}

			// Initialize flat map renderer (minimap)
			if (map) {
				flatMapRenderer = stmp::make_unique<VulkanFlatMapRenderer>(*this, *map);
			} else {
				flatMapRenderer.reset();
			}

		}

		void VulkanRenderer::SetFogDistance(float distance) {
			fogDistance = distance;
		}

		void VulkanRenderer::SetFogColor(Vector3 color) {
			fogColor = color;
		}

		Vector3 VulkanRenderer::GetFogColorForSolidPass() {
			if (r_fogShadow && shadowMapRenderer)
				return MakeVector3(0, 0, 0);
			else
				return fogColor;
		}

		void VulkanRenderer::StartScene(const client::SceneDefinition& def) {
			SPADES_MARK_FUNCTION();
			EnsureInitialized();
			EnsureSceneNotStarted();

			sceneDef = def;
			duringSceneRendering = true;
			sceneUsedInThisFrame = true;

			BuildProjectionMatrix();
			BuildView();
			BuildFrustrum();

			// Wait for the previous frame that used this semaphore slot to finish
			currentFrameSlot = device->GetCurrentFrame();
			vkWaitForFences(device->GetDevice(), 1, &inFlightFences[currentFrameSlot], VK_TRUE, UINT64_MAX);

			// Acquire next swapchain image
			currentImageIndex = device->AcquireNextImage(&imageAvailableSemaphore, &renderFinishedSemaphore);
			if (currentImageIndex == UINT32_MAX) {
				// Swapchain was recreated, try again
				currentImageIndex = device->AcquireNextImage(&imageAvailableSemaphore, &renderFinishedSemaphore);
			}
		}

		void VulkanRenderer::AddDebugLine(Vector3 a, Vector3 b, Vector4 color) {
			SPADES_MARK_FUNCTION();
			EnsureInitialized();
			EnsureSceneStarted();
			DebugLine line;
			line.v1 = a;
			line.v2 = b;
			line.color = color;
			debugLines.push_back(line);
		}

		void VulkanRenderer::AddSprite(client::IImage& img, Vector3 center, float radius, float rotation) {
			SPADES_MARK_FUNCTION();
			EnsureInitialized();
			EnsureSceneStarted();

			VulkanImage* vkImage = nullptr;
			VulkanImageWrapper* wrapper = dynamic_cast<VulkanImageWrapper*>(&img);
			if (wrapper) {
				vkImage = wrapper->GetVulkanImage();
			} else {
				vkImage = dynamic_cast<VulkanImage*>(&img);
			}

			if (vkImage && spriteRenderer) {
				spriteRenderer->Add(vkImage, center, radius, rotation, drawColorAlphaPremultiplied);
			}
		}

		void VulkanRenderer::AddLongSprite(client::IImage& img, Vector3 p1, Vector3 p2, float radius) {
			SPADES_MARK_FUNCTION();
			EnsureInitialized();
			EnsureSceneStarted();

			VulkanImage* vkImage = nullptr;
			VulkanImageWrapper* wrapper = dynamic_cast<VulkanImageWrapper*>(&img);
			if (wrapper) {
				vkImage = wrapper->GetVulkanImage();
			} else {
				vkImage = dynamic_cast<VulkanImage*>(&img);
			}

			if (vkImage && longSpriteRenderer) {
				longSpriteRenderer->Add(vkImage, p1, p2, radius, drawColorAlphaPremultiplied);
			}
		}

		void VulkanRenderer::AddLight(const client::DynamicLightParam& light) {
			SPADES_MARK_FUNCTION();
			if (!r_dlights)
				return;
			if (!SphereFrustrumCull(light.origin, light.radius))
				return;
			EnsureInitialized();
			EnsureSceneStarted();
			lights.push_back(light);
		}

		void VulkanRenderer::RenderModel(client::IModel& model, const client::ModelRenderParam& param) {
			SPADES_MARK_FUNCTION();
			EnsureInitialized();
			EnsureSceneStarted();

			// Forward to model renderer
			if (modelRenderer) {
				VulkanModel* vkModel = dynamic_cast<VulkanModel*>(&model);
				if (vkModel) {
					modelRenderer->AddModel(vkModel, param);
				} else {
					SPLog("Warning: Model is not a VulkanModel, skipping");
				}
			}
		}

		void VulkanRenderer::EndScene() {
			SPADES_MARK_FUNCTION();
			EnsureSceneStarted();

			if (sceneUsedInThisFrame) {
				// Calculate delta time for animations
				float dt = (float)(sceneDef.time - lastTime) / 1000.0f;
				if (dt > 0.1f) dt = 0.1f; // Cap dt to avoid large jumps
				if (dt < 0.0f) dt = 0.0f; // Handle timer wrap-around
				if (lastTime == 0) dt = 0.0f; // No animation on the first frame
				lastDt = dt;

		}

			// Reset the fence for this frame slot (waited on in StartScene)
			vkResetFences(device->GetDevice(), 1, &inFlightFences[currentFrameSlot]);

			// Record command buffer for this frame
			RecordCommandBuffer(currentImageIndex);

			// Submit command buffer
			VkSubmitInfo submitInfo{};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

			VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
			VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
			submitInfo.waitSemaphoreCount = 1;
			submitInfo.pWaitSemaphores = waitSemaphores;
			submitInfo.pWaitDstStageMask = waitStages;

			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffers[currentImageIndex];

			VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
			submitInfo.signalSemaphoreCount = 1;
			submitInfo.pSignalSemaphores = signalSemaphores;

			VkResult result = vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, inFlightFences[currentFrameSlot]);
			if (result != VK_SUCCESS) {
				SPLog("Warning: Failed to submit draw command buffer (error code: %d)", result);
			}

			duringSceneRendering = false;
		}

		void VulkanRenderer::MultiplyScreenColor(Vector3 color) {
			SPADES_MARK_FUNCTION();
			EnsureSceneNotStarted();
			pendingMultiplyColors.push_back(color);
		}

		void VulkanRenderer::SetColor(Vector4 color) {
			drawColorAlphaPremultiplied = color;
			legacyColorPremultiply = true;
		}

		void VulkanRenderer::SetColorAlphaPremultiplied(Vector4 color) {
			legacyColorPremultiply = false;
			drawColorAlphaPremultiplied = color;
		}

		void VulkanRenderer::DrawImage(stmp::optional<client::IImage&> image, const Vector2& outTopLeft) {
			SPADES_MARK_FUNCTION();

			if (!image) {
				SPRaise("Null image provided to DrawImage");
				return;
			}

			DrawImage(image,
			          AABB2(outTopLeft.x, outTopLeft.y, image->GetWidth(), image->GetHeight()),
			          AABB2(0, 0, image->GetWidth(), image->GetHeight()));
		}

		void VulkanRenderer::DrawImage(stmp::optional<client::IImage&> image, const AABB2& outRect) {
			SPADES_MARK_FUNCTION();

			DrawImage(image, outRect,
			          AABB2(0, 0, image ? image->GetWidth() : 0, image ? image->GetHeight() : 0));
		}

		void VulkanRenderer::DrawImage(stmp::optional<client::IImage&> image, const Vector2& outTopLeft, const AABB2& inRect) {
			SPADES_MARK_FUNCTION();

			DrawImage(image,
			          AABB2(outTopLeft.x, outTopLeft.y, inRect.GetWidth(), inRect.GetHeight()),
			          inRect);
		}

		void VulkanRenderer::DrawImage(stmp::optional<client::IImage&> image, const AABB2& outRect, const AABB2& inRect) {
			SPADES_MARK_FUNCTION();

			DrawImage(image, Vector2::Make(outRect.GetMinX(), outRect.GetMinY()),
			          Vector2::Make(outRect.GetMaxX(), outRect.GetMinY()),
			          Vector2::Make(outRect.GetMinX(), outRect.GetMaxY()), inRect);
		}

		void VulkanRenderer::DrawImage(stmp::optional<client::IImage&> image, const Vector2& outTopLeft,
		                                const Vector2& outTopRight, const Vector2& outBottomLeft,
		                                const AABB2& inRect) {
			SPADES_MARK_FUNCTION();

			EnsureSceneNotStarted();

			// d = a + (b - a) + (c - a)
			//   = b + c - a
			Vector2 outBottomRight = outTopRight + outBottomLeft - outTopLeft;

			VulkanImage* img = nullptr;

			// Try to get VulkanImage from VulkanImageWrapper
			VulkanImageWrapper* wrapper = dynamic_cast<VulkanImageWrapper*>(image.get_pointer());
			if (wrapper) {
				img = wrapper->GetVulkanImage();
			} else {
				// Try direct cast to VulkanImage
				img = dynamic_cast<VulkanImage*>(image.get_pointer());
			}

			if (!img) {
				if (!image) {
					// Use white image for solid color rendering
					img = whiteImage.GetPointerOrNull();
					if (!img) {
						SPLog("DrawImage: Warning - white image not available");
						return;
					}
				} else {
					// invalid type: not VulkanImage or VulkanImageWrapper.
					SPLog("Warning: Unsupported image type in DrawImage, skipping");
					return;
				}
			}

			if (!imageRenderer) {
				// Image renderer not initialized yet
				SPLog("DrawImage: Skipping - imageRenderer not initialized");
				return;
			}

			imageRenderer->SetImage(img);

			Vector4 col = drawColorAlphaPremultiplied;
			if (legacyColorPremultiply) {
				// in legacy mode, image color is non alpha-premultiplied
				col.x *= col.w;
				col.y *= col.w;
				col.z *= col.w;
			}

			imageRenderer->Add(outTopLeft.x, outTopLeft.y, outTopRight.x, outTopRight.y,
			                   outBottomRight.x, outBottomRight.y, outBottomLeft.x, outBottomLeft.y,
			                   inRect.GetMinX(), inRect.GetMinY(), inRect.GetMaxX(),
			                   inRect.GetMinY(), inRect.GetMaxX(), inRect.GetMaxY(),
			                   inRect.GetMinX(), inRect.GetMaxY(), col.x, col.y, col.z, col.w);
		}

		void VulkanRenderer::UpdateFlatGameMap() {
			SPADES_MARK_FUNCTION();
			EnsureSceneNotStarted();
			if (flatMapRenderer)
				flatMapRenderer->UpdateChunks();
		}

		void VulkanRenderer::DrawFlatGameMap(const AABB2& outRect, const AABB2& inRect) {
			SPADES_MARK_FUNCTION();
			EnsureSceneNotStarted();
			if (flatMapRenderer)
				flatMapRenderer->Draw(outRect, inRect);
		}

		void VulkanRenderer::FrameDone() {
			SPADES_MARK_FUNCTION();

			if (!inited) {
				SPLog("[VulkanRenderer::FrameDone] Not initialized, skipping");
				return;
			}


			EnsureSceneNotStarted();

			// Flush image renderer like GLRenderer does
			// Note: We pass VK_NULL_HANDLE here since we'll record to command buffer later in Flip()
			// The Flush() call processes the batches but doesn't record to a command buffer yet
			if (imageRenderer) {
				// Store the batches for later recording in Flip()
				// Actually, we need to NOT flush here - just let batches accumulate
			}

			frameNumber++;
		}

		void VulkanRenderer::Flip() {
			SPADES_MARK_FUNCTION();

			if (!inited) {
				// Before initialization, just present black frames
				SPLog("[VulkanRenderer::Flip] Not initialized, presenting black frame");
				try {
					VkSemaphore dummySemaphore1 = VK_NULL_HANDLE;
					VkSemaphore dummySemaphore2 = VK_NULL_HANDLE;
					uint32_t imageIndex = device->AcquireNextImage(&dummySemaphore1, &dummySemaphore2);
					if (imageIndex != UINT32_MAX) {
						device->PresentImage(imageIndex, nullptr, 0);
					}
				} catch (...) {
					// Silently ignore errors during uninitialized presentation
				}
				return;
			}

		// Rebuild swapchain-dependent resources if the swapchain was recreated since last frame
		// (e.g. window resize triggered VK_ERROR_OUT_OF_DATE_KHR or VK_SUBOPTIMAL_KHR).
		if (device->GetSwapchainGeneration() != lastSwapchainGeneration) {
			RecreateSwapchainDependencies();
		}

			if (sceneUsedInThisFrame) {
				// Present the image (already rendered in EndScene)
				VkSemaphore waitSemaphores[] = {renderFinishedSemaphore};
						device->PresentImage(currentImageIndex, waitSemaphores, 1);
				
						lastTime = sceneDef.time;
						sceneUsedInThisFrame = false;			} else {
				// 2D-only rendering (like loading screen) - need to record and submit command buffer

				// Wait for the previous frame using this semaphore slot
				currentFrameSlot = device->GetCurrentFrame();
				vkWaitForFences(device->GetDevice(), 1, &inFlightFences[currentFrameSlot], VK_TRUE, UINT64_MAX);

				// Acquire next swapchain image
				currentImageIndex = device->AcquireNextImage(&imageAvailableSemaphore, &renderFinishedSemaphore);
				if (currentImageIndex == UINT32_MAX) {
					SPLog("[VulkanRenderer::Flip] Failed to acquire swapchain image");
					if (temporaryImagePool) { temporaryImagePool->ReleaseAll(); }
					return;
				}

				vkResetFences(device->GetDevice(), 1, &inFlightFences[currentFrameSlot]);

				// Record command buffer with the batched 2D images
				RecordCommandBuffer(currentImageIndex);

				// Submit command buffer
				VkSubmitInfo submitInfo{};
				submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

				VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
				VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
				submitInfo.waitSemaphoreCount = 1;
				submitInfo.pWaitSemaphores = waitSemaphores;
				submitInfo.pWaitDstStageMask = waitStages;

				submitInfo.commandBufferCount = 1;
				submitInfo.pCommandBuffers = &commandBuffers[currentImageIndex];

				VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
				submitInfo.signalSemaphoreCount = 1;
				submitInfo.pSignalSemaphores = signalSemaphores;

				VkResult result = vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, inFlightFences[currentFrameSlot]);
				if (result != VK_SUCCESS) {
					SPLog("[VulkanRenderer::Flip] Failed to submit command buffer (error code: %d)", result);
				}

				// Present the image with proper synchronization
				device->PresentImage(currentImageIndex, signalSemaphores, 1);
			}

			// Release all temporary images back to the pool for reuse next frame
			if (temporaryImagePool) {
				temporaryImagePool->ReleaseAll();
			}
		}

		Handle<Bitmap> VulkanRenderer::ReadBitmap() {
			SPADES_MARK_FUNCTION();

			if (!framebufferManager || !sceneUsedInThisFrame)
				return Handle<Bitmap>();

			Handle<VulkanImage> srcImage = framebufferManager->GetColorImage();
			if (!srcImage)
				return Handle<Bitmap>();

			// Block until the frame submission is complete so the image is safe to read.
			vkWaitForFences(device->GetDevice(), 1, &inFlightFences[currentFrameSlot],
			                VK_TRUE, UINT64_MAX);

			VkFormat fmt = framebufferManager->GetMainColorFormat();

			// Bytes per texel for the offscreen color format.
			uint32_t bytesPerPixel;
			switch (fmt) {
				case VK_FORMAT_R16G16B16A16_SFLOAT: bytesPerPixel = 8; break;
				default:                             bytesPerPixel = 4; break;
			}

			VkDeviceSize imageSize = (VkDeviceSize)renderWidth * renderHeight * bytesPerPixel;

			// Create host-visible staging buffer for the copy destination.
			Handle<VulkanBuffer> stagingBuffer(
			    new VulkanBuffer(device, imageSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
			    false);

			// One-shot command buffer: transition → copy → transition back.
			VkCommandBufferAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandPool = device->GetCommandPool();
			allocInfo.commandBufferCount = 1;

			VkCommandBuffer cmd;
			vkAllocateCommandBuffers(device->GetDevice(), &allocInfo, &cmd);

			VkCommandBufferBeginInfo beginInfo{};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			vkBeginCommandBuffer(cmd, &beginInfo);

			// Offscreen image is in SHADER_READ_ONLY_OPTIMAL at end of frame.
			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = srcImage->GetImage();
			barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

			barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
			                     1, &barrier);

			VkBufferImageCopy region{};
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.layerCount = 1;
			region.imageExtent = {(uint32_t)renderWidth, (uint32_t)renderHeight, 1};
			vkCmdCopyImageToBuffer(cmd, srcImage->GetImage(),
			                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			                       stagingBuffer->GetBuffer(), 1, &region);

			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
			                     nullptr, 1, &barrier);

			vkEndCommandBuffer(cmd);

			VkSubmitInfo submitInfo{};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &cmd;
			vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
			vkQueueWaitIdle(device->GetGraphicsQueue());
			vkFreeCommandBuffers(device->GetDevice(), device->GetCommandPool(), 1, &cmd);

			// Map and convert to Bitmap (always RGBA8 output).
			const void* raw = stagingBuffer->Map();

			Handle<Bitmap> bmp(new Bitmap(renderWidth, renderHeight), false);
			uint32_t* pixels = bmp->GetPixels();
			const int count = renderWidth * renderHeight;

			switch (fmt) {
				case VK_FORMAT_R8G8B8A8_UNORM:
				case VK_FORMAT_R8G8B8A8_SRGB:
					std::memcpy(pixels, raw, (size_t)count * 4);
					break;

				case VK_FORMAT_R16G16B16A16_SFLOAT: {
					// Half-float → uint8 with clamping.
					const uint16_t* src = reinterpret_cast<const uint16_t*>(raw);
					// Standard half→float via IEEE 754 bit manipulation (bias offset 127-15=112).
					auto halfToU8 = [](uint16_t h) -> uint8_t {
						uint32_t s = (h >> 15) & 1;
						uint32_t e = (h >> 10) & 0x1f;
						uint32_t m = h & 0x3ff;
						uint32_t bits;
						if (e == 0) {
							bits = (s << 31) | (m << 13); // ±0 or denorm (clamps to ~0)
						} else if (e == 31) {
							bits = (s << 31) | 0x7f800000u | (m << 13); // inf/NaN
						} else {
							bits = (s << 31) | ((e + 112) << 23) | (m << 13);
						}
						float f;
						std::memcpy(&f, &bits, 4);
						if (f < 0.0f) f = 0.0f;
						if (f > 1.0f) f = 1.0f;
						return static_cast<uint8_t>(f * 255.0f + 0.5f);
					};
					for (int i = 0; i < count; i++) {
						uint8_t r = halfToU8(src[i * 4 + 0]);
						uint8_t g = halfToU8(src[i * 4 + 1]);
						uint8_t b = halfToU8(src[i * 4 + 2]);
						uint8_t a = halfToU8(src[i * 4 + 3]);
						pixels[i] = r | (uint32_t(g) << 8) | (uint32_t(b) << 16) |
						            (uint32_t(a) << 24);
					}
					break;
				}

				case VK_FORMAT_A2B10G10R10_UNORM_PACK32: {
					// Bits: [31:30]=A [29:20]=B [19:10]=G [9:0]=R
					const uint32_t* src = reinterpret_cast<const uint32_t*>(raw);
					for (int i = 0; i < count; i++) {
						uint32_t p = src[i];
						uint8_t r = static_cast<uint8_t>(((p >> 0) & 0x3ff) * 255 / 1023);
						uint8_t g = static_cast<uint8_t>(((p >> 10) & 0x3ff) * 255 / 1023);
						uint8_t b = static_cast<uint8_t>(((p >> 20) & 0x3ff) * 255 / 1023);
						uint8_t a = static_cast<uint8_t>(((p >> 30) & 0x3) * 255 / 3);
						pixels[i] = r | (uint32_t(g) << 8) | (uint32_t(b) << 16) |
						            (uint32_t(a) << 24);
					}
					break;
				}

				default:
					// Unknown format: return solid black rather than garbage.
					std::memset(pixels, 0, (size_t)count * 4);
					break;
			}

			stagingBuffer->Unmap();
			return bmp;
		}

		float VulkanRenderer::ScreenWidth() {
			return static_cast<float>(renderWidth);
		}

		float VulkanRenderer::ScreenHeight() {
			return static_cast<float>(renderHeight);
		}

		void VulkanRenderer::GameMapChanged(int x, int y, int z, client::GameMap* map) {
			SPADES_MARK_FUNCTION();
			if (mapRenderer)
				mapRenderer->GameMapChanged(x, y, z, map);
			if (mapShadowRenderer)
				mapShadowRenderer->GameMapChanged(x, y, z, map);
			if (flatMapRenderer)
				flatMapRenderer->GameMapChanged(x, y, z, *map);
			}

		void VulkanRenderer::ProcessDeferredDeletions() {
			SPADES_MARK_FUNCTION();

			// Process deferred deletions for buffers that are no longer in use by the GPU
			// We keep buffers alive for MAX_FRAMES_IN_FLIGHT frames to ensure the GPU is done with them
			const uint32_t MAX_FRAMES_IN_FLIGHT = device->GetMaxFramesInFlight();

			auto it = deferredDeletions.begin();
			while (it != deferredDeletions.end()) {
				// Calculate how many frames have passed since this buffer was marked for deletion
				uint32_t framesPassed = frameNumber - it->frameIndex;

				// If enough frames have passed, the GPU is guaranteed to be done with this buffer
				if (framesPassed >= MAX_FRAMES_IN_FLIGHT) {
					// The buffer will be automatically destroyed when the Handle reference is released
					it = deferredDeletions.erase(it);
				} else {
					++it;
				}
			}
		}

		void VulkanRenderer::FlushPendingUploads() {
			if (pendingUploads.empty())
				return;

			// Collect all command buffers
			std::vector<VkCommandBuffer> cmdBufs;
			cmdBufs.reserve(pendingUploads.size());
			for (auto& u : pendingUploads)
				cmdBufs.push_back(u.commandBuffer);

			// Submit all uploads in a single batch
			VkSubmitInfo submitInfo{};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = (uint32_t)cmdBufs.size();
			submitInfo.pCommandBuffers = cmdBufs.data();

			VkFenceCreateInfo fenceInfo{};
			fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			VkFence fence;
			vkCreateFence(device->GetDevice(), &fenceInfo, nullptr, &fence);

			vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, fence);
			vkWaitForFences(device->GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
			vkDestroyFence(device->GetDevice(), fence, nullptr);

			vkFreeCommandBuffers(device->GetDevice(), device->GetCommandPool(),
			                     (uint32_t)cmdBufs.size(), cmdBufs.data());

			// Queue staging buffers for deferred deletion (GPU is done, but keep
			// them alive for MAX_FRAMES_IN_FLIGHT frames to be safe with
			// descriptor sets that may still reference layout metadata).
			for (auto& u : pendingUploads)
				QueueBufferForDeletion(u.stagingBuffer);

			pendingUploads.clear();
		}

				VkRenderPass VulkanRenderer::GetOffscreenRenderPass() const {
		return framebufferManager ? framebufferManager->GetRenderPass() : renderPass;
	}

	VkPipelineCache VulkanRenderer::GetPipelineCache() const {
		return pipelineCache ? pipelineCache->GetCache() : VK_NULL_HANDLE;
	}

	void VulkanRenderer::QueueBufferForDeletion(Handle<VulkanBuffer> buffer) {
			if (buffer) {
				DeferredDeletion deletion;
				deletion.buffer = buffer;
				deletion.frameIndex = frameNumber;
				deferredDeletions.push_back(deletion);
			}
		}

		void VulkanRenderer::RecordCommandBuffer(uint32_t imageIndex) {
			SPADES_MARK_FUNCTION();

			// Flush any pending texture uploads before recording render commands
			FlushPendingUploads();

			// Process deferred deletions at the start of each frame
			ProcessDeferredDeletions();

			// SPLog("[VulkanRenderer::RecordCommandBuffer] Recording commands for image index %u", imageIndex);

			VkCommandBuffer commandBuffer = commandBuffers[imageIndex];

			VkCommandBufferBeginInfo beginInfo{};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = 0;
			beginInfo.pInheritanceInfo = nullptr;

			VkResult result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
			if (result != VK_SUCCESS) {
				SPLog("Warning: Failed to begin recording command buffer");
				return;
			}

		// Realize map chunks BEFORE starting render pass (updates buffers outside render pass)
		if (sceneUsedInThisFrame && mapRenderer) {
			mapRenderer->Realize();

			// Insert memory barrier to ensure host writes are visible to vertex shader reads
			// This synchronizes CPU-side buffer updates (from Map/Unmap) with GPU vertex fetching
			VkMemoryBarrier memoryBarrier{};
			memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
			memoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
			memoryBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;

			vkCmdPipelineBarrier(
				commandBuffer,
				VK_PIPELINE_STAGE_HOST_BIT,
				VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
				0,
				1, &memoryBarrier,
				0, nullptr,
				0, nullptr
			);
		}

		// Update map shadow heightmap texture (incremental updates from block changes)
		if (sceneUsedInThisFrame && mapShadowRenderer) {
			mapShadowRenderer->Update(commandBuffer);
		}

		// Render shadow maps BEFORE starting main render pass (shadow maps use their own render passes)
		if (sceneUsedInThisFrame && shadowMapRenderer && r_fogShadow) {
			shadowMapRenderer->Render(commandBuffer);
		}

		// Render 3D scene to offscreen framebuffer
		if (sceneUsedInThisFrame && framebufferManager) {
			// Use fog color for solid pass (which may be black if fog shadow is enabled)
			Vector3 bgColor = GetFogColorForSolidPass();

			// Begin offscreen render pass (to framebuffer manager's render target)
			VkRenderPassBeginInfo offscreenRenderPassInfo{};
			offscreenRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			offscreenRenderPassInfo.renderPass = framebufferManager->GetRenderPass();
			offscreenRenderPassInfo.framebuffer = framebufferManager->GetRenderFramebuffer();
			offscreenRenderPassInfo.renderArea.offset = {0, 0};
			offscreenRenderPassInfo.renderArea.extent = {static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight)};

			VkClearValue clearValues[2];
			clearValues[0].color = {{bgColor.x, bgColor.y, bgColor.z, 1.0f}};
			clearValues[1].depthStencil = {1.0f, 0};
			offscreenRenderPassInfo.clearValueCount = 2;
			offscreenRenderPassInfo.pClearValues = clearValues;

			vkCmdBeginRenderPass(commandBuffer, &offscreenRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			// Set viewport and scissor for 3D rendering
			// Vulkan has inverted Y-axis compared to OpenGL, so we need to flip the viewport
			VkViewport viewport{};
			viewport.x = 0.0f;
			viewport.y = static_cast<float>(renderHeight);
			viewport.width = static_cast<float>(renderWidth);
			viewport.height = -static_cast<float>(renderHeight);
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

			VkRect2D scissor{};
			scissor.offset = {0, 0};
			scissor.extent = {static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight)};
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			// Render the 3D scene

			// Render sky gradient
			RenderSky(commandBuffer);

			// Render map
			if (!sceneDef.skipWorld && mapRenderer) {
				mapRenderer->RenderSunlightPass(commandBuffer);
			}

			// Render models
			if (modelRenderer) {
				modelRenderer->RenderSunlightPass(commandBuffer, false);
			}

			// Render dynamic lights (muzzle flash, flashlight, etc.)
			if (!lights.empty()) {
				std::vector<void*> lightPtrs;
				lightPtrs.reserve(lights.size());
				for (auto& l : lights) {
					lightPtrs.push_back(&l);
				}
				if (!sceneDef.skipWorld && mapRenderer) {
					mapRenderer->RenderDynamicLightPass(commandBuffer, lightPtrs);
				}
				if (modelRenderer) {
					modelRenderer->RenderDynamicLightPass(commandBuffer, lightPtrs);
				}
			}

			// Render outlines (wireframe back-faces with depth bias)
			if ((int)r_outlines) {
				if (!sceneDef.skipWorld && mapRenderer) {
					mapRenderer->RenderOutlinePass(commandBuffer);
				}
				if (modelRenderer) {
					modelRenderer->RenderOutlinePass(commandBuffer);
				}
			}

			// Ghost model pass: depth prepass then semi-transparent color pass
		if (modelRenderer) {
			modelRenderer->Prerender(commandBuffer, true);
			modelRenderer->RenderSunlightPass(commandBuffer, true);
		}

		bool useSoftParticles = spriteRenderer && spriteRenderer->IsSoftParticles();

			// Render sprites (non-soft mode: inside offscreen pass with hardware depth test)
			if (!useSoftParticles) {
				if (spriteRenderer) {
					spriteRenderer->Render(commandBuffer, imageIndex);
				}
			}
			// Long sprites always use hardware depth test (pipeline requires depth attachment)
			if (longSpriteRenderer) {
				longSpriteRenderer->Render(commandBuffer, imageIndex);
			}

			// Clear for next frame
			if (modelRenderer) {
				modelRenderer->Clear();
			}
			RenderDebugLines(commandBuffer);
			debugLines.clear();
			lights.clear();

			// End offscreen render pass (scene without water is now complete)
			vkCmdEndRenderPass(commandBuffer);

			Handle<VulkanImage> offscreenColor = framebufferManager->GetColorImage();
			Handle<VulkanImage> offscreenDepth = framebufferManager->GetDepthImage();

			if (useSoftParticles) {
				// Soft particles: transition depth to shader-readable, render sprites
				// in a color-only pass sampling the depth texture

				// Transition depth to SHADER_READ_ONLY for sampling
				VkImageMemoryBarrier depthBarrier{};
				depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				depthBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				depthBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				depthBarrier.image = offscreenDepth->GetImage();
				depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				depthBarrier.subresourceRange.baseMipLevel = 0;
				depthBarrier.subresourceRange.levelCount = 1;
				depthBarrier.subresourceRange.baseArrayLayer = 0;
				depthBarrier.subresourceRange.layerCount = 1;
				depthBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				depthBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

				vkCmdPipelineBarrier(commandBuffer,
					VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &depthBarrier);

				// Begin sprite render pass (color-only, preserves existing content)
				VkRenderPassBeginInfo spriteRenderPassInfo{};
				spriteRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				spriteRenderPassInfo.renderPass = framebufferManager->GetSpriteRenderPass();
				spriteRenderPassInfo.framebuffer = framebufferManager->GetSpriteFramebuffer();
				spriteRenderPassInfo.renderArea.offset = {0, 0};
				spriteRenderPassInfo.renderArea.extent = {static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight)};
				spriteRenderPassInfo.clearValueCount = 0;
				spriteRenderPassInfo.pClearValues = nullptr;

				vkCmdBeginRenderPass(commandBuffer, &spriteRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

				// Set viewport and scissor
				VkViewport spriteViewport{};
				spriteViewport.x = 0.0f;
				spriteViewport.y = static_cast<float>(renderHeight);
				spriteViewport.width = static_cast<float>(renderWidth);
				spriteViewport.height = -static_cast<float>(renderHeight);
				spriteViewport.minDepth = 0.0f;
				spriteViewport.maxDepth = 1.0f;
				vkCmdSetViewport(commandBuffer, 0, 1, &spriteViewport);

				VkRect2D spriteScissor{};
				spriteScissor.offset = {0, 0};
				spriteScissor.extent = {static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight)};
				vkCmdSetScissor(commandBuffer, 0, 1, &spriteScissor);

				// Render soft sprites
				if (spriteRenderer) {
					spriteRenderer->Render(commandBuffer, imageIndex);
				}

				vkCmdEndRenderPass(commandBuffer);

				// Transition color to SHADER_READ_ONLY for water/post-processing
				// (depth is already in SHADER_READ_ONLY)
				VkImageMemoryBarrier colorBarrier{};
				colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				colorBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				colorBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				colorBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				colorBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				colorBarrier.image = offscreenColor->GetImage();
				colorBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				colorBarrier.subresourceRange.baseMipLevel = 0;
				colorBarrier.subresourceRange.levelCount = 1;
				colorBarrier.subresourceRange.baseArrayLayer = 0;
				colorBarrier.subresourceRange.layerCount = 1;
				colorBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				colorBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

				vkCmdPipelineBarrier(commandBuffer,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &colorBarrier);
			} else {
				// Non-soft: transition both color and depth to shader read-only
				VkImageMemoryBarrier barriers[2]{};
				barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				barriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barriers[0].image = offscreenColor->GetImage();
				barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				barriers[0].subresourceRange.baseMipLevel = 0;
				barriers[0].subresourceRange.levelCount = 1;
				barriers[0].subresourceRange.baseArrayLayer = 0;
				barriers[0].subresourceRange.layerCount = 1;
				barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

				barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				barriers[1].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barriers[1].image = offscreenDepth->GetImage();
				barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				barriers[1].subresourceRange.baseMipLevel = 0;
				barriers[1].subresourceRange.levelCount = 1;
				barriers[1].subresourceRange.baseArrayLayer = 0;
				barriers[1].subresourceRange.layerCount = 1;
				barriers[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

				vkCmdPipelineBarrier(commandBuffer,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, 0, nullptr, 0, nullptr, 2, barriers);
			}

			// Clear sprites after rendering (whether soft or not)
			if (spriteRenderer) {
				spriteRenderer->Clear();
			}
			if (longSpriteRenderer) {
				longSpriteRenderer->Clear();
			}

			// --- Blit offscreen image to swapchain ---
			// Transition offscreen color from SHADER_READ_ONLY to TRANSFER_SRC
			VkImageMemoryBarrier barrier1{};
			barrier1.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier1.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier1.image = offscreenColor->GetImage();
			barrier1.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier1.subresourceRange.baseMipLevel = 0;
			barrier1.subresourceRange.levelCount = 1;
			barrier1.subresourceRange.baseArrayLayer = 0;
			barrier1.subresourceRange.layerCount = 1;
			barrier1.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			barrier1.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier1);

			// Transition swapchain image for transfer destination
			VkImageMemoryBarrier barrier2{};
			barrier2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier2.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier2.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier2.image = device->GetSwapchainImage(imageIndex);
			barrier2.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier2.subresourceRange.baseMipLevel = 0;
			barrier2.subresourceRange.levelCount = 1;
			barrier2.subresourceRange.baseArrayLayer = 0;
			barrier2.subresourceRange.layerCount = 1;
			barrier2.srcAccessMask = 0;
			barrier2.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier2);

			// Blit final post-process image to swapchain
			VkImageBlit blitRegion{};
			blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blitRegion.srcSubresource.layerCount = 1;
			blitRegion.srcOffsets[0] = {0, 0, 0};
			blitRegion.srcOffsets[1] = {renderWidth, renderHeight, 1};
			blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blitRegion.dstSubresource.layerCount = 1;
			blitRegion.dstOffsets[0] = {0, 0, 0};
			blitRegion.dstOffsets[1] = {static_cast<int32_t>(device->GetSwapchainExtent().width),
			                            static_cast<int32_t>(device->GetSwapchainExtent().height), 1};

			vkCmdBlitImage(commandBuffer,
				offscreenColor->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				device->GetSwapchainImage(imageIndex), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blitRegion, VK_FILTER_LINEAR);

			// Transition offscreen color back to SHADER_READ_ONLY for next frame
			barrier1.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier1.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier1.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier1.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier1);

			// Transition swapchain image to color attachment for UI rendering
			barrier2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier2.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			barrier2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier2.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier2);
		}

		// Begin swapchain render pass for 2D UI rendering
		VkRenderPassBeginInfo swapchainRenderPassInfo{};
		swapchainRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		swapchainRenderPassInfo.renderPass = renderPass;
		swapchainRenderPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
		swapchainRenderPassInfo.renderArea.offset = {0, 0};
		swapchainRenderPassInfo.renderArea.extent = device->GetSwapchainExtent();
		swapchainRenderPassInfo.clearValueCount = 0; // No clear, using LOAD_OP_LOAD
		swapchainRenderPassInfo.pClearValues = nullptr;

		vkCmdBeginRenderPass(commandBuffer, &swapchainRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		// Render fullscreen multiply-color tints (hit flash, underwater tint, etc.)
		RenderMultiplyColors(commandBuffer);

		// Flush image renderer to record 2D drawing commands (UI)
		if (imageRenderer) {
			imageRenderer->Flush(commandBuffer, imageIndex);
		}

		vkCmdEndRenderPass(commandBuffer);

			vkEndCommandBuffer(commandBuffer);
		}

		void VulkanRenderer::CreateSkyPipeline() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			// Load SPIR-V shaders
			auto LoadSPIRVFile = [](const char* filename) -> std::vector<uint32_t> {
				std::unique_ptr<IStream> stream = FileManager::OpenForReading(filename);
				if (!stream) {
					SPRaise("Failed to open shader file: %s", filename);
				}
				size_t size = stream->GetLength();
				std::vector<uint32_t> code(size / 4);
				stream->Read(code.data(), size);
				return code;
			};

			std::vector<uint32_t> vertCode = LoadSPIRVFile("Shaders/Vulkan/Sky.vert.spv");
			std::vector<uint32_t> fragCode = LoadSPIRVFile("Shaders/Vulkan/Sky.frag.spv");

			// Create shader modules
			VkShaderModuleCreateInfo vertShaderModuleInfo{};
			vertShaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			vertShaderModuleInfo.codeSize = vertCode.size() * sizeof(uint32_t);
			vertShaderModuleInfo.pCode = vertCode.data();

			VkShaderModule vertShaderModule;
			VkResult result = vkCreateShaderModule(vkDevice, &vertShaderModuleInfo, nullptr, &vertShaderModule);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create sky vertex shader module (error code: %d)", result);
			}

			VkShaderModuleCreateInfo fragShaderModuleInfo{};
			fragShaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			fragShaderModuleInfo.codeSize = fragCode.size() * sizeof(uint32_t);
			fragShaderModuleInfo.pCode = fragCode.data();

			VkShaderModule fragShaderModule;
			result = vkCreateShaderModule(vkDevice, &fragShaderModuleInfo, nullptr, &fragShaderModule);
			if (result != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
				SPRaise("Failed to create sky fragment shader module (error code: %d)", result);
			}

			// Shader stages
			VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
			vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
			vertShaderStageInfo.module = vertShaderModule;
			vertShaderStageInfo.pName = "main";

			VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
			fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			fragShaderStageInfo.module = fragShaderModule;
			fragShaderStageInfo.pName = "main";

			VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

			// Vertex input - 2D position
			VkVertexInputBindingDescription bindingDescription{};
			bindingDescription.binding = 0;
			bindingDescription.stride = sizeof(float) * 2;
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			VkVertexInputAttributeDescription attributeDescription{};
			attributeDescription.binding = 0;
			attributeDescription.location = 0;
			attributeDescription.format = VK_FORMAT_R32G32_SFLOAT;
			attributeDescription.offset = 0;

			VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
			vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputInfo.vertexBindingDescriptionCount = 1;
			vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
			vertexInputInfo.vertexAttributeDescriptionCount = 1;
			vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

			// Input assembly
			VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
			inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			inputAssembly.primitiveRestartEnable = VK_FALSE;

			// Viewport state (dynamic)
			VkPipelineViewportStateCreateInfo viewportState{};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.scissorCount = 1;

			// Rasterization
			VkPipelineRasterizationStateCreateInfo rasterizer{};
			rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizer.depthClampEnable = VK_FALSE;
			rasterizer.rasterizerDiscardEnable = VK_FALSE;
			rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = VK_CULL_MODE_NONE;
			rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
			rasterizer.depthBiasEnable = VK_FALSE;

			// Multisampling
			VkPipelineMultisampleStateCreateInfo multisampling{};
			multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampling.sampleShadingEnable = VK_FALSE;
			multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			// Depth stencil - no depth test, sky is always in background
			VkPipelineDepthStencilStateCreateInfo depthStencil{};
			depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencil.depthTestEnable = VK_FALSE;
			depthStencil.depthWriteEnable = VK_FALSE;
			depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
			depthStencil.depthBoundsTestEnable = VK_FALSE;
			depthStencil.stencilTestEnable = VK_FALSE;

			// Color blending
			VkPipelineColorBlendAttachmentState colorBlendAttachment{};
			colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			colorBlendAttachment.blendEnable = VK_FALSE;

			VkPipelineColorBlendStateCreateInfo colorBlending{};
			colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlending.logicOpEnable = VK_FALSE;
			colorBlending.attachmentCount = 1;
			colorBlending.pAttachments = &colorBlendAttachment;

			// Dynamic state
			VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
			VkPipelineDynamicStateCreateInfo dynamicState{};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = 2;
			dynamicState.pDynamicStates = dynamicStates;

			// Push constants for sky parameters
			VkPushConstantRange pushConstantRange{};
			pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			pushConstantRange.offset = 0;
			pushConstantRange.size = sizeof(float) * 18;  // fogColor(3+1) + 3 view axes(3+1 each) + fovX + fovY

			VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
			pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutInfo.setLayoutCount = 0;
			pipelineLayoutInfo.pushConstantRangeCount = 1;
			pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

			result = vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &skyPipelineLayout);
			if (result != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
				vkDestroyShaderModule(vkDevice, fragShaderModule, nullptr);
				SPRaise("Failed to create sky pipeline layout (error code: %d)", result);
			}

			// Create graphics pipeline
			VkGraphicsPipelineCreateInfo pipelineInfo{};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = 2;
			pipelineInfo.pStages = shaderStages;
			pipelineInfo.pVertexInputState = &vertexInputInfo;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterizer;
			pipelineInfo.pMultisampleState = &multisampling;
			pipelineInfo.pDepthStencilState = &depthStencil;
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.pDynamicState = &dynamicState;
			pipelineInfo.layout = skyPipelineLayout;
			pipelineInfo.renderPass = framebufferManager->GetRenderPass(); // Use offscreen render pass for 3D rendering
			pipelineInfo.subpass = 0;

			result = vkCreateGraphicsPipelines(vkDevice, GetPipelineCache(), 1, &pipelineInfo, nullptr, &skyPipeline);

			vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
			vkDestroyShaderModule(vkDevice, fragShaderModule, nullptr);

			if (result != VK_SUCCESS) {
				SPRaise("Failed to create sky graphics pipeline (error code: %d)", result);
			}

			// Create fullscreen quad buffers
			struct Vertex {
				float x, y;
			};

			Vertex vertices[] = {
				{-1.0f, -1.0f},
				{1.0f, -1.0f},
				{-1.0f, 1.0f},
				{1.0f, 1.0f}
			};

			uint16_t indices[] = {0, 1, 2, 2, 1, 3};

			skyVertexBuffer = Handle<VulkanBuffer>::New(
				device,
				sizeof(vertices),
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);
			skyVertexBuffer->UpdateData(vertices, sizeof(vertices));

			skyIndexBuffer = Handle<VulkanBuffer>::New(
				device,
				sizeof(indices),
				VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);
			skyIndexBuffer->UpdateData(indices, sizeof(indices));

			SPLog("Sky pipeline created successfully");
		}

		void VulkanRenderer::DestroySkyPipeline() {
			VkDevice vkDevice = device->GetDevice();

			if (skyPipeline != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, skyPipeline, nullptr);
				skyPipeline = VK_NULL_HANDLE;
			}

			if (skyPipelineLayout != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, skyPipelineLayout, nullptr);
				skyPipelineLayout = VK_NULL_HANDLE;
			}

			skyVertexBuffer.Set(nullptr, false);
			skyIndexBuffer.Set(nullptr, false);
		}

		void VulkanRenderer::CreateMultiplyColorPipeline() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			// Load shaders
			auto LoadSPIRVFile = [](const char* filename) -> std::vector<uint32_t> {
				std::unique_ptr<IStream> stream = FileManager::OpenForReading(filename);
				if (!stream) {
					SPRaise("Failed to open shader file: %s", filename);
				}
				size_t size = stream->GetLength();
				std::vector<uint32_t> code(size / 4);
				stream->Read(code.data(), size);
				return code;
			};

			std::vector<uint32_t> vertCode = LoadSPIRVFile("Shaders/Vulkan/MultiplyColor.vert.spv");
			std::vector<uint32_t> fragCode = LoadSPIRVFile("Shaders/Vulkan/MultiplyColor.frag.spv");

			VkShaderModuleCreateInfo vertInfo{};
			vertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			vertInfo.codeSize = vertCode.size() * sizeof(uint32_t);
			vertInfo.pCode = vertCode.data();
			VkShaderModule vertModule;
			if (vkCreateShaderModule(vkDevice, &vertInfo, nullptr, &vertModule) != VK_SUCCESS) {
				SPRaise("Failed to create MultiplyColor vertex shader module");
			}

			VkShaderModuleCreateInfo fragInfo{};
			fragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			fragInfo.codeSize = fragCode.size() * sizeof(uint32_t);
			fragInfo.pCode = fragCode.data();
			VkShaderModule fragModule;
			if (vkCreateShaderModule(vkDevice, &fragInfo, nullptr, &fragModule) != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertModule, nullptr);
				SPRaise("Failed to create MultiplyColor fragment shader module");
			}

			VkPipelineShaderStageCreateInfo stages[2]{};
			stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			stages[0].module = vertModule;
			stages[0].pName = "main";
			stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[1].module = fragModule;
			stages[1].pName = "main";

			// No vertex input — fullscreen triangle generated from gl_VertexIndex
			VkPipelineVertexInputStateCreateInfo vertexInput{};
			vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

			VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
			inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			VkPipelineViewportStateCreateInfo viewportState{};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.scissorCount = 1;

			VkPipelineRasterizationStateCreateInfo rasterizer{};
			rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = VK_CULL_MODE_NONE;
			rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

			VkPipelineMultisampleStateCreateInfo multisampling{};
			multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			// No depth test — this is a 2D post-process over the swapchain image
			VkPipelineDepthStencilStateCreateInfo depthStencil{};
			depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencil.depthTestEnable = VK_FALSE;
			depthStencil.depthWriteEnable = VK_FALSE;

			// Multiplicative blend: dst = dst * src_color  (ZERO, SRC_COLOR)
			VkPipelineColorBlendAttachmentState blendAttachment{};
			blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			blendAttachment.blendEnable = VK_TRUE;
			blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
			blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
			blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
			blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

			VkPipelineColorBlendStateCreateInfo colorBlending{};
			colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlending.attachmentCount = 1;
			colorBlending.pAttachments = &blendAttachment;

			VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
			VkPipelineDynamicStateCreateInfo dynamicState{};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = 2;
			dynamicState.pDynamicStates = dynamicStates;

			// Push constant: vec3 color (fragment stage)
			VkPushConstantRange pushConstantRange{};
			pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			pushConstantRange.offset = 0;
			pushConstantRange.size = sizeof(float) * 3;

			VkPipelineLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layoutInfo.pushConstantRangeCount = 1;
			layoutInfo.pPushConstantRanges = &pushConstantRange;

			VkResult result = vkCreatePipelineLayout(vkDevice, &layoutInfo, nullptr, &multiplyColorPipelineLayout);
			if (result != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertModule, nullptr);
				vkDestroyShaderModule(vkDevice, fragModule, nullptr);
				SPRaise("Failed to create MultiplyColor pipeline layout (error: %d)", result);
			}

			VkGraphicsPipelineCreateInfo pipelineInfo{};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = 2;
			pipelineInfo.pStages = stages;
			pipelineInfo.pVertexInputState = &vertexInput;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterizer;
			pipelineInfo.pMultisampleState = &multisampling;
			pipelineInfo.pDepthStencilState = &depthStencil;
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.pDynamicState = &dynamicState;
			pipelineInfo.layout = multiplyColorPipelineLayout;
			pipelineInfo.renderPass = renderPass;
			pipelineInfo.subpass = 0;

			result = vkCreateGraphicsPipelines(vkDevice, GetPipelineCache(), 1, &pipelineInfo, nullptr, &multiplyColorPipeline);

			vkDestroyShaderModule(vkDevice, vertModule, nullptr);
			vkDestroyShaderModule(vkDevice, fragModule, nullptr);

			if (result != VK_SUCCESS) {
				SPRaise("Failed to create MultiplyColor pipeline (error: %d)", result);
			}
		}

		void VulkanRenderer::DestroyMultiplyColorPipeline() {
			VkDevice vkDevice = device->GetDevice();
			if (multiplyColorPipeline != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, multiplyColorPipeline, nullptr);
				multiplyColorPipeline = VK_NULL_HANDLE;
			}
			if (multiplyColorPipelineLayout != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, multiplyColorPipelineLayout, nullptr);
				multiplyColorPipelineLayout = VK_NULL_HANDLE;
			}
		}

		void VulkanRenderer::RenderMultiplyColors(VkCommandBuffer commandBuffer) {
			if (pendingMultiplyColors.empty() || multiplyColorPipeline == VK_NULL_HANDLE) {
				pendingMultiplyColors.clear();
				return;
			}

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, multiplyColorPipeline);

			VkExtent2D extent = device->GetSwapchainExtent();
			VkViewport viewport{};
			viewport.x = 0.0f;
			viewport.y = 0.0f;
			viewport.width = static_cast<float>(extent.width);
			viewport.height = static_cast<float>(extent.height);
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

			VkRect2D scissor{};
			scissor.offset = {0, 0};
			scissor.extent = extent;
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			for (const Vector3& color : pendingMultiplyColors) {
				float pushConstants[3] = {color.x, color.y, color.z};
				vkCmdPushConstants(commandBuffer, multiplyColorPipelineLayout,
				                   VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants), pushConstants);
				vkCmdDraw(commandBuffer, 3, 1, 0, 0);
			}

			pendingMultiplyColors.clear();
		}

		void VulkanRenderer::RenderSky(VkCommandBuffer commandBuffer) {
			if (skyPipeline == VK_NULL_HANDLE) {
				return;
			}

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline);

			// Push constants: fogColor, viewAxisFront, viewAxisUp, viewAxisSide, fovX, fovY
			struct SkyPushConstants {
				float fogColor[3];
				float _pad0;
				float viewAxisFront[3];
				float _pad1;
				float viewAxisUp[3];
				float _pad2;
				float viewAxisSide[3];
				float _pad3;
				float fovX;
				float fovY;
			} pushConstants;

			// Sky should always use the actual fog color, not the shadow-modified color
			// Don't linearize - the SRGB framebuffer handles gamma correction automatically

			pushConstants.fogColor[0] = fogColor.x;
			pushConstants.fogColor[1] = fogColor.y;
			pushConstants.fogColor[2] = fogColor.z;

			pushConstants.viewAxisFront[0] = sceneDef.viewAxis[2].x;
			pushConstants.viewAxisFront[1] = sceneDef.viewAxis[2].y;
			pushConstants.viewAxisFront[2] = sceneDef.viewAxis[2].z;

			pushConstants.viewAxisUp[0] = sceneDef.viewAxis[1].x;
			pushConstants.viewAxisUp[1] = sceneDef.viewAxis[1].y;
			pushConstants.viewAxisUp[2] = sceneDef.viewAxis[1].z;

			pushConstants.viewAxisSide[0] = sceneDef.viewAxis[0].x;
			pushConstants.viewAxisSide[1] = sceneDef.viewAxis[0].y;
			pushConstants.viewAxisSide[2] = sceneDef.viewAxis[0].z;

			pushConstants.fovX = sceneDef.fovX;
			pushConstants.fovY = sceneDef.fovY;

			vkCmdPushConstants(
				commandBuffer,
				skyPipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0,
				sizeof(pushConstants),
				&pushConstants
			);

			VkBuffer vertexBuffers[] = {skyVertexBuffer->GetBuffer()};
			VkDeviceSize offsets[] = {0};
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
			vkCmdBindIndexBuffer(commandBuffer, skyIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

			vkCmdDrawIndexed(commandBuffer, 6, 1, 0, 0, 0);
		}

		void VulkanRenderer::CreateDebugLinePipeline() {
			VkDevice vkDevice = device->GetDevice();

			// Load shaders
			auto LoadSPV = [&](const char* path) -> std::vector<uint32_t> {
				std::unique_ptr<IStream> s = FileManager::OpenForReading(path);
				if (!s) SPRaise("Failed to open shader: %s", path);
				size_t size = s->GetLength();
				std::vector<uint32_t> code(size / 4);
				s->Read(code.data(), size);
				return code;
			};

			auto vertCode = LoadSPV("Shaders/Vulkan/DebugLine.vert.spv");
			auto fragCode = LoadSPV("Shaders/Vulkan/DebugLine.frag.spv");

			VkShaderModuleCreateInfo vertInfo{};
			vertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			vertInfo.codeSize = vertCode.size() * sizeof(uint32_t);
			vertInfo.pCode = vertCode.data();
			VkShaderModule vertModule;
			if (vkCreateShaderModule(vkDevice, &vertInfo, nullptr, &vertModule) != VK_SUCCESS) {
				SPLog("Warning: Failed to create debug line vertex shader module");
				return;
			}

			VkShaderModuleCreateInfo fragInfo{};
			fragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			fragInfo.codeSize = fragCode.size() * sizeof(uint32_t);
			fragInfo.pCode = fragCode.data();
			VkShaderModule fragModule;
			if (vkCreateShaderModule(vkDevice, &fragInfo, nullptr, &fragModule) != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertModule, nullptr);
				SPLog("Warning: Failed to create debug line fragment shader module");
				return;
			}

			VkPipelineShaderStageCreateInfo stages[2]{};
			stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			stages[0].module = vertModule;
			stages[0].pName = "main";
			stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[1].module = fragModule;
			stages[1].pName = "main";

			// Vertex input: vec3 position (loc 0), vec4 color (loc 1)
			VkVertexInputBindingDescription binding{};
			binding.binding = 0;
			binding.stride = sizeof(float) * 7; // vec3 + vec4
			binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			VkVertexInputAttributeDescription attrs[2]{};
			attrs[0].binding = 0;
			attrs[0].location = 0;
			attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
			attrs[0].offset = 0;
			attrs[1].binding = 0;
			attrs[1].location = 1;
			attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
			attrs[1].offset = sizeof(float) * 3;

			VkPipelineVertexInputStateCreateInfo vertexInput{};
			vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInput.vertexBindingDescriptionCount = 1;
			vertexInput.pVertexBindingDescriptions = &binding;
			vertexInput.vertexAttributeDescriptionCount = 2;
			vertexInput.pVertexAttributeDescriptions = attrs;

			VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
			inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
			inputAssembly.primitiveRestartEnable = VK_FALSE;

			VkPipelineViewportStateCreateInfo viewportState{};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.scissorCount = 1;

			VkPipelineRasterizationStateCreateInfo rasterizer{};
			rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = VK_CULL_MODE_NONE;
			rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

			VkPipelineMultisampleStateCreateInfo multisampling{};
			multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			VkPipelineDepthStencilStateCreateInfo depthStencil{};
			depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencil.depthTestEnable = VK_FALSE;
			depthStencil.depthWriteEnable = VK_FALSE;
			depthStencil.stencilTestEnable = VK_FALSE;

			VkPipelineColorBlendAttachmentState blendAttachment{};
			blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			blendAttachment.blendEnable = VK_TRUE;
			blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
			blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

			VkPipelineColorBlendStateCreateInfo colorBlending{};
			colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlending.attachmentCount = 1;
			colorBlending.pAttachments = &blendAttachment;

			VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
			VkPipelineDynamicStateCreateInfo dynamicState{};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = 2;
			dynamicState.pDynamicStates = dynamicStates;

			VkPushConstantRange pcRange{};
			pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			pcRange.offset = 0;
			pcRange.size = sizeof(float) * 16; // mat4

			VkPipelineLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layoutInfo.pushConstantRangeCount = 1;
			layoutInfo.pPushConstantRanges = &pcRange;

			if (vkCreatePipelineLayout(vkDevice, &layoutInfo, nullptr, &debugLinePipelineLayout) != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertModule, nullptr);
				vkDestroyShaderModule(vkDevice, fragModule, nullptr);
				SPLog("Warning: Failed to create debug line pipeline layout");
				return;
			}

			VkGraphicsPipelineCreateInfo pipelineInfo{};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = 2;
			pipelineInfo.pStages = stages;
			pipelineInfo.pVertexInputState = &vertexInput;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterizer;
			pipelineInfo.pMultisampleState = &multisampling;
			pipelineInfo.pDepthStencilState = &depthStencil;
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.pDynamicState = &dynamicState;
			pipelineInfo.layout = debugLinePipelineLayout;
			pipelineInfo.renderPass = GetOffscreenRenderPass();
			pipelineInfo.subpass = 0;

			VkResult result = vkCreateGraphicsPipelines(vkDevice, GetPipelineCache(), 1, &pipelineInfo, nullptr, &debugLinePipeline);

			vkDestroyShaderModule(vkDevice, vertModule, nullptr);
			vkDestroyShaderModule(vkDevice, fragModule, nullptr);

			if (result != VK_SUCCESS) {
				SPLog("Warning: Failed to create debug line pipeline (error code: %d)", result);
				debugLinePipeline = VK_NULL_HANDLE;
			} else {
				SPLog("Debug line pipeline created successfully");
			}
		}

		void VulkanRenderer::DestroyDebugLinePipeline() {
			VkDevice vkDevice = device->GetDevice();
			if (debugLinePipeline != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, debugLinePipeline, nullptr);
				debugLinePipeline = VK_NULL_HANDLE;
			}
			if (debugLinePipelineLayout != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, debugLinePipelineLayout, nullptr);
				debugLinePipelineLayout = VK_NULL_HANDLE;
			}
		}

		void VulkanRenderer::RenderDebugLines(VkCommandBuffer commandBuffer) {
			if (debugLines.empty() || debugLinePipeline == VK_NULL_HANDLE)
				return;

			// Build flat vertex buffer: 2 vertices per line, each vertex = vec3 pos + vec4 color
			struct LineVertex { float x, y, z, r, g, b, a; };
			std::vector<LineVertex> vertices;
			vertices.reserve(debugLines.size() * 2);
			for (const auto& line : debugLines) {
				vertices.push_back({line.v1.x, line.v1.y, line.v1.z, line.color.x, line.color.y, line.color.z, line.color.w});
				vertices.push_back({line.v2.x, line.v2.y, line.v2.z, line.color.x, line.color.y, line.color.z, line.color.w});
			}

			Handle<VulkanBuffer> vertexBuffer(new VulkanBuffer(
				device,
				vertices.size() * sizeof(LineVertex),
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			));
			vertexBuffer->UpdateData(vertices.data(), vertices.size() * sizeof(LineVertex));

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, debugLinePipeline);

			VkViewport viewport{0.0f, (float)renderHeight, (float)renderWidth, -(float)renderHeight, 0.0f, 1.0f};
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
			VkRect2D scissor{{0, 0}, {static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight)}};
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			const Matrix4& mvp = GetProjectionViewMatrix();
			vkCmdPushConstants(commandBuffer, debugLinePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
			                   0, sizeof(float) * 16, &mvp);

			VkBuffer vb = vertexBuffer->GetBuffer();
			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, &offset);

			vkCmdDraw(commandBuffer, static_cast<uint32_t>(vertices.size()), 1, 0, 0);

			QueueBufferForDeletion(vertexBuffer);
		}

	} // namespace draw
} // namespace spades
