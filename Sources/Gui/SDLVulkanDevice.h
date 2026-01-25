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

#include <OpenSpades.h>

#if USE_VULKAN

#include <vector>
#include <vulkan/vulkan.h>
#include <Imports/SDL.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace gui {

		class SDLVulkanDevice : public RefCountedObject {
			SDL_Window* window;
			int w, h;

			// Vulkan core objects
			VkInstance instance;
			VkSurfaceKHR surface;
			VkPhysicalDevice physicalDevice;
			VkDevice device;

			// Queue families and queues
			uint32_t graphicsQueueFamily;
			uint32_t presentQueueFamily;
			VkQueue graphicsQueue;
			VkQueue presentQueue;

			// Swapchain
			VkSwapchainKHR swapchain;
			VkFormat swapchainImageFormat;
			VkExtent2D swapchainExtent;
			std::vector<VkImage> swapchainImages;
			std::vector<VkImageView> swapchainImageViews;

			// Command pools and buffers
			VkCommandPool commandPool;

			// Synchronization primitives
			std::vector<VkSemaphore> imageAvailableSemaphores;
			std::vector<VkSemaphore> renderFinishedSemaphores;
			std::vector<VkFence> inFlightFences;
			std::vector<VkFence> imagesInFlight;
			uint32_t currentFrame;

			// Debug messenger (only in debug mode)
#ifndef NDEBUG
			VkDebugUtilsMessengerEXT debugMessenger;
#endif

			// Helper methods
			void CreateInstance();
			void SetupDebugMessenger();
			void CreateSurface();
			void PickPhysicalDevice();
			void CreateLogicalDevice();
			void CreateSwapchain();
			void CreateImageViews();
			void CreateCommandPool();
			void CreateSyncObjects();

			// Cleanup methods
			void CleanupSwapchain();

		protected:
			~SDLVulkanDevice();

		public:
			SDLVulkanDevice(SDL_Window* window);

			// Getters for Vulkan objects
			VkInstance GetInstance() const { return instance; }
			VkDevice GetDevice() const { return device; }
			VkPhysicalDevice GetPhysicalDevice() const { return physicalDevice; }
			VkQueue GetGraphicsQueue() const { return graphicsQueue; }
			VkQueue GetPresentQueue() const { return presentQueue; }
			VkSwapchainKHR GetSwapchain() const { return swapchain; }
			VkFormat GetSwapchainImageFormat() const { return swapchainImageFormat; }
			VkExtent2D GetSwapchainExtent() const { return swapchainExtent; }
			const std::vector<VkImageView>& GetSwapchainImageViews() const { return swapchainImageViews; }
			VkImage GetSwapchainImage(uint32_t index) const { return swapchainImages[index]; }
			VkCommandPool GetCommandPool() const { return commandPool; }
			uint32_t GetGraphicsQueueFamily() const { return graphicsQueueFamily; }

			// Frame management
			uint32_t AcquireNextImage(VkSemaphore* outImageAvailableSemaphore, VkSemaphore* outRenderFinishedSemaphore);
			void PresentImage(uint32_t imageIndex, VkSemaphore* waitSemaphores, uint32_t waitSemaphoreCount);
			void WaitForFences();

			// Screen dimensions
			int ScreenWidth() const { return w; }
			int ScreenHeight() const { return h; }

			// Swapchain recreation (for window resize)
			void RecreateSwapchain();
		};

	} // namespace gui
} // namespace spades

#endif // USE_VULKAN
