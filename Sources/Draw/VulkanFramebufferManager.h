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

#include <memory>
#include <vector>
#include <vulkan/vulkan.h>
#include <Core/RefCountedObject.h>
#include <Core/Math.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanImage;

		class VulkanFramebufferManager {
		public:
			class BufferHandle {
				VulkanFramebufferManager *manager;
				size_t bufferIndex;
				bool valid;

			public:
				BufferHandle();
				BufferHandle(VulkanFramebufferManager *manager, size_t index);
				BufferHandle(const BufferHandle &);
				~BufferHandle();

				void operator=(const BufferHandle &);

				bool IsValid() const { return valid; }
				void Release();
				VkFramebuffer GetFramebuffer();
				Handle<VulkanImage> GetColorImage();
				Handle<VulkanImage> GetDepthImage();
				int GetWidth();
				int GetHeight();
				VkFormat GetColorFormat();

				VulkanFramebufferManager *GetManager() { return manager; }
			};

		private:
			Handle<gui::SDLVulkanDevice> device;

			struct Buffer {
				VkFramebuffer framebuffer;
				Handle<VulkanImage> colorImage;
				Handle<VulkanImage> depthImage;
				int refCount;
				int w, h;
				VkFormat colorFormat;
			};

			bool useHighPrec;
			bool useHdr;
			bool useSRGB;

			bool doingPostProcessing;

			int renderWidth;
			int renderHeight;

			VkFormat fbColorFormat;
			VkFormat fbDepthFormat;

			// Main render framebuffer
			VkFramebuffer renderFramebuffer;
			Handle<VulkanImage> renderColorImage;
			Handle<VulkanImage> renderDepthImage;

			// Mirror framebuffer for water reflections
			VkFramebuffer mirrorFramebuffer;
			Handle<VulkanImage> mirrorColorImage;
			Handle<VulkanImage> mirrorDepthImage;

			// Render pass used for all framebuffers
			VkRenderPass renderPass;
			VkRenderPass waterRenderPass; // Render pass with LOAD_OP for water rendering

			std::vector<Buffer> buffers;

			void CreateRenderPass();

		public:
			VulkanFramebufferManager(Handle<gui::SDLVulkanDevice> device, int renderWidth, int renderHeight);
			~VulkanFramebufferManager();

			/** Sets up for scene rendering. */
			void PrepareSceneRendering(VkCommandBuffer commandBuffer);

			BufferHandle PrepareForWaterRendering(VkCommandBuffer commandBuffer);
			BufferHandle StartPostProcessing();

			void MakeSureAllBuffersReleased();

			Handle<VulkanImage> GetDepthImage() { return renderDepthImage; }
			Handle<VulkanImage> GetColorImage() { return renderColorImage; }
			VkFormat GetMainColorFormat() { return fbColorFormat; }
			VkRenderPass GetRenderPass() { return renderPass; }
			VkRenderPass GetWaterRenderPass() { return waterRenderPass; }
			VkFramebuffer GetRenderFramebuffer() { return renderFramebuffer; }

			/**
			 * Creates BufferHandle with a given size and format.
			 */
			BufferHandle CreateBufferHandle(int w = -1, int h = -1, bool alpha = false);

			/**
			 * Creates BufferHandle with a given size and format.
			 */
			BufferHandle CreateBufferHandle(int w, int h, VkFormat colorFormat);

			void CopyToMirrorImage(VkCommandBuffer commandBuffer, VkFramebuffer srcFb = VK_NULL_HANDLE);
			void ClearMirrorImage(VkCommandBuffer commandBuffer, Vector3 bgCol);
			Handle<VulkanImage> GetMirrorColorImage() { return mirrorColorImage; }
			Handle<VulkanImage> GetMirrorDepthImage() { return mirrorDepthImage; }
		};

		// Shorter name
		typedef VulkanFramebufferManager::BufferHandle VulkanColorBuffer;
	} // namespace draw
} // namespace spades
