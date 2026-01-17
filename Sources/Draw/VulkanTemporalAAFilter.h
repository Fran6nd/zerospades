/*
 Copyright (c) 2017 yvt

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
#include <Core/Math.h>
#include <cstddef>

namespace spades {
	namespace draw {
		class VulkanTemporalAAFilter : public VulkanPostProcessFilter {
		private:
			struct HistoryBuffer {
				bool valid = false;
				int width, height;
				Handle<VulkanImage> image;
				VkFramebuffer framebuffer;
			} historyBuffer;

			Matrix4 prevMatrix;
			Vector3 prevViewOrigin;
			std::size_t jitterTableIndex = 0;

			void CreatePipeline() override;
			void CreateRenderPass() override;
			void DeleteHistoryBuffer();

		public:
			VulkanTemporalAAFilter(VulkanRenderer&);
			~VulkanTemporalAAFilter();

			Vector2 GetProjectionMatrixJitter();

			void Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output) override;
			void Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output, bool useFxaa);
		};
	}
}
