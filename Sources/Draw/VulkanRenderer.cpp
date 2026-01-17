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
#include "VulkanImageRenderer.h"
#include "VulkanWaterRenderer.h"
#include "VulkanFlatMapRenderer.h"
#include "VulkanFramebufferManager.h"
#include "VulkanImageWrapper.h"
#include "VulkanImageManager.h"
#include "VulkanImage.h"
#include "VulkanBuffer.h"
#include "VulkanOptimizedVoxelModel.h"
#include "VulkanModelManager.h"
#include "VulkanProgramManager.h"
#include <Gui/SDLVulkanDevice.h>
#include <Client/GameMap.h>
#include <Core/Bitmap.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <cstring>
#include <vector>

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
		  imageAvailableSemaphore(VK_NULL_HANDLE),
		  renderFinishedSemaphore(VK_NULL_HANDLE),
		  fogDistance(128.0f),
		  fogColor(MakeVector3(0, 0, 0)),
		  drawColorAlphaPremultiplied(MakeVector4(1, 1, 1, 1)),
		  legacyColorPremultiply(false),
		  lastTime(0),
		  frameNumber(0),
		  duringSceneRendering(false),
		  renderingMirror(false),
		  mapRenderer(nullptr),
		  modelRenderer(nullptr),
		  spriteRenderer(nullptr),
			imageRenderer(nullptr),
		waterRenderer(nullptr),
		flatMapRenderer(nullptr),
		framebufferManager(nullptr),
		programManager(nullptr),
		imageManager(nullptr),
		skyPipeline(VK_NULL_HANDLE),
		skyPipelineLayout(VK_NULL_HANDLE) {
		renderWidth = device->ScreenWidth();
		renderHeight = device->ScreenHeight();


		// Initialize program manager and model manager
		try {
			programManager = Handle<VulkanProgramManager>::New(device);
			modelManager = Handle<VulkanModelManager>::New(*this);
			InitializeVulkanResources();  // Create semaphores for synchronization

			// Create framebuffer manager for offscreen rendering
			framebufferManager = new VulkanFramebufferManager(device, renderWidth, renderHeight);

			CreateRenderPass();  // Must create render pass before framebuffers
			CreateDepthResources();  // Create depth buffer for 3D rendering
			CreateFramebuffers();
				CreateCommandBuffers();
			CreateSkyPipeline();

				mapRenderer = nullptr;
				modelRenderer = new VulkanModelRenderer(*this);
				spriteRenderer = new VulkanSpriteRenderer(*this);
				imageRenderer = new VulkanImageRenderer(*this);
			imageManager = new VulkanImageManager(*this, device);
			waterRenderer = new VulkanWaterRenderer(*this, map);

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
			VulkanWaterRenderer::PreloadShaders(*this);

			inited = true;
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

			delete imageRenderer;
			imageRenderer = nullptr;
			delete spriteRenderer;
			spriteRenderer = nullptr;
			delete modelRenderer;
			modelRenderer = nullptr;
			delete imageManager;
			imageManager = nullptr;
			delete mapRenderer;
			mapRenderer = nullptr;
			delete flatMapRenderer;
			flatMapRenderer = nullptr;
			delete waterRenderer;
			waterRenderer = nullptr;
			delete framebufferManager;
			framebufferManager = nullptr;
			programManager = nullptr;

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

			// Create semaphores for frame synchronization
			VkSemaphoreCreateInfo semaphoreInfo{};
			semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

			VkResult result = vkCreateSemaphore(vkDevice, &semaphoreInfo, nullptr, &imageAvailableSemaphore);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create image available semaphore (error code: %d)", result);
			}

			result = vkCreateSemaphore(vkDevice, &semaphoreInfo, nullptr, &renderFinishedSemaphore);
			if (result != VK_SUCCESS) {
				vkDestroySemaphore(vkDevice, imageAvailableSemaphore, nullptr);
				SPRaise("Failed to create render finished semaphore (error code: %d)", result);
			}

			// Create fences for frame synchronization (one per swapchain image)
			uint32_t imageCount = static_cast<uint32_t>(device->GetSwapchainImageViews().size());
			inFlightFences.resize(imageCount);

			VkFenceCreateInfo fenceInfo{};
			fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start signaled so first frame doesn't wait

			for (size_t i = 0; i < imageCount; i++) {
				result = vkCreateFence(vkDevice, &fenceInfo, nullptr, &inFlightFences[i]);
				if (result != VK_SUCCESS) {
					// Clean up previously created fences
					for (size_t j = 0; j < i; j++) {
						vkDestroyFence(vkDevice, inFlightFences[j], nullptr);
					}
					vkDestroySemaphore(vkDevice, renderFinishedSemaphore, nullptr);
					vkDestroySemaphore(vkDevice, imageAvailableSemaphore, nullptr);
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
			depthAttachment.format = VK_FORMAT_D32_SFLOAT;
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

		void VulkanRenderer::CreateDepthResources() {
			SPADES_MARK_FUNCTION();

			VkExtent2D swapchainExtent = device->GetSwapchainExtent();
			VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

			// Create depth image
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
			imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VkResult result = vkCreateImage(device->GetDevice(), &imageInfo, nullptr, &depthImage);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create depth image (error code: %d)", result);
			}

			// Allocate memory for depth image
			VkMemoryRequirements memRequirements;
			vkGetImageMemoryRequirements(device->GetDevice(), depthImage, &memRequirements);

			VkMemoryAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = memRequirements.size;
			allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
			                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

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

			if (renderPass != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroyRenderPass(vkDevice, renderPass, nullptr);
				renderPass = VK_NULL_HANDLE;
			}

			if (imageAvailableSemaphore != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroySemaphore(vkDevice, imageAvailableSemaphore, nullptr);
				imageAvailableSemaphore = VK_NULL_HANDLE;
			}

			if (renderFinishedSemaphore != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroySemaphore(vkDevice, renderFinishedSemaphore, nullptr);
				renderFinishedSemaphore = VK_NULL_HANDLE;
			}

			// Destroy fences
			for (VkFence fence : inFlightFences) {
				if (fence != VK_NULL_HANDLE) {
					vkDestroyFence(vkDevice, fence, nullptr);
				}
			}
			inFlightFences.clear();
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
				vkAllocateCommandBuffers(device->GetDevice(), &allocInfo, &commandBuffer);

				VkCommandBufferBeginInfo beginInfo{};
				beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
				vkBeginCommandBuffer(commandBuffer, &beginInfo);

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

				// Submit and wait
				VkSubmitInfo submitInfo{};
				submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				submitInfo.commandBufferCount = 1;
				submitInfo.pCommandBuffers = &commandBuffer;

				vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
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

			// Initialize map renderer
			if (map) {
				delete mapRenderer;
				mapRenderer = new VulkanMapRenderer(map, *this);
				mapRenderer->CreatePipelines(framebufferManager->GetRenderPass()); // Use offscreen render pass for 3D rendering
			} else {
				delete mapRenderer;
				mapRenderer = nullptr;
			}

			// Initialize flat map renderer (minimap)
			if (map) {
				delete flatMapRenderer;
				flatMapRenderer = new VulkanFlatMapRenderer(*this, *map);
			} else {
				delete flatMapRenderer;
				flatMapRenderer = nullptr;
			}

			// Notify water renderer about map change
			if (waterRenderer) {
				waterRenderer->GameMapChanged(0, 0, 0, map);
			}
		}

		void VulkanRenderer::SetFogDistance(float distance) {
			fogDistance = distance;
		}

		void VulkanRenderer::SetFogColor(Vector3 color) {
			fogColor = color;
		}

		Vector3 VulkanRenderer::GetFogColorForSolidPass() {
			// For now, always return the fog color since VulkanMapShadowRenderer is not implemented
			// When shadow maps are implemented, check r_fogShadow setting like OpenGL does
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

			// Acquire next swapchain image
			currentImageIndex = device->AcquireNextImage(&imageAvailableSemaphore);
			if (currentImageIndex == UINT32_MAX) {
				// Swapchain was recreated, try again
				currentImageIndex = device->AcquireNextImage(&imageAvailableSemaphore);
			}

			// Wait for the previous frame to finish (if any)
			// Note: Fences are waited on and reset in EndScene before submission
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

			// Approximate long sprite with two regular sprites
			Vector3 center = (p1 + p2) * 0.5f;
			Vector3 diff = p2 - p1;
			float length = diff.GetLength();
			float angle = atan2f(diff.y, diff.x);

			VulkanImage* vkImage = nullptr;
			VulkanImageWrapper* wrapper = dynamic_cast<VulkanImageWrapper*>(&img);
			if (wrapper) {
				vkImage = wrapper->GetVulkanImage();
			} else {
				vkImage = dynamic_cast<VulkanImage*>(&img);
			}

			if (vkImage && spriteRenderer) {
				spriteRenderer->Add(vkImage, center, std::max(radius, length * 0.5f), angle, drawColorAlphaPremultiplied);
			}
		}

		void VulkanRenderer::AddLight(const client::DynamicLightParam& light) {
			SPADES_MARK_FUNCTION();
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

			// Wait for the fence for this frame to ensure the GPU is done with the command buffer
			vkWaitForFences(device->GetDevice(), 1, &inFlightFences[currentImageIndex], VK_TRUE, UINT64_MAX);
			vkResetFences(device->GetDevice(), 1, &inFlightFences[currentImageIndex]);

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

			VkResult result = vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, inFlightFences[currentImageIndex]);
			if (result != VK_SUCCESS) {
				SPLog("Warning: Failed to submit draw command buffer (error code: %d)", result);
			}

			duringSceneRendering = false;
		}

		void VulkanRenderer::MultiplyScreenColor(Vector3 color) {
			SPADES_MARK_FUNCTION();
			EnsureSceneNotStarted();

			if (imageRenderer && whiteImage) {
				// Note: We don't flush here - pending 2D content will be rendered normally
				// This just adds a fullscreen quad to the batch for screen color multiplication

				// Draw a fullscreen quad with the white image and color multiplication
				imageRenderer->SetImage(whiteImage.GetPointerOrNull());
				float w = ScreenWidth();
				float h = ScreenHeight();
				imageRenderer->Add(0, 0, w, 0, w, h, 0, h,
				                   0, 0, 1, 0, 1, 1, 0, 1,
				                   color.x, color.y, color.z, 1.0f);
			}
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

			static int drawCallCount = 0;
			if (drawCallCount < 5) {
				SPLog("DrawImage #%d: image=%p, outTL=(%.1f,%.1f), outTR=(%.1f,%.1f), outBL=(%.1f,%.1f), inRect=(%.1f,%.1f,%.1f,%.1f)",
					drawCallCount, image.get_pointer(), outTopLeft.x, outTopLeft.y, outTopRight.x, outTopRight.y,
					outBottomLeft.x, outBottomLeft.y, inRect.GetMinX(), inRect.GetMinY(),
					inRect.GetMaxX(), inRect.GetMaxY());
				drawCallCount++;
			}

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

			static int addCallCount = 0;
			if (addCallCount < 3 && img != whiteImage.GetPointerOrNull()) {
				SPLog("DrawImage: Adding vertices for non-white image %p, color=(%.2f,%.2f,%.2f,%.2f)",
					img, col.x, col.y, col.z, col.w);
				addCallCount++;
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
					VkSemaphore dummySemaphore = VK_NULL_HANDLE;
					uint32_t imageIndex = device->AcquireNextImage(&dummySemaphore);
					if (imageIndex != UINT32_MAX) {
						device->PresentImage(imageIndex, nullptr, 0);
					}
				} catch (...) {
					// Silently ignore errors during uninitialized presentation
				}
				return;
			}


			if (sceneUsedInThisFrame) {
				// Present the image (already rendered in EndScene)
				VkSemaphore waitSemaphores[] = {renderFinishedSemaphore};
				device->PresentImage(currentImageIndex, waitSemaphores, 1);

				sceneUsedInThisFrame = false;
			} else {
				// 2D-only rendering (like loading screen) - need to record and submit command buffer

				// Acquire next swapchain image
				currentImageIndex = device->AcquireNextImage(&imageAvailableSemaphore);
				if (currentImageIndex == UINT32_MAX) {
					SPLog("[VulkanRenderer::Flip] Failed to acquire swapchain image");
					return;
				}

				// Wait for the fence for this frame to ensure the GPU is done with the command buffer
				vkWaitForFences(device->GetDevice(), 1, &inFlightFences[currentImageIndex], VK_TRUE, UINT64_MAX);
				vkResetFences(device->GetDevice(), 1, &inFlightFences[currentImageIndex]);

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

				VkResult result = vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, inFlightFences[currentImageIndex]);
				if (result != VK_SUCCESS) {
					SPLog("[VulkanRenderer::Flip] Failed to submit command buffer (error code: %d)", result);
				}

				// Present the image with proper synchronization
				device->PresentImage(currentImageIndex, signalSemaphores, 1);
			}
		}

		Handle<Bitmap> VulkanRenderer::ReadBitmap() {
			SPADES_MARK_FUNCTION();

			// For now, return an empty bitmap
			// Full implementation requires:
			// 1. Create a staging buffer
			// 2. Copy the swapchain image to the staging buffer
			// 3. Map the staging buffer and read the data
			// 4. Convert the data to Bitmap format
			// 5. Clean up the staging buffer
			return Handle<Bitmap>();
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
			if (flatMapRenderer)
				flatMapRenderer->GameMapChanged(x, y, z, *map);
			if (waterRenderer)
				waterRenderer->GameMapChanged(x, y, z, map);
		}

		void VulkanRenderer::ProcessDeferredDeletions() {
			SPADES_MARK_FUNCTION();

			// Process deferred deletions for buffers that are no longer in use by the GPU
			// We keep buffers alive for MAX_FRAMES_IN_FLIGHT frames to ensure the GPU is done with them
			const uint32_t MAX_FRAMES_IN_FLIGHT = static_cast<uint32_t>(inFlightFences.size());

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

		VkRenderPass VulkanRenderer::GetOffscreenRenderPass() const {
		return framebufferManager ? framebufferManager->GetRenderPass() : renderPass;
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

		// If we're rendering a 3D scene, render it to the offscreen framebuffer first
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
			// Depth prepass and shadow maps will be added when:
			// - VulkanShadowMapRenderer is implemented
			// - Additional render passes are configured
			// - Depth-only pipeline is created

			// Render sky gradient
			RenderSky(commandBuffer);

			// Render map
			if (mapRenderer) {
				mapRenderer->RenderSunlightPass(commandBuffer);
			}

			// Render models
			if (modelRenderer) {
				modelRenderer->RenderSunlightPass(commandBuffer, false);
			}

			// Render sprites
			if (spriteRenderer) {
				spriteRenderer->Render(commandBuffer, imageIndex);
			}

			// Clear for next frame
			if (spriteRenderer) {
				spriteRenderer->Clear();
			}
			if (modelRenderer) {
				modelRenderer->Clear();
			}
			debugLines.clear();
			lights.clear();

			// End offscreen render pass (scene without water is now complete)
			vkCmdEndRenderPass(commandBuffer);

			// Transition offscreen color and depth images to shader read-only for water sampling
			Handle<VulkanImage> offscreenColor = framebufferManager->GetColorImage();
			Handle<VulkanImage> offscreenDepth = framebufferManager->GetDepthImage();

			VkImageMemoryBarrier barriers[2]{};
			// Color image transition
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

			// Depth image transition
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

			// Copy scene to mirror images so water can sample from them
			if (waterRenderer && framebufferManager->GetMirrorColorImage()) {
				// Copy color and depth from render targets to mirror targets
				framebufferManager->CopyToMirrorImage(commandBuffer);

				// Transition renderColorImage and renderDepthImage back to COLOR_ATTACHMENT_OPTIMAL
				// for water rendering
				VkImageMemoryBarrier backToAttachment[2]{};
				backToAttachment[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				backToAttachment[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				backToAttachment[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				backToAttachment[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				backToAttachment[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				backToAttachment[0].image = offscreenColor->GetImage();
				backToAttachment[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				backToAttachment[0].subresourceRange.baseMipLevel = 0;
				backToAttachment[0].subresourceRange.levelCount = 1;
				backToAttachment[0].subresourceRange.baseArrayLayer = 0;
				backToAttachment[0].subresourceRange.layerCount = 1;
				backToAttachment[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				backToAttachment[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

				backToAttachment[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				backToAttachment[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				backToAttachment[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				backToAttachment[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				backToAttachment[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				backToAttachment[1].image = offscreenDepth->GetImage();
				backToAttachment[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				backToAttachment[1].subresourceRange.baseMipLevel = 0;
				backToAttachment[1].subresourceRange.levelCount = 1;
				backToAttachment[1].subresourceRange.baseArrayLayer = 0;
				backToAttachment[1].subresourceRange.layerCount = 1;
				backToAttachment[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				backToAttachment[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

				vkCmdPipelineBarrier(commandBuffer,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
					0, 0, nullptr, 0, nullptr, 2, backToAttachment);

				// Begin water render pass with LOAD_OP to preserve scene content
				VkRenderPassBeginInfo waterRenderPassInfo{};
				waterRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				waterRenderPassInfo.renderPass = framebufferManager->GetWaterRenderPass();
				waterRenderPassInfo.framebuffer = framebufferManager->GetRenderFramebuffer();
				waterRenderPassInfo.renderArea.offset = {0, 0};
				waterRenderPassInfo.renderArea.extent = {static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight)};
				waterRenderPassInfo.clearValueCount = 0; // No clear, using LOAD_OP
				waterRenderPassInfo.pClearValues = nullptr;

				vkCmdBeginRenderPass(commandBuffer, &waterRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
				waterRenderer->RenderSunlightPass(commandBuffer);
				vkCmdEndRenderPass(commandBuffer);

				// Transition back to SHADER_READ_ONLY for next steps
				barriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

				vkCmdPipelineBarrier(commandBuffer,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &barriers[0]);
			}

			// Transition offscreen color image for transfer source
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
			barrier1.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier1.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
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

			// Blit offscreen render target to swapchain
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

			// Transition offscreen color back to shader read-only for next frame
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

			std::vector<uint32_t> vertCode = LoadSPIRVFile("Shaders/Sky.vert.spv");
			std::vector<uint32_t> fragCode = LoadSPIRVFile("Shaders/Sky.frag.spv");

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
			pushConstantRange.size = sizeof(float) * 16;  // fogColor + 3 view axes + 2 fov

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

			result = vkCreateGraphicsPipelines(vkDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &skyPipeline);

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

			Vector3 fogCol = GetFogColorForSolidPass();
			// Don't linearize - the SRGB framebuffer handles gamma correction automatically

			pushConstants.fogColor[0] = fogCol.x;
			pushConstants.fogColor[1] = fogCol.y;
			pushConstants.fogColor[2] = fogCol.z;

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

	} // namespace draw
} // namespace spades
