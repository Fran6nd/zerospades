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
#include <Client/IModel.h>
#include <Client/IRenderer.h>

namespace spades {
	namespace draw {
		class VulkanModelRenderer;

		class VulkanModel : public client::IModel {
			friend class VulkanModelRenderer;

		public:
			VulkanModel();

			/** Renders for shadow map */
			virtual void RenderShadowMapPass(VkCommandBuffer commandBuffer,
			                                 std::vector<client::ModelRenderParam> params) = 0;

			/** Renders only in depth buffer (optional) */
			virtual void Prerender(VkCommandBuffer commandBuffer,
			                       std::vector<client::ModelRenderParam> params,
			                       bool ghostPass) = 0;

			/** Renders sunlighted solid geometry */
			virtual void RenderSunlightPass(VkCommandBuffer commandBuffer,
			                                std::vector<client::ModelRenderParam> params,
			                                bool ghostPass) = 0;

			/** Adds dynamic light */
			virtual void RenderDynamicLightPass(VkCommandBuffer commandBuffer,
			                                    std::vector<client::ModelRenderParam> params,
			                                    std::vector<void*> lights) = 0;

			virtual void RenderOutlinePass(VkCommandBuffer commandBuffer,
			                               std::vector<client::ModelRenderParam> params) = 0;

		private:
			// members used when rendering by VulkanModelRenderer
			int renderId;
		};
	} // namespace draw
} // namespace spades
