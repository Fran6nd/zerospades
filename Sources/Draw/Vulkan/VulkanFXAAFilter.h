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

#include "VulkanPostProcessFilter.h"

namespace spades {
	namespace draw {
		class VulkanBuffer;

		class VulkanFXAAFilter : public VulkanPostProcessFilter {
		private:
			Handle<VulkanBuffer> uniformBuffer;
			Handle<VulkanBuffer> quadVertexBuffer;
			Handle<VulkanBuffer> quadIndexBuffer;
			VkDescriptorPool descriptorPool;
			VkFramebuffer framebuffer;

			void CreatePipeline() override;
			void CreateRenderPass() override;
			void CreateQuadBuffers();
			void CreateDescriptorPool();

		public:
			VulkanFXAAFilter(VulkanRenderer&);
			~VulkanFXAAFilter();

			void Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output) override;
		};
	}
}
