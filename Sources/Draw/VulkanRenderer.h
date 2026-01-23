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
#include <string>
#include <vulkan/vulkan.h>

#include <Client/IGameMapListener.h>
#include <Client/IRenderer.h>
#include <Client/SceneDefinition.h>
#include <Core/Math.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanMapRenderer;
		class VulkanModelRenderer;
		class VulkanSpriteRenderer;
		class VulkanImageRenderer;
		class VulkanWaterRenderer;
		class VulkanFlatMapRenderer;
		class VulkanShadowMapRenderer;
		class VulkanFramebufferManager;
		class VulkanProgramManager;
		class VulkanModelManager;
		class VulkanImageManager;
		class VulkanPipelineCache;
		class VulkanProgram;
		class VulkanShader;
		class VulkanImage;
		class VulkanTemporaryImagePool;

		class VulkanBuffer;

		class VulkanRenderer : public client::IRenderer, public client::IGameMapListener {
			struct DebugLine {
				Vector3 v1, v2;
				Vector4 color;
			};

			// Deferred deletion queue entry
			struct DeferredDeletion {
				Handle<VulkanBuffer> buffer;
				uint32_t frameIndex; // Frame when this buffer was marked for deletion
			};

			Handle<gui::SDLVulkanDevice> device;
			client::GameMap* map;
			bool inited;
			bool sceneUsedInThisFrame;

			client::SceneDefinition sceneDef;

			std::vector<DebugLine> debugLines;
			std::vector<client::DynamicLightParam> lights;

			// Vulkan rendering resources
			std::vector<VkCommandBuffer> commandBuffers;
			VkRenderPass renderPass;
			std::vector<VkFramebuffer> swapchainFramebuffers;

			// Depth buffer
			VkImage depthImage;
			VkDeviceMemory depthImageMemory;
			VkImageView depthImageView;
			Handle<VulkanImage> depthImageWrapper; // Wrapper for water renderer access

			// Frame synchronization
			uint32_t currentImageIndex;
			VkSemaphore imageAvailableSemaphore;
			VkSemaphore renderFinishedSemaphore;
			std::vector<VkFence> inFlightFences;

			// Deferred deletion queue for buffers that may still be in use by GPU
			std::vector<DeferredDeletion> deferredDeletions;

			float fogDistance;
			Vector3 fogColor;

			Matrix4 projectionMatrix;
			Matrix4 viewMatrix;
			Matrix4 projectionViewMatrix;

			Vector4 drawColorAlphaPremultiplied;
			bool legacyColorPremultiply;

			unsigned int lastTime;
			std::uint32_t frameNumber = 0;

			bool duringSceneRendering;
			bool renderingMirror;

			VulkanMapRenderer* mapRenderer;
			VulkanModelRenderer* modelRenderer;
			VulkanSpriteRenderer* spriteRenderer;
			VulkanImageRenderer* imageRenderer;
			VulkanWaterRenderer* waterRenderer;
			VulkanFlatMapRenderer* flatMapRenderer;
			VulkanShadowMapRenderer* shadowMapRenderer;
			VulkanFramebufferManager* framebufferManager;
			Handle<VulkanProgramManager> programManager;
			Handle<VulkanModelManager> modelManager;
			VulkanImageManager* imageManager;
			Handle<VulkanPipelineCache> pipelineCache;
			Handle<VulkanTemporaryImagePool> temporaryImagePool;

			Handle<VulkanImage> whiteImage; // 1x1 white image for solid color rendering

			// Sky gradient rendering
			VkPipeline skyPipeline;
			VkPipelineLayout skyPipelineLayout;
			Handle<VulkanBuffer> skyVertexBuffer;
			Handle<VulkanBuffer> skyIndexBuffer;

			int renderWidth;
			int renderHeight;

			void BuildProjectionMatrix();
			void BuildView();

			void EnsureInitialized();
			void EnsureSceneStarted();
			void EnsureSceneNotStarted();

			void InitializeVulkanResources();
			void CreateRenderPass();
			void CreateDepthResources();
			void CreateFramebuffers();
			void CreateCommandBuffers();
			void CleanupVulkanResources();

			uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

			void RecordCommandBuffer(uint32_t imageIndex);
			void Record2DCommandBuffer(uint32_t imageIndex);

			// Sky gradient rendering
			void CreateSkyPipeline();
			void DestroySkyPipeline();
			void RenderSky(VkCommandBuffer commandBuffer);

			// Deferred deletion queue management
			void ProcessDeferredDeletions();

		protected:
			~VulkanRenderer();

		public:
			VulkanRenderer(Handle<gui::SDLVulkanDevice> device);

			// IRenderer implementation
			void Init() override;
			void Shutdown() override;

			Handle<client::IImage> RegisterImage(const char* filename) override;
			Handle<client::IModel> RegisterModel(const char* filename) override;

			Handle<client::IImage> CreateImage(Bitmap& bitmap) override;
			Handle<client::IModel> CreateModel(VoxelModel& model) override;

			void SetGameMap(stmp::optional<client::GameMap&>) override;

			void SetFogDistance(float) override;
			void SetFogColor(Vector3) override;

			Vector3 GetFogColor() { return fogColor; }
			Vector3 GetFogColorForSolidPass();
			float GetFogDistance() { return fogDistance; }

			const client::SceneDefinition& GetSceneDef() const { return sceneDef; }
			const Matrix4& GetProjectionViewMatrix() const { return projectionViewMatrix; }
			const Matrix4& GetViewMatrix() const { return viewMatrix; }
			const Matrix4& GetProjectionMatrix() const { return projectionMatrix; }
			VulkanImage* GetWhiteImage() { return whiteImage.GetPointerOrNull(); }
			VulkanImage* GetDepthImageWrapper() { return depthImageWrapper.GetPointerOrNull(); }
			VkImageView GetDepthImageView() const { return depthImageView; }
			bool IsRenderingMirror() const { return renderingMirror; }

			void StartScene(const client::SceneDefinition& def) override;

			void AddDebugLine(Vector3 a, Vector3 b, Vector4 color) override;

			void AddSprite(client::IImage&, Vector3 center, float radius, float rotation) override;
			void AddLongSprite(client::IImage&, Vector3 p1, Vector3 p2, float radius) override;

			void AddLight(const client::DynamicLightParam& light) override;

			void RenderModel(client::IModel&, const client::ModelRenderParam& param) override;

			void EndScene() override;

			void MultiplyScreenColor(Vector3) override;

			void SetColor(Vector4) override;
			void SetColorAlphaPremultiplied(Vector4) override;

			void DrawImage(stmp::optional<client::IImage&>, const Vector2& outTopLeft) override;
			void DrawImage(stmp::optional<client::IImage&>, const AABB2& outRect) override;
			void DrawImage(stmp::optional<client::IImage&>, const Vector2& outTopLeft, const AABB2& inRect) override;
			void DrawImage(stmp::optional<client::IImage&>, const AABB2& outRect, const AABB2& inRect) override;
			void DrawImage(stmp::optional<client::IImage&>, const Vector2& outTopLeft, const Vector2& outTopRight,
			               const Vector2& outBottomLeft, const AABB2& inRect) override;

			void UpdateFlatGameMap() override;
			void DrawFlatGameMap(const AABB2& outRect, const AABB2& inRect) override;

			void FrameDone() override;
			void Flip() override;

			Handle<Bitmap> ReadBitmap() override;

			float ScreenWidth() override;
			float ScreenHeight() override;

			// IGameMapListener implementation
			void GameMapChanged(int x, int y, int z, client::GameMap*) override;

			// Vulkan-specific methods
			VulkanMapRenderer* GetMapRenderer() { return mapRenderer; }
			VulkanModelRenderer* GetModelRenderer() { return modelRenderer; }
			VulkanSpriteRenderer* GetSpriteRenderer() { return spriteRenderer; }
			VulkanWaterRenderer* GetWaterRenderer() { return waterRenderer; }
			VulkanShadowMapRenderer* GetShadowMapRenderer() { return shadowMapRenderer; }
			VulkanFramebufferManager* GetFramebufferManager() { return framebufferManager; }
			VkPipelineCache GetPipelineCache() const;

		// Program registration API
		VulkanProgram* RegisterProgram(const std::string& name);
		VulkanShader* RegisterShader(const std::string& name);
			Handle<gui::SDLVulkanDevice> GetDevice() { return device; }
			VkRenderPass GetRenderPass() const { return renderPass; } // Swapchain render pass (for UI)
			VkRenderPass GetOffscreenRenderPass() const; // Offscreen render pass (for 3D)

			// Queue a buffer for deferred deletion (will be deleted after GPU is done with it)
			void QueueBufferForDeletion(Handle<VulkanBuffer> buffer);

			// Temporary image pool for render target reuse
			VulkanTemporaryImagePool* GetTemporaryImagePool() { return temporaryImagePool.GetPointerOrNull(); }
		};

	} // namespace draw
} // namespace spades
