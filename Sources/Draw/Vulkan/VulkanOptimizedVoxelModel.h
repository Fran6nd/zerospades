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

#include <vector>
#include <vulkan/vulkan.h>
#include "VulkanModel.h"
#include <Core/VoxelModel.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanRenderer;
		class VulkanBuffer;
		class VulkanImage;

		class VulkanOptimizedVoxelModel : public VulkanModel {
			struct Vertex {
				uint8_t x, y, z;
				uint8_t padding;

				// color
				uint8_t colorR, colorG, colorB;
				uint8_t padding2;

				// normal
				int8_t nx, ny, nz;

				uint8_t padding3;
			};

			// Shared pipeline cache across all model instances
			struct PipelineCache {
				VkRenderPass renderPass;
				VkPipeline pipeline;
				VkPipeline dlightPipeline;
				VkPipeline shadowMapPipeline;
				VkPipeline outlinesPipeline;
				VkPipelineLayout pipelineLayout;
				VkPipelineLayout dlightPipelineLayout;
				VkDescriptorSetLayout descriptorSetLayout;
				bool physicalLighting;

				PipelineCache() : renderPass(VK_NULL_HANDLE), pipeline(VK_NULL_HANDLE),
				                  dlightPipeline(VK_NULL_HANDLE), shadowMapPipeline(VK_NULL_HANDLE),
				                  outlinesPipeline(VK_NULL_HANDLE), pipelineLayout(VK_NULL_HANDLE),
				                  dlightPipelineLayout(VK_NULL_HANDLE),
				                  descriptorSetLayout(VK_NULL_HANDLE),
				                  physicalLighting(false) {}
			};

			static PipelineCache sharedPipeline;
			static int pipelineRefCount;

			VulkanRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;

			VkDescriptorPool descriptorPool;
			VkDescriptorSet descriptorSet;

			Handle<VulkanImage> image;
			Handle<VulkanImage> aoImage;

			Handle<VulkanBuffer> vertexBuffer;
			Handle<VulkanBuffer> indexBuffer;
			std::vector<Vertex> vertices;
			std::vector<uint32_t> indices;
			std::vector<uint16_t> bmpIndex; // bmp id for vertex (not index)
			std::vector<Bitmap*> bmps;
			unsigned int numIndices;

			Vector3 origin;
			float radius;
			IntVector3 dimensions;

			AABB3 boundingBox;

			uint8_t calcAOID(VoxelModel*, int x, int y, int z,
			                 int ux, int uy, int uz, int vx, int vy, int vz);
			void BuildVertices(VoxelModel*);
			void GenerateTexture();
			void CreatePipeline(VkRenderPass renderPass);

		protected:
			~VulkanOptimizedVoxelModel();

		public:
			VulkanOptimizedVoxelModel(VoxelModel*, VulkanRenderer& r);

			static void PreloadShaders(VulkanRenderer&);
			static void InvalidateSharedPipeline(gui::SDLVulkanDevice* device);

			void Prerender(VkCommandBuffer commandBuffer,
			               std::vector<client::ModelRenderParam> params,
			               bool ghostPass) override;
			void RenderShadowMapPass(VkCommandBuffer commandBuffer,
			                         std::vector<client::ModelRenderParam> params) override;
			void RenderSunlightPass(VkCommandBuffer commandBuffer,
			                        std::vector<client::ModelRenderParam> params,
			                        bool ghostPass) override;
			void RenderDynamicLightPass(VkCommandBuffer commandBuffer,
			                            std::vector<client::ModelRenderParam> params,
			                            std::vector<void*> lights) override;
			void RenderOutlinePass(VkCommandBuffer commandBuffer,
			                       std::vector<client::ModelRenderParam> params) override;

			IntVector3 GetDimensions() override { return dimensions; }
			AABB3 GetBoundingBox() override { return boundingBox; }
		};
	} // namespace draw
} // namespace spades
