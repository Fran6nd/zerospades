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

#include <vulkan/vulkan.h>
#include <vector>
#include <Client/IGameMapListener.h>
#include <Client/IRenderer.h>
#include <Core/Math.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanRenderer;
		class VulkanMapChunk;
		class VulkanBuffer;
		class VulkanImage;

		class VulkanMapRenderer {

			friend class VulkanMapChunk;

		protected:
			VulkanRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;

			// Shader programs/pipelines
			VkPipeline depthonlyPipeline;
			VkPipeline basicPipeline;
			VkPipeline dlightPipeline;
			VkPipeline backfacePipeline;
			VkPipeline outlinesPipeline;

			VkPipelineLayout pipelineLayout;
			VkDescriptorSetLayout descriptorSetLayout;
			VkDescriptorPool descriptorPool;
			VkDescriptorSet textureDescriptorSet;

			Handle<VulkanImage> aoImage;

			Handle<VulkanBuffer> squareVertexBuffer;

			struct ChunkRenderInfo {
				bool rendered;
				float distance;
			};
			VulkanMapChunk** chunks;
			ChunkRenderInfo* chunkInfos;

			client::GameMap* gameMap;

			int numChunkWidth, numChunkHeight;
			int numChunkDepth, numChunks;

			inline int GetChunkIndex(int x, int y, int z) {
				return (x * numChunkHeight + y) * numChunkDepth + z;
			}

			inline VulkanMapChunk* GetChunk(int x, int y, int z) {
				return chunks[GetChunkIndex(x, y, z)];
			}

			void RealizeChunks(Vector3 eye);

			void DrawColumnDepth(VkCommandBuffer commandBuffer, int cx, int cy, int cz, Vector3 eye);
			void DrawColumnSunlight(VkCommandBuffer commandBuffer, int cx, int cy, int cz, Vector3 eye);
			void DrawColumnDynamicLight(VkCommandBuffer commandBuffer, int cx, int cy, int cz, Vector3 eye,
			                            const std::vector<void*>& lights);
			void DrawColumnOutline(VkCommandBuffer commandBuffer, int cx, int cy, int cz, Vector3 eye);

			void RenderBackface(VkCommandBuffer commandBuffer);

		public:
			VulkanMapRenderer(client::GameMap*, VulkanRenderer&);
			virtual ~VulkanMapRenderer();

			static void PreloadShaders(VulkanRenderer&);

			void GameMapChanged(int x, int y, int z, client::GameMap*);

			// Access to renderer for chunks
			VulkanRenderer& GetRenderer() { return renderer; }

			client::GameMap* GetMap() { return gameMap; }

			void Realize();
			void Prerender();
			void RenderSunlightPass(VkCommandBuffer commandBuffer);
			void RenderDynamicLightPass(VkCommandBuffer commandBuffer, std::vector<void*> lights);
			void RenderOutlinePass(VkCommandBuffer commandBuffer);
			void RenderDepthPass(VkCommandBuffer commandBuffer);
			void RenderShadowMapPass(VkCommandBuffer commandBuffer, VkPipelineLayout shadowPipelineLayout);

			void CreatePipelines(VkRenderPass renderPass);
			void DestroyPipelines();
		};
	} // namespace draw
} // namespace spades
