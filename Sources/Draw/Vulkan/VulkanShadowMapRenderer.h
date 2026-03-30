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
#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanRenderer;
		class VulkanImage;
		class VulkanBuffer;

		class VulkanShadowMapRenderer {
		public:
			enum { NumSlices = 3 };

		private:
			VulkanRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;

			int textureSize;

			Handle<VulkanImage> shadowMapImages[NumSlices];
			VkFramebuffer framebuffers[NumSlices];
			VkRenderPass renderPass;

			Matrix4 matrix;
			Matrix4 matrices[NumSlices];
			OBB3 obb;
			float vpWidth, vpHeight;

			// Map-chunk shadow pipeline (stride 20)
			VkPipelineLayout pipelineLayout;
			VkDescriptorSetLayout descriptorSetLayout;
			VkPipeline pipeline;
			VkDescriptorPool descriptorPool;
			VkDescriptorSet descriptorSets[NumSlices];
			Handle<VulkanBuffer> uniformBuffers[NumSlices];

			// Model shadow pipeline (stride 12, full matrix push constant)
			VkPipeline modelPipeline;
			VkPipelineLayout modelPipelineLayout;

			// Cascade shadow descriptor exposed to scene renderers (set 1)
			// binding 0: UBO with 3 cascade matrices + 3 split depths
			// binding 1-3: sampler2D for each cascade depth map
			VkDescriptorSetLayout cascadeDescriptorSetLayout;
			VkDescriptorPool cascadeDescriptorPool;
			VkDescriptorSet cascadeDescriptorSet;
			Handle<VulkanBuffer> cascadeMatrixBuffer;

			void BuildMatrix(float near, float far);
			void CreateRenderPass();
			void CreateFramebuffers();
			void CreatePipeline();
			void CreateDescriptorSets();
			void CreateCascadeDescriptor();
			void DestroyResources();

		public:
			VulkanShadowMapRenderer(VulkanRenderer&);
			~VulkanShadowMapRenderer();

			void Render(VkCommandBuffer commandBuffer);

			bool Cull(const AABB3&);
			bool SphereCull(const Vector3& center, float rad);

			const Matrix4& GetMatrix() const { return matrix; }
			const Matrix4* GetMatrices() const { return matrices; }
			VulkanImage* GetShadowMapImage(int slice) { return shadowMapImages[slice].GetPointerOrNull(); }
			VkPipelineLayout GetPipelineLayout() const { return pipelineLayout; }
			VkPipeline GetModelPipeline() const { return modelPipeline; }
			VkPipelineLayout GetModelPipelineLayout() const { return modelPipelineLayout; }
			VkDescriptorSet GetCascadeDescriptorSet() const { return cascadeDescriptorSet; }
			VkDescriptorSetLayout GetCascadeDescriptorSetLayout() const { return cascadeDescriptorSetLayout; }
		};
	}
}
