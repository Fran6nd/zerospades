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

#include <OpenSpades.h>

#if USE_VULKAN

#include "SDLVulkanDevice.h"
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <algorithm>
#include <set>
#include <cstring>
#include <vector>
#include <SDL2/SDL_vulkan.h>

namespace spades {
	namespace gui {

		static const int MAX_FRAMES_IN_FLIGHT = 2;

#ifndef NDEBUG
		static const bool enableValidationLayers = true;
#else
		static const bool enableValidationLayers = false;
#endif

		static const std::vector<const char*> validationLayers = {
			"VK_LAYER_KHRONOS_validation"
		};

		static const std::vector<const char*> deviceExtensions = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME
		};

		// Debug callback for validation layers
		static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			VkDebugUtilsMessageTypeFlagsEXT messageType,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
			void* pUserData) {

			if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
				SPLog("[Vulkan] %s", pCallbackData->pMessage);
			}

			return VK_FALSE;
		}

		SDLVulkanDevice::SDLVulkanDevice(SDL_Window* wnd)
		: window(wnd),
		  instance(VK_NULL_HANDLE),
		  surface(VK_NULL_HANDLE),
		  physicalDevice(VK_NULL_HANDLE),
		  device(VK_NULL_HANDLE),
		  graphicsQueue(VK_NULL_HANDLE),
		  presentQueue(VK_NULL_HANDLE),
		  swapchain(VK_NULL_HANDLE),
		  commandPool(VK_NULL_HANDLE),
		  currentFrame(0),
		  allocator(VK_NULL_HANDLE)
#ifndef NDEBUG
		  , debugMessenger(VK_NULL_HANDLE)
#endif  // NDEBUG
		{
			SPADES_MARK_FUNCTION();

			SDL_GetWindowSize(window, &w, &h);
			SPLog("Initializing Vulkan device (window size: %dx%d)", w, h);

			try {
				CreateInstance();
				SetupDebugMessenger();
				CreateSurface();
				PickPhysicalDevice();
				CreateLogicalDevice();
				CreateAllocator();
				CreateSwapchain();
				CreateImageViews();
				CreateCommandPool();
				CreateSyncObjects();

				SPLog("Vulkan device initialized successfully");
			} catch (...) {
				// Cleanup on failure
				if (device != VK_NULL_HANDLE) {
					vkDeviceWaitIdle(device);
				}
				throw;
			}
		}

		SDLVulkanDevice::~SDLVulkanDevice() {
			SPADES_MARK_FUNCTION();

			if (device != VK_NULL_HANDLE) {
				vkDeviceWaitIdle(device);

				// Cleanup sync objects
				for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
					if (renderFinishedSemaphores[i] != VK_NULL_HANDLE)
						vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
					if (imageAvailableSemaphores[i] != VK_NULL_HANDLE)
						vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
					if (inFlightFences[i] != VK_NULL_HANDLE)
						vkDestroyFence(device, inFlightFences[i], nullptr);
				}

				if (commandPool != VK_NULL_HANDLE)
					vkDestroyCommandPool(device, commandPool, nullptr);

				CleanupSwapchain();

				if (allocator != VK_NULL_HANDLE) {
					vmaDestroyAllocator(allocator);
					allocator = VK_NULL_HANDLE;
				}

				vkDestroyDevice(device, nullptr);
			}

			if (surface != VK_NULL_HANDLE)
				vkDestroySurfaceKHR(instance, surface, nullptr);

#ifndef NDEBUG
			if (debugMessenger != VK_NULL_HANDLE) {
				auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
					vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
				if (func != nullptr) {
					func(instance, debugMessenger, nullptr);
				}
			}
#endif

			if (instance != VK_NULL_HANDLE)
				vkDestroyInstance(instance, nullptr);

			SPLog("Vulkan device destroyed");
		}

		void SDLVulkanDevice::CreateInstance() {
			SPADES_MARK_FUNCTION();

			VkApplicationInfo appInfo{};
			appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
			appInfo.pApplicationName = "OpenSpades";
			appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 5);
			appInfo.pEngineName = "OpenSpades";
			appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 5);
			appInfo.apiVersion = VK_API_VERSION_1_0;

			// Get required SDL extensions
			unsigned int sdlExtensionCount = 0;
			if (!SDL_Vulkan_GetInstanceExtensions(window, &sdlExtensionCount, nullptr)) {
				SPRaise("Failed to get SDL Vulkan extensions count: %s", SDL_GetError());
			}

			std::vector<const char*> extensions(sdlExtensionCount);
			if (!SDL_Vulkan_GetInstanceExtensions(window, &sdlExtensionCount, extensions.data())) {
				SPRaise("Failed to get SDL Vulkan extensions: %s", SDL_GetError());
			}

			// Add debug extension and check validation layer availability if validation layers are enabled
			bool useValidationLayers = enableValidationLayers;
			if (useValidationLayers) {
				// Check available instance layers
				uint32_t availableLayerCount = 0;
				vkEnumerateInstanceLayerProperties(&availableLayerCount, nullptr);
				std::vector<VkLayerProperties> availableLayers(availableLayerCount);
				if (availableLayerCount > 0) {
					vkEnumerateInstanceLayerProperties(&availableLayerCount, availableLayers.data());
				}

				for (const char* layerName : validationLayers) {
					bool found = false;
					for (const auto& layerProps : availableLayers) {
						if (strcmp(layerName, layerProps.layerName) == 0) {
							found = true;
							break;
						}
					}
					if (!found) {
						SPLog("Warning: Requested validation layer '%s' not available; disabling validation layers", layerName);
						useValidationLayers = false;
						break;
					}
				}

				if (useValidationLayers) {
					extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
				}
			}

			VkInstanceCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
			createInfo.pApplicationInfo = &appInfo;
			createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
			createInfo.ppEnabledExtensionNames = extensions.data();

			// Enable validation layers in debug mode (if available)
			VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
			if (useValidationLayers) {
				createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
				createInfo.ppEnabledLayerNames = validationLayers.data();

				// Setup debug messenger creation info for instance creation/destruction
				debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
				debugCreateInfo.messageSeverity =
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
				debugCreateInfo.messageType =
					VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
				debugCreateInfo.pfnUserCallback = debugCallback;

				createInfo.pNext = &debugCreateInfo;
			} else {
				createInfo.enabledLayerCount = 0;
				createInfo.pNext = nullptr;
			}

			// On macOS with MoltenVK, enable portability enumeration if available.
			// When linking directly to MoltenVK the extension may not be present,
			// but MoltenVK still exposes its device without it.
#ifdef __APPLE__
			{
				uint32_t availableExtCount = 0;
				vkEnumerateInstanceExtensionProperties(nullptr, &availableExtCount, nullptr);
				std::vector<VkExtensionProperties> availableExts(availableExtCount);
				if (availableExtCount > 0) {
					vkEnumerateInstanceExtensionProperties(nullptr, &availableExtCount, availableExts.data());
				}

				bool hasPortabilityEnum = false;
				bool hasPhysDevProps2 = false;
				for (const auto& ext : availableExts) {
					if (strcmp(ext.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0)
						hasPortabilityEnum = true;
					if (strcmp(ext.extensionName, "VK_KHR_get_physical_device_properties2") == 0)
						hasPhysDevProps2 = true;
				}

				if (hasPortabilityEnum) {
					createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
					extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
				}
				if (hasPhysDevProps2) {
					extensions.push_back("VK_KHR_get_physical_device_properties2");
				}
				createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
				createInfo.ppEnabledExtensionNames = extensions.data();
			}
#endif

			VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create Vulkan instance (error code: %d)", result);
			}

			SPLog("Vulkan instance created");
		}

		void SDLVulkanDevice::SetupDebugMessenger() {
#ifndef NDEBUG
			if (!enableValidationLayers) return;

			VkDebugUtilsMessengerCreateInfoEXT createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			createInfo.messageSeverity =
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			createInfo.messageType =
				VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			createInfo.pfnUserCallback = debugCallback;

			auto func = (PFN_vkCreateDebugUtilsMessengerEXT)
				vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

			if (func != nullptr) {
				VkResult result = func(instance, &createInfo, nullptr, &debugMessenger);
				if (result != VK_SUCCESS) {
					SPLog("Warning: Failed to set up debug messenger (error code: %d)", result);
				} else {
					SPLog("Vulkan debug messenger created");
				}
			}
#endif
		}

		void SDLVulkanDevice::CreateSurface() {
			if (!SDL_Vulkan_CreateSurface(window, instance, &surface)) {
				SPRaise("Failed to create Vulkan surface: %s", SDL_GetError());
			}
			SPLog("Vulkan surface created");
		}

		void SDLVulkanDevice::PickPhysicalDevice() {
			uint32_t deviceCount = 0;
			vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

			if (deviceCount == 0) {
				SPRaise("Failed to find GPUs with Vulkan support");
			}

			std::vector<VkPhysicalDevice> devices(deviceCount);
			vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

			// For now, just pick the first suitable device
			// TODO: Implement device scoring to pick the best GPU
			for (const auto& dev : devices) {
				VkPhysicalDeviceProperties properties;
				vkGetPhysicalDeviceProperties(dev, &properties);

				// Check if device supports required queue families
				uint32_t queueFamilyCount = 0;
				vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, nullptr);

				std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
				vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, queueFamilies.data());

				bool foundGraphics = false;
				bool foundPresent = false;

				for (uint32_t i = 0; i < queueFamilies.size(); i++) {
					if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
						graphicsQueueFamily = i;
						foundGraphics = true;
					}

					VkBool32 presentSupport = false;
					vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &presentSupport);
					if (presentSupport) {
						presentQueueFamily = i;
						foundPresent = true;
					}

					if (foundGraphics && foundPresent) break;
				}

				// Check device extension support
				uint32_t extensionCount;
				vkEnumerateDeviceExtensionProperties(dev, nullptr, &extensionCount, nullptr);

				std::vector<VkExtensionProperties> availableExtensions(extensionCount);
				vkEnumerateDeviceExtensionProperties(dev, nullptr, &extensionCount,
					availableExtensions.data());

				std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());
				for (const auto& extension : availableExtensions) {
					requiredExtensions.erase(extension.extensionName);
				}

				if (foundGraphics && foundPresent && requiredExtensions.empty()) {
					physicalDevice = dev;
					SPLog("Selected GPU: %s", properties.deviceName);
					break;
				}
			}

			if (physicalDevice == VK_NULL_HANDLE) {
				SPRaise("Failed to find a suitable GPU");
			}
		}

		void SDLVulkanDevice::CreateLogicalDevice() {
			std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
			std::set<uint32_t> uniqueQueueFamilies = {graphicsQueueFamily, presentQueueFamily};

			float queuePriority = 1.0f;
			for (uint32_t queueFamily : uniqueQueueFamilies) {
				VkDeviceQueueCreateInfo queueCreateInfo{};
				queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueCreateInfo.queueFamilyIndex = queueFamily;
				queueCreateInfo.queueCount = 1;
				queueCreateInfo.pQueuePriorities = &queuePriority;
				queueCreateInfos.push_back(queueCreateInfo);
			}

			// Query supported features before enabling them
			VkPhysicalDeviceFeatures supportedFeatures{};
			vkGetPhysicalDeviceFeatures(physicalDevice, &supportedFeatures);

			VkPhysicalDeviceFeatures deviceFeatures{};
			// Only enable features that are actually supported
			if (supportedFeatures.samplerAnisotropy) {
				deviceFeatures.samplerAnisotropy = VK_TRUE;
			} else {
				SPLog("Warning: Anisotropic filtering not supported on this device");
			}
			if (supportedFeatures.sampleRateShading) {
				deviceFeatures.sampleRateShading = VK_TRUE;
			} else {
				SPLog("Warning: Sample rate shading not supported on this device");
			}
			if (supportedFeatures.fillModeNonSolid) {
				deviceFeatures.fillModeNonSolid = VK_TRUE;
			} else {
				SPLog("Warning: fillModeNonSolid not supported - outlines will be disabled");
			}

			VkDeviceCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
			createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
			createInfo.pQueueCreateInfos = queueCreateInfos.data();
			createInfo.pEnabledFeatures = &deviceFeatures;

			// Device extensions (swapchain is required)
			std::vector<const char*> extensions = deviceExtensions;
#ifdef __APPLE__
			// MoltenVK requires portability subset extension
			extensions.push_back("VK_KHR_portability_subset");
#endif

			createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
			createInfo.ppEnabledExtensionNames = extensions.data();

			if (enableValidationLayers) {
				createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
				createInfo.ppEnabledLayerNames = validationLayers.data();
			} else {
				createInfo.enabledLayerCount = 0;
			}

			VkResult result = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create logical device (error code: %d)", result);
			}

			vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
			vkGetDeviceQueue(device, presentQueueFamily, 0, &presentQueue);

			SPLog("Vulkan logical device created");
		}

		void SDLVulkanDevice::CreateAllocator() {
			SPADES_MARK_FUNCTION();

			VmaVulkanFunctions vkFuncs = {};
			vkFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
			vkFuncs.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;

			// Check for optional extensions that VMA can exploit for better allocation.
			uint32_t devExtCount = 0;
			vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &devExtCount, nullptr);
			std::vector<VkExtensionProperties> devExts(devExtCount);
			if (devExtCount > 0) {
				vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &devExtCount, devExts.data());
			}
			bool hasDedicatedAlloc = false, hasBindMemory2 = false;
			for (const auto& ext : devExts) {
				if (strcmp(ext.extensionName, VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME) == 0)
					hasDedicatedAlloc = true;
				if (strcmp(ext.extensionName, VK_KHR_BIND_MEMORY_2_EXTENSION_NAME) == 0)
					hasBindMemory2 = true;
			}

			VmaAllocatorCreateFlags allocatorFlags = 0;
			if (hasDedicatedAlloc) allocatorFlags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
			if (hasBindMemory2)    allocatorFlags |= VMA_ALLOCATOR_CREATE_KHR_BIND_MEMORY2_BIT;

			VmaAllocatorCreateInfo allocatorInfo = {};
			allocatorInfo.flags            = allocatorFlags;
			allocatorInfo.physicalDevice   = physicalDevice;
			allocatorInfo.device           = device;
			allocatorInfo.instance         = instance;
			allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_0;
			allocatorInfo.pVulkanFunctions = &vkFuncs;

			VkResult result = vmaCreateAllocator(&allocatorInfo, &allocator);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create VMA allocator (error code: %d)", result);
			}
			SPLog("VMA allocator created (flags: 0x%x)", allocatorFlags);
		}

		void SDLVulkanDevice::CreateSwapchain() {
			// Query swapchain support
			VkSurfaceCapabilitiesKHR capabilities;
			vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

			uint32_t formatCount;
			vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
			std::vector<VkSurfaceFormatKHR> formats(formatCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

			uint32_t presentModeCount;
			vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
			std::vector<VkPresentModeKHR> presentModes(presentModeCount);
			vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount,
				presentModes.data());

			// Choose surface format (prefer SRGB if available)
			VkSurfaceFormatKHR surfaceFormat = formats[0];
			for (const auto& availableFormat : formats) {
				if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
					availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
					surfaceFormat = availableFormat;
					break;
				}
			}

			// Choose present mode (prefer MAILBOX for lower latency, fall back to FIFO)
			VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
			for (const auto& availablePresentMode : presentModes) {
				if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
					presentMode = availablePresentMode;
					break;
				}
			}

			// Choose swap extent
			if (capabilities.currentExtent.width != UINT32_MAX) {
				swapchainExtent = capabilities.currentExtent;
			} else {
				swapchainExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
				swapchainExtent.width = std::max(capabilities.minImageExtent.width,
					std::min(capabilities.maxImageExtent.width, swapchainExtent.width));
				swapchainExtent.height = std::max(capabilities.minImageExtent.height,
					std::min(capabilities.maxImageExtent.height, swapchainExtent.height));
			}

			// Request one more image than the minimum to avoid waiting
			uint32_t imageCount = capabilities.minImageCount + 1;
			if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
				imageCount = capabilities.maxImageCount;
			}

			VkSwapchainCreateInfoKHR createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
			createInfo.surface = surface;
			createInfo.minImageCount = imageCount;
			createInfo.imageFormat = surfaceFormat.format;
			createInfo.imageColorSpace = surfaceFormat.colorSpace;
			createInfo.imageExtent = swapchainExtent;
			createInfo.imageArrayLayers = 1;
			createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

			uint32_t queueFamilyIndices[] = {graphicsQueueFamily, presentQueueFamily};
			if (graphicsQueueFamily != presentQueueFamily) {
				createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
				createInfo.queueFamilyIndexCount = 2;
				createInfo.pQueueFamilyIndices = queueFamilyIndices;
			} else {
				createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			}

			createInfo.preTransform = capabilities.currentTransform;
			createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
			createInfo.presentMode = presentMode;
			createInfo.clipped = VK_TRUE;
			createInfo.oldSwapchain = VK_NULL_HANDLE;

			VkResult result = vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create swapchain (error code: %d)", result);
			}

			// Retrieve swapchain images
			vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
			swapchainImages.resize(imageCount);
			vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());

			swapchainImageFormat = surfaceFormat.format;

			SPLog("Vulkan swapchain created (%ux%u, %u images)",
				swapchainExtent.width, swapchainExtent.height, imageCount);
		}

		void SDLVulkanDevice::CreateImageViews() {
			swapchainImageViews.resize(swapchainImages.size());

			for (size_t i = 0; i < swapchainImages.size(); i++) {
				VkImageViewCreateInfo createInfo{};
				createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				createInfo.image = swapchainImages[i];
				createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
				createInfo.format = swapchainImageFormat;
				createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
				createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
				createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
				createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
				createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				createInfo.subresourceRange.baseMipLevel = 0;
				createInfo.subresourceRange.levelCount = 1;
				createInfo.subresourceRange.baseArrayLayer = 0;
				createInfo.subresourceRange.layerCount = 1;

				VkResult result = vkCreateImageView(device, &createInfo, nullptr, &swapchainImageViews[i]);
				if (result != VK_SUCCESS) {
					SPRaise("Failed to create image view (error code: %d)", result);
				}
			}

			SPLog("Created %zu swapchain image views", swapchainImageViews.size());
		}

		void SDLVulkanDevice::CreateCommandPool() {
			VkCommandPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			poolInfo.queueFamilyIndex = graphicsQueueFamily;
			poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

			VkResult result = vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create command pool (error code: %d)", result);
			}

			SPLog("Vulkan command pool created");
		}

		void SDLVulkanDevice::CreateSyncObjects() {
			imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
			renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
			inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
			imagesInFlight.resize(swapchainImages.size(), VK_NULL_HANDLE);

			VkSemaphoreCreateInfo semaphoreInfo{};
			semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

			VkFenceCreateInfo fenceInfo{};
			fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

			for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
				if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
					vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
					vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
					SPRaise("Failed to create synchronization objects");
				}
			}

			SPLog("Vulkan synchronization objects created");
		}

		void SDLVulkanDevice::CleanupSwapchain() {
			for (auto imageView : swapchainImageViews) {
				vkDestroyImageView(device, imageView, nullptr);
			}
			swapchainImageViews.clear();

			if (swapchain != VK_NULL_HANDLE) {
				vkDestroySwapchainKHR(device, swapchain, nullptr);
				swapchain = VK_NULL_HANDLE;
			}
		}

		uint32_t SDLVulkanDevice::AcquireNextImage(VkSemaphore* outImageAvailableSemaphore, VkSemaphore* outRenderFinishedSemaphore) {
			// Note: Frame synchronization is handled by VulkanRenderer's fences in EndScene.
			// Removed redundant fence wait that was causing frame timing issues.

			uint32_t imageIndex;
			VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
				imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

			if (result == VK_ERROR_OUT_OF_DATE_KHR) {
				RecreateSwapchain();
				return UINT32_MAX; // Signal caller to retry
			} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
				SPRaise("Failed to acquire swapchain image");
			}

			*outImageAvailableSemaphore = imageAvailableSemaphores[currentFrame];
			*outRenderFinishedSemaphore = renderFinishedSemaphores[currentFrame];
			return imageIndex;
		}

		void SDLVulkanDevice::PresentImage(uint32_t imageIndex, VkSemaphore* waitSemaphores,
										   uint32_t waitSemaphoreCount) {
			VkPresentInfoKHR presentInfo{};
			presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
			presentInfo.waitSemaphoreCount = waitSemaphoreCount;
			presentInfo.pWaitSemaphores = waitSemaphores;

			VkSwapchainKHR swapchains[] = {swapchain};
			presentInfo.swapchainCount = 1;
			presentInfo.pSwapchains = swapchains;
			presentInfo.pImageIndices = &imageIndex;

			VkResult result = vkQueuePresentKHR(presentQueue, &presentInfo);

			if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
				RecreateSwapchain();
			} else if (result != VK_SUCCESS) {
				SPRaise("Failed to present swapchain image");
			}

			currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
		}

		void SDLVulkanDevice::WaitForFences() {
			vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
			vkResetFences(device, 1, &inFlightFences[currentFrame]);
		}

		void SDLVulkanDevice::RecreateSwapchain() {
			int width = 0, height = 0;
			SDL_GetWindowSize(window, &width, &height);
			while (width == 0 || height == 0) {
				SDL_GetWindowSize(window, &width, &height);
				SDL_Delay(10);
			}

			vkDeviceWaitIdle(device);

			CleanupSwapchain();

			w = width;
			h = height;
			CreateSwapchain();
			CreateImageViews();

			SPLog("Swapchain recreated (%dx%d)", w, h);
		}

	} // namespace gui
} // namespace spades

#endif // USE_VULKAN
