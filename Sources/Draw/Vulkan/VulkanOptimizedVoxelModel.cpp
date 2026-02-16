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

#include "VulkanOptimizedVoxelModel.h"
#include "VulkanRenderer.h"
#include "VulkanMapRenderer.h"
#include "VulkanBuffer.h"
#include "VulkanImage.h"
#include "VulkanImageWrapper.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Bitmap.h>
#include <Core/BitmapAtlasGenerator.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/Settings.h>
#include <Core/IStream.h>
#include <array>
#include <map>

namespace spades {
	namespace draw {
		// Initialize static members
		VulkanOptimizedVoxelModel::PipelineCache VulkanOptimizedVoxelModel::sharedPipeline;
		int VulkanOptimizedVoxelModel::pipelineRefCount = 0;

		void VulkanOptimizedVoxelModel::PreloadShaders(VulkanRenderer& renderer) {
			SPADES_MARK_FUNCTION();
			// TODO: Preload shaders when shader system is complete
		}

		void VulkanOptimizedVoxelModel::InvalidateSharedPipeline(gui::SDLVulkanDevice* device) {
			SPADES_MARK_FUNCTION();

			if (!device || sharedPipeline.pipeline == VK_NULL_HANDLE)
				return;

			VkDevice vkDevice = device->GetDevice();

			// Clean up all shared pipelines
			if (sharedPipeline.pipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, sharedPipeline.pipeline, nullptr);
				sharedPipeline.pipeline = VK_NULL_HANDLE;
			}
			if (sharedPipeline.dlightPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, sharedPipeline.dlightPipeline, nullptr);
				sharedPipeline.dlightPipeline = VK_NULL_HANDLE;
			}
			if (sharedPipeline.shadowMapPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, sharedPipeline.shadowMapPipeline, nullptr);
				sharedPipeline.shadowMapPipeline = VK_NULL_HANDLE;
			}
			if (sharedPipeline.outlinesPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, sharedPipeline.outlinesPipeline, nullptr);
				sharedPipeline.outlinesPipeline = VK_NULL_HANDLE;
			}
			if (sharedPipeline.pipelineLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, sharedPipeline.pipelineLayout, nullptr);
				sharedPipeline.pipelineLayout = VK_NULL_HANDLE;
			}
			if (sharedPipeline.dlightPipelineLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, sharedPipeline.dlightPipelineLayout, nullptr);
				sharedPipeline.dlightPipelineLayout = VK_NULL_HANDLE;
			}
			if (sharedPipeline.descriptorSetLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(vkDevice, sharedPipeline.descriptorSetLayout, nullptr);
				sharedPipeline.descriptorSetLayout = VK_NULL_HANDLE;
			}
			sharedPipeline.renderPass = VK_NULL_HANDLE;

			SPLog("Invalidated shared voxel model pipeline cache");
		}

		VulkanOptimizedVoxelModel::VulkanOptimizedVoxelModel(VoxelModel* m, VulkanRenderer& r)
		    : renderer(r),
		      device(static_cast<gui::SDLVulkanDevice*>(r.GetDevice().Unmanage())),
		      descriptorPool(VK_NULL_HANDLE),
		      descriptorSet(VK_NULL_HANDLE),
		      numIndices(0) {
			SPADES_MARK_FUNCTION();

			// Increment reference count for shared pipeline
			pipelineRefCount++;

			BuildVertices(m);

			// Create vertex buffer
			if (!vertices.empty()) {
				size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
				vertexBuffer = Handle<VulkanBuffer>::New(
				    device, vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

				void* data = vertexBuffer->Map();
				memcpy(data, vertices.data(), vertexBufferSize);
				vertexBuffer->Unmap();
			}

			// Create index buffer
			if (!indices.empty()) {
				size_t indexBufferSize = indices.size() * sizeof(uint32_t);
				indexBuffer = Handle<VulkanBuffer>::New(
				    device, indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

				void* data = indexBuffer->Map();
				memcpy(data, indices.data(), indexBufferSize);
				indexBuffer->Unmap();
			}

			origin = m->GetOrigin();
			origin -= 0.5F; // (0,0,0) is center of voxel (0,0,0)

			dimensions.x = m->GetWidth();
			dimensions.y = m->GetHeight();
			dimensions.z = m->GetDepth();

			Vector3 minPos = {0, 0, 0};
			Vector3 maxPos = MakeVector3(dimensions);
			minPos += origin;
			maxPos += origin;
			Vector3 maxDiff = {
			    std::max(fabsf(minPos.x), fabsf(maxPos.x)),
			    std::max(fabsf(minPos.y), fabsf(maxPos.y)),
			    std::max(fabsf(minPos.z), fabsf(maxPos.z))
			};
			radius = maxDiff.GetLength();

			boundingBox.min = minPos;
			boundingBox.max = maxPos;

			// Clean up CPU-side data
			numIndices = (unsigned int)indices.size();
			std::vector<Vertex>().swap(vertices);
			std::vector<uint32_t>().swap(indices);
		}

		VulkanOptimizedVoxelModel::~VulkanOptimizedVoxelModel() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			// Clean up per-instance resources
			if (descriptorPool != VK_NULL_HANDLE) {
				vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
			}

			// Decrement reference count and clean up shared pipeline if this is the last instance
			pipelineRefCount--;
			if (pipelineRefCount == 0) {
				if (sharedPipeline.pipeline != VK_NULL_HANDLE) {
					vkDestroyPipeline(vkDevice, sharedPipeline.pipeline, nullptr);
					sharedPipeline.pipeline = VK_NULL_HANDLE;
				}
				if (sharedPipeline.dlightPipeline != VK_NULL_HANDLE) {
					vkDestroyPipeline(vkDevice, sharedPipeline.dlightPipeline, nullptr);
					sharedPipeline.dlightPipeline = VK_NULL_HANDLE;
				}
				if (sharedPipeline.shadowMapPipeline != VK_NULL_HANDLE) {
					vkDestroyPipeline(vkDevice, sharedPipeline.shadowMapPipeline, nullptr);
					sharedPipeline.shadowMapPipeline = VK_NULL_HANDLE;
				}
				if (sharedPipeline.outlinesPipeline != VK_NULL_HANDLE) {
					vkDestroyPipeline(vkDevice, sharedPipeline.outlinesPipeline, nullptr);
					sharedPipeline.outlinesPipeline = VK_NULL_HANDLE;
				}
				if (sharedPipeline.pipelineLayout != VK_NULL_HANDLE) {
					vkDestroyPipelineLayout(vkDevice, sharedPipeline.pipelineLayout, nullptr);
					sharedPipeline.pipelineLayout = VK_NULL_HANDLE;
				}
				if (sharedPipeline.dlightPipelineLayout != VK_NULL_HANDLE) {
					vkDestroyPipelineLayout(vkDevice, sharedPipeline.dlightPipelineLayout, nullptr);
					sharedPipeline.dlightPipelineLayout = VK_NULL_HANDLE;
				}
				if (sharedPipeline.descriptorSetLayout != VK_NULL_HANDLE) {
					vkDestroyDescriptorSetLayout(vkDevice, sharedPipeline.descriptorSetLayout, nullptr);
					sharedPipeline.descriptorSetLayout = VK_NULL_HANDLE;
				}
				sharedPipeline.renderPass = VK_NULL_HANDLE;
			}
		}

		void VulkanOptimizedVoxelModel::GenerateTexture() {
			SPADES_MARK_FUNCTION();

			if (bmps.empty()) {
				return;
			}

			// Since we're using vertex colors, we don't need texture atlas
			// Just release the bitmaps and clear the index
			for (size_t i = 0; i < bmps.size(); i++)
				bmps[i]->Release();
			bmps.clear();

			std::vector<uint16_t>().swap(bmpIndex);

			// Create a white placeholder texture for compatibility
			Handle<Bitmap> bmp(new Bitmap(1, 1), false);
			bmp->SetPixel(0, 0, 0xFFFFFFFF);

			// Create Vulkan texture from bitmap
			Handle<client::IImage> imgHandle = renderer.CreateImage(*bmp);

			// Get VulkanImage from the IImage (unwrap from VulkanImageWrapper if needed)
			VulkanImageWrapper* wrapper = dynamic_cast<VulkanImageWrapper*>(imgHandle.GetPointerOrNull());
			if (wrapper) {
				image = Handle<VulkanImage>(wrapper->GetVulkanImage());
			} else {
				image = imgHandle.Cast<VulkanImage>();
			}
		}

		uint8_t VulkanOptimizedVoxelModel::calcAOID(VoxelModel* m, int x, int y, int z,
		                                            int ux, int uy, int uz, int vx, int vy, int vz) {
			int v = 0;
			if (m->IsSolid(x - ux, y - uy, z - uz))
				v |= 1;
			if (m->IsSolid(x + ux, y + uy, z + uz))
				v |= 1 << 1;
			if (m->IsSolid(x - vx, y - vy, z - vz))
				v |= 1 << 2;
			if (m->IsSolid(x + vx, y + vy, z + vz))
				v |= 1 << 3;
			if (m->IsSolid(x - ux + vx, y - uy + vy, z - uz + vz))
				v |= 1 << 4;
			if (m->IsSolid(x - ux - vx, y - uy - vy, z - uz - vz))
				v |= 1 << 5;
			if (m->IsSolid(x + ux + vx, y + uy + vy, z + uz + vz))
				v |= 1 << 6;
			if (m->IsSolid(x + ux - vx, y + uy - vy, z + uz - vz))
				v |= 1 << 7;
			return (uint8_t)v;
		}

		void VulkanOptimizedVoxelModel::BuildVertices(VoxelModel* m) {
			SPADES_MARK_FUNCTION();

			// Use vertex colors instead of textures for simplicity and performance
			int w = m->GetWidth();
			int h = m->GetHeight();
			int d = m->GetDepth();

			// Helper lambda to emit a face with vertex colors
			auto EmitFace = [&](int x, int y, int z, int nx, int ny, int nz, uint32_t color) {
				uint32_t idx = (uint32_t)vertices.size();

				// Extract RGB components
				uint8_t r = color & 0xFF;
				uint8_t g = (color >> 8) & 0xFF;
				uint8_t b = (color >> 16) & 0xFF;

				// Calculate face vertices based on normal direction
				Vertex v[4];
				for (int i = 0; i < 4; i++) {
					v[i].nx = nx;
					v[i].ny = ny;
					v[i].nz = nz;
					v[i].colorR = r;
					v[i].colorG = g;
					v[i].colorB = b;
					v[i].padding = 0;
					v[i].padding2 = 0;
				}

				if (nx == 1) { // +X face
					v[0].x = x + 1; v[0].y = y; v[0].z = z;
					v[1].x = x + 1; v[1].y = y + 1; v[1].z = z;
					v[2].x = x + 1; v[2].y = y + 1; v[2].z = z + 1;
					v[3].x = x + 1; v[3].y = y; v[3].z = z + 1;
				} else if (nx == -1) { // -X face
					v[0].x = x; v[0].y = y; v[0].z = z;
					v[1].x = x; v[1].y = y; v[1].z = z + 1;
					v[2].x = x; v[2].y = y + 1; v[2].z = z + 1;
					v[3].x = x; v[3].y = y + 1; v[3].z = z;
				} else if (ny == 1) { // +Y face
					v[0].x = x; v[0].y = y + 1; v[0].z = z;
					v[1].x = x; v[1].y = y + 1; v[1].z = z + 1;
					v[2].x = x + 1; v[2].y = y + 1; v[2].z = z + 1;
					v[3].x = x + 1; v[3].y = y + 1; v[3].z = z;
				} else if (ny == -1) { // -Y face
					v[0].x = x; v[0].y = y; v[0].z = z;
					v[1].x = x + 1; v[1].y = y; v[1].z = z;
					v[2].x = x + 1; v[2].y = y; v[2].z = z + 1;
					v[3].x = x; v[3].y = y; v[3].z = z + 1;
				} else if (nz == 1) { // +Z face
					v[0].x = x; v[0].y = y; v[0].z = z + 1;
					v[1].x = x + 1; v[1].y = y; v[1].z = z + 1;
					v[2].x = x + 1; v[2].y = y + 1; v[2].z = z + 1;
					v[3].x = x; v[3].y = y + 1; v[3].z = z + 1;
				} else { // -Z face (nz == -1)
					v[0].x = x; v[0].y = y; v[0].z = z;
					v[1].x = x; v[1].y = y + 1; v[1].z = z;
					v[2].x = x + 1; v[2].y = y + 1; v[2].z = z;
					v[3].x = x + 1; v[3].y = y; v[3].z = z;
				}

				// Add vertices
				for (int i = 0; i < 4; i++) {
					vertices.push_back(v[i]);
				}

				// Add indices for two triangles
				indices.push_back(idx);
				indices.push_back(idx + 1);
				indices.push_back(idx + 2);
				indices.push_back(idx);
				indices.push_back(idx + 2);
				indices.push_back(idx + 3);
			};

			// Generate faces for all solid voxels
			for (int x = 0; x < w; x++) {
				for (int y = 0; y < h; y++) {
					for (int z = 0; z < d; z++) {
						if (!m->IsSolid(x, y, z))
							continue;

						uint32_t color = m->GetColor(x, y, z);

						// Check each face and emit if exposed
						if (!m->IsSolid(x + 1, y, z)) EmitFace(x, y, z, 1, 0, 0, color);
						if (!m->IsSolid(x - 1, y, z)) EmitFace(x, y, z, -1, 0, 0, color);
						if (!m->IsSolid(x, y + 1, z)) EmitFace(x, y, z, 0, 1, 0, color);
						if (!m->IsSolid(x, y - 1, z)) EmitFace(x, y, z, 0, -1, 0, color);
						if (!m->IsSolid(x, y, z + 1)) EmitFace(x, y, z, 0, 0, 1, color);
						if (!m->IsSolid(x, y, z - 1)) EmitFace(x, y, z, 0, 0, -1, color);
					}
				}
			}
		}

		void VulkanOptimizedVoxelModel::Prerender(VkCommandBuffer commandBuffer,
		                                          std::vector<client::ModelRenderParam> params,
		                                          bool ghostPass) {
			SPADES_MARK_FUNCTION();

			// Depth prerender writes only to depth buffer, no color output
			// This is used for early-Z optimization and occlusion culling

			if (numIndices == 0 || !vertexBuffer || !indexBuffer)
				return;

			// Skip if no instances to render
			if (params.empty())
				return;

			// Use the same rendering as sunlight pass but with depth-only pipeline
			// For now, reuse the standard pipeline (full implementation needs depth-only pipeline)
			VkRenderPass renderPass = renderer.GetOffscreenRenderPass();
			if (sharedPipeline.pipeline == VK_NULL_HANDLE || sharedPipeline.renderPass != renderPass) {
				CreatePipeline(renderPass);
			}

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, sharedPipeline.pipeline);

			// Bind vertex buffer
			VkBuffer vb = vertexBuffer->GetBuffer();
			VkDeviceSize offsets[] = {0};
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, offsets);

			// Bind index buffer
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

			const Matrix4& projectionViewMatrix = renderer.GetProjectionViewMatrix();
			const auto& eye = renderer.GetSceneDef().viewOrigin;
			Vector3 fogCol = renderer.GetFogColor();
			fogCol *= fogCol; // linearize
			float fogDist = renderer.GetFogDistance();

			for (const auto& param : params) {
				Matrix4 mvpMatrix = projectionViewMatrix * param.matrix;

				// Compute fog density from model's world position
				Vector4 modelWorldPos4 = param.matrix * MakeVector4(origin.x, origin.y, origin.z, 1.0f);
				float dx = modelWorldPos4.x - eye.x;
				float dy = modelWorldPos4.y - eye.y;
				float horzDistSq = dx * dx + dy * dy;
				float fogDensity = std::min(horzDistSq / (fogDist * fogDist), 1.0f);

				struct {
					Matrix4 projectionViewMatrix;
					Matrix4 modelMatrix;
					Vector3 modelOrigin;
					float fogDensity;
					Vector3 customColor;
					float _pad;
					Vector3 fogColor;
					float _pad2;
					Matrix4 viewMatrix;
					Vector3 viewOrigin;
				} pushConstants;

				pushConstants.projectionViewMatrix = mvpMatrix;
				pushConstants.modelMatrix = param.matrix;
				pushConstants.modelOrigin = origin;
				pushConstants.fogDensity = fogDensity;
				pushConstants.customColor = param.customColor;
				pushConstants._pad = 0.0f;
				pushConstants.fogColor = fogCol;

				uint32_t pcSize = 172;
				VkShaderStageFlags pcStages = VK_SHADER_STAGE_VERTEX_BIT;
				if (sharedPipeline.physicalLighting) {
					pushConstants._pad2 = 0.0f;
					pushConstants.viewMatrix = renderer.GetViewMatrix();
					pushConstants.viewOrigin = renderer.GetSceneDef().viewOrigin;
					pcSize = sizeof(pushConstants);
					pcStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
				}
				vkCmdPushConstants(commandBuffer, sharedPipeline.pipelineLayout, pcStages,
				                   0, pcSize, &pushConstants);

				vkCmdDrawIndexed(commandBuffer, numIndices, 1, 0, 0, 0);
			}
		}

		void VulkanOptimizedVoxelModel::RenderShadowMapPass(VkCommandBuffer commandBuffer,
		                                                    std::vector<client::ModelRenderParam> params) {
			SPADES_MARK_FUNCTION();

			if (numIndices == 0 || !vertexBuffer || !indexBuffer)
				return;

			if (params.empty())
				return;

			// Bind vertex buffer
			VkBuffer vb = vertexBuffer->GetBuffer();
			VkDeviceSize offsets[] = {0};
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, offsets);

			// Bind index buffer
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

			// Render each model instance
			for (const auto& param : params) {
				vkCmdDrawIndexed(commandBuffer, numIndices, 1, 0, 0, 0);
			}
		}

		void VulkanOptimizedVoxelModel::RenderSunlightPass(VkCommandBuffer commandBuffer,
		                                                   std::vector<client::ModelRenderParam> params,
		                                                   bool ghostPass) {
			SPADES_MARK_FUNCTION();

			if (numIndices == 0 || !vertexBuffer || !indexBuffer)
				return;

			// Lazy pipeline creation on first render (shared across all instances)
			VkRenderPass renderPass = renderer.GetOffscreenRenderPass();
			if (sharedPipeline.pipeline == VK_NULL_HANDLE || sharedPipeline.renderPass != renderPass) {
				CreatePipeline(renderPass);
			}

			// Bind pipeline
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, sharedPipeline.pipeline);

			// Bind shadow map descriptor set from map renderer
			VulkanMapRenderer* mapRenderer = renderer.GetMapRenderer();
			if (mapRenderer) {
				VkDescriptorSet shadowDs = mapRenderer->GetShadowDescriptorSet();
				if (shadowDs != VK_NULL_HANDLE) {
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
					                        sharedPipeline.pipelineLayout, 0, 1,
					                        &shadowDs, 0, nullptr);
				}
			}

			// Bind vertex buffer
			VkBuffer vb = vertexBuffer->GetBuffer();
			VkDeviceSize offsets[] = {0};
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, offsets);

			// Bind index buffer
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

			// Get projection-view matrix from renderer
			const Matrix4& projectionViewMatrix = renderer.GetProjectionViewMatrix();
			const auto& eye = renderer.GetSceneDef().viewOrigin;
			Vector3 fogCol = renderer.GetFogColor();
			fogCol *= fogCol; // linearize
			float fogDist = renderer.GetFogDistance();

			// Draw each instance
			for (const auto& param : params) {
				// Compute final MVP matrix
				Matrix4 mvpMatrix = projectionViewMatrix * param.matrix;

				// Compute fog density from model's world position
				Vector4 modelWorldPos4 = param.matrix * MakeVector4(origin.x, origin.y, origin.z, 1.0f);
				float dx = modelWorldPos4.x - eye.x;
				float dy = modelWorldPos4.y - eye.y;
				float horzDistSq = dx * dx + dy * dy;
				float fogDensity = std::min(horzDistSq / (fogDist * fogDist), 1.0f);

				struct {
					Matrix4 projectionViewMatrix;
					Matrix4 modelMatrix;
					Vector3 modelOrigin;
					float fogDensity;
					Vector3 customColor;
					float _pad;
					Vector3 fogColor;
					float _pad2;
					Matrix4 viewMatrix;
					Vector3 viewOrigin;
				} pushConstants;

				pushConstants.projectionViewMatrix = mvpMatrix;
				pushConstants.modelMatrix = param.matrix;
				pushConstants.modelOrigin = origin;
				pushConstants.fogDensity = fogDensity;
				pushConstants.customColor = param.customColor;
				pushConstants._pad = 0.0f;
				pushConstants.fogColor = fogCol;

				uint32_t pcSize = 172;
				VkShaderStageFlags pcStages = VK_SHADER_STAGE_VERTEX_BIT;
				if (sharedPipeline.physicalLighting) {
					pushConstants._pad2 = 0.0f;
					pushConstants.viewMatrix = renderer.GetViewMatrix();
					pushConstants.viewOrigin = renderer.GetSceneDef().viewOrigin;
					pcSize = sizeof(pushConstants);
					pcStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
				}
				vkCmdPushConstants(commandBuffer, sharedPipeline.pipelineLayout, pcStages,
				                   0, pcSize, &pushConstants);

				vkCmdDrawIndexed(commandBuffer, numIndices, 1, 0, 0, 0);
			}
		}

		void VulkanOptimizedVoxelModel::RenderDynamicLightPass(VkCommandBuffer commandBuffer,
		                                                       std::vector<client::ModelRenderParam> params,
		                                                       std::vector<void*> lights) {
			SPADES_MARK_FUNCTION();

			if (numIndices == 0 || !vertexBuffer || !indexBuffer)
				return;

			if (params.empty() || lights.empty())
				return;

			VkRenderPass renderPass = renderer.GetOffscreenRenderPass();
			if (sharedPipeline.dlightPipeline == VK_NULL_HANDLE || sharedPipeline.renderPass != renderPass) {
				CreatePipeline(renderPass);
			}

			if (sharedPipeline.dlightPipeline == VK_NULL_HANDLE)
				return;

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, sharedPipeline.dlightPipeline);

			// Bind vertex buffer
			VkBuffer vb = vertexBuffer->GetBuffer();
			VkDeviceSize offsets[] = {0};
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, offsets);

			// Bind index buffer
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

			const Matrix4& projectionViewMatrix = renderer.GetProjectionViewMatrix();
			const auto& eye = renderer.GetSceneDef().viewOrigin;
			float fogDist = renderer.GetFogDistance();

			for (void* lightPtr : lights) {
				const client::DynamicLightParam* light =
				    static_cast<const client::DynamicLightParam*>(lightPtr);

				// Light type
				float lightType = 0.0f; // point
				if (light->type == client::DynamicLightTypeLinear)
					lightType = 1.0f;
				else if (light->type == client::DynamicLightTypeSpotlight)
					lightType = 2.0f;

				// Linear light direction and length
				Vector3 linearDir = MakeVector3(0, 0, 0);
				float linearLength = 0.0f;
				if (light->type == client::DynamicLightTypeLinear) {
					Vector3 dir = light->point2 - light->origin;
					linearLength = dir.GetLength();
					if (linearLength > 0.0001f)
						linearDir = dir / linearLength;
				}

				for (const auto& param : params) {
					Matrix4 mvpMatrix = projectionViewMatrix * param.matrix;

					// Compute fog density from model's world position
					Vector4 modelWorldPos4 = param.matrix * MakeVector4(origin.x, origin.y, origin.z, 1.0f);
					float dx = modelWorldPos4.x - eye.x;
					float dy = modelWorldPos4.y - eye.y;
					float horzDistSq = dx * dx + dy * dy;
					float fogDensity = std::min(horzDistSq / (fogDist * fogDist), 1.0f);

					struct {
						Matrix4 projectionViewModelMatrix;
						Matrix4 modelMatrix;
						Vector3 modelOrigin;
						float fogDensityVal;
						Vector3 customColor;
						float lightRadius;
						Vector3 lightOrigin;
						float lightTypeVal;
						Vector3 lightColor;
						float lightRadiusInversed;
						Vector3 lightLinearDirection;
						float lightLinearLength;
					} pushConstants;

					pushConstants.projectionViewModelMatrix = mvpMatrix;
					pushConstants.modelMatrix = param.matrix;
					pushConstants.modelOrigin = origin;
					pushConstants.fogDensityVal = fogDensity;
					pushConstants.customColor = param.customColor;
					pushConstants.lightRadius = light->radius;
					pushConstants.lightOrigin = light->origin;
					pushConstants.lightTypeVal = lightType;
					pushConstants.lightColor = light->color;
					pushConstants.lightRadiusInversed = 1.0f / light->radius;
					pushConstants.lightLinearDirection = linearDir;
					pushConstants.lightLinearLength = linearLength;

					vkCmdPushConstants(commandBuffer, sharedPipeline.dlightPipelineLayout,
					                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
					                   0, sizeof(pushConstants), &pushConstants);

					vkCmdDrawIndexed(commandBuffer, numIndices, 1, 0, 0, 0);
				}
			}
		}

		void VulkanOptimizedVoxelModel::RenderOutlinePass(VkCommandBuffer commandBuffer,
		                                                  std::vector<client::ModelRenderParam> params) {
			SPADES_MARK_FUNCTION();

			if (numIndices == 0 || !vertexBuffer || !indexBuffer)
				return;

			if (params.empty())
				return;

			VkRenderPass renderPass = renderer.GetOffscreenRenderPass();
			if (sharedPipeline.outlinesPipeline == VK_NULL_HANDLE || sharedPipeline.renderPass != renderPass) {
				CreatePipeline(renderPass);
			}

			if (sharedPipeline.outlinesPipeline == VK_NULL_HANDLE)
				return;

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, sharedPipeline.outlinesPipeline);

			VkBuffer vb = vertexBuffer->GetBuffer();
			VkDeviceSize offsets[] = {0};
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, offsets);
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

			const Matrix4& projectionViewMatrix = renderer.GetProjectionViewMatrix();
			const auto& eye = renderer.GetSceneDef().viewOrigin;
			Vector3 fogCol = renderer.GetFogColor();
			fogCol *= fogCol; // linearize
			float fogDist = renderer.GetFogDistance();

			for (const auto& param : params) {
				Matrix4 mvpMatrix = projectionViewMatrix * param.matrix;

				Vector4 modelWorldPos4 = param.matrix * MakeVector4(origin.x, origin.y, origin.z, 1.0f);
				float dx = modelWorldPos4.x - eye.x;
				float dy = modelWorldPos4.y - eye.y;
				float horzDistSq = dx * dx + dy * dy;
				float fogDensity = std::min(horzDistSq / (fogDist * fogDist), 1.0f);

				struct {
					Matrix4 projectionViewModelMatrix;
					Matrix4 modelMatrix;
					Vector3 modelOrigin;
					float fogDensityVal;
					Vector3 customColor;
					float _pad;
					Vector3 fogColor;
				} pushConstants;

				pushConstants.projectionViewModelMatrix = mvpMatrix;
				pushConstants.modelMatrix = param.matrix;
				pushConstants.modelOrigin = origin;
				pushConstants.fogDensityVal = fogDensity;
				pushConstants.customColor = param.customColor;
				pushConstants._pad = 0.0f;
				pushConstants.fogColor = fogCol;

				vkCmdPushConstants(commandBuffer, sharedPipeline.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
				                   0, 172, &pushConstants);

				vkCmdDrawIndexed(commandBuffer, numIndices, 1, 0, 0, 0);
			}
		}

		void VulkanOptimizedVoxelModel::CreatePipeline(VkRenderPass renderPass) {
			SPADES_MARK_FUNCTION();

			// Clean up old pipeline if render pass changed
			VkDevice vkDevice = device->GetDevice();
			if (sharedPipeline.pipeline != VK_NULL_HANDLE && sharedPipeline.renderPass != renderPass) {
				// Wait for GPU to finish using the old pipeline before destroying it
				vkDeviceWaitIdle(vkDevice);
				vkDestroyPipeline(vkDevice, sharedPipeline.pipeline, nullptr);
				sharedPipeline.pipeline = VK_NULL_HANDLE;
				if (sharedPipeline.pipelineLayout != VK_NULL_HANDLE) {
					vkDestroyPipelineLayout(vkDevice, sharedPipeline.pipelineLayout, nullptr);
					sharedPipeline.pipelineLayout = VK_NULL_HANDLE;
				}
			}

			sharedPipeline.renderPass = renderPass;

			{
				SPADES_SETTING(r_physicalLighting);
				sharedPipeline.physicalLighting = (int)r_physicalLighting != 0;
			}

			// Load SPIR-V shaders
			auto LoadSPIRVFile = [](const char* filename) -> std::vector<uint32_t> {
				std::unique_ptr<IStream> stream = FileManager::OpenForReading(filename);
				if (!stream) {
					SPRaise("Failed to open shader file: %s", filename);
				}
				size_t size = stream->GetLength();
				std::vector<uint32_t> code(size / 4);
				stream->Read(code.data(), size);
				return code;
			};

			std::vector<uint32_t> vertCode, fragCode;
			if (sharedPipeline.physicalLighting) {
				vertCode = LoadSPIRVFile("Shaders/Vulkan/BasicModelVertexColorPhys.vert.spv");
				fragCode = LoadSPIRVFile("Shaders/Vulkan/BasicModelVertexColorPhys.frag.spv");
			} else {
				vertCode = LoadSPIRVFile("Shaders/Vulkan/BasicModelVertexColor.vert.spv");
				fragCode = LoadSPIRVFile("Shaders/Vulkan/BasicModelVertexColor.frag.spv");
			}

			// Create shader modules
			VkShaderModuleCreateInfo vertShaderModuleInfo{};
			vertShaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			vertShaderModuleInfo.codeSize = vertCode.size() * sizeof(uint32_t);
			vertShaderModuleInfo.pCode = vertCode.data();

			VkShaderModule vertShaderModule;
			VkResult result = vkCreateShaderModule(vkDevice, &vertShaderModuleInfo, nullptr, &vertShaderModule);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create vertex shader module (error code: %d)", result);
			}

			VkShaderModuleCreateInfo fragShaderModuleInfo{};
			fragShaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			fragShaderModuleInfo.codeSize = fragCode.size() * sizeof(uint32_t);
			fragShaderModuleInfo.pCode = fragCode.data();

			VkShaderModule fragShaderModule;
			result = vkCreateShaderModule(vkDevice, &fragShaderModuleInfo, nullptr, &fragShaderModule);
			if (result != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
				SPRaise("Failed to create fragment shader module (error code: %d)", result);
			}

			// Shader stage creation
			VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
			vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
			vertShaderStageInfo.module = vertShaderModule;
			vertShaderStageInfo.pName = "main";

			VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
			fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			fragShaderStageInfo.module = fragShaderModule;
			fragShaderStageInfo.pName = "main";

			VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

			// Vertex input state
			VkVertexInputBindingDescription bindingDescription{};
			bindingDescription.binding = 0;
			bindingDescription.stride = sizeof(Vertex);
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

			// Position (location 0)
			attributeDescriptions[0].binding = 0;
			attributeDescriptions[0].location = 0;
			attributeDescriptions[0].format = VK_FORMAT_R8G8B8_UINT;
			attributeDescriptions[0].offset = offsetof(Vertex, x);

			// Color (location 1)
			attributeDescriptions[1].binding = 0;
			attributeDescriptions[1].location = 1;
			attributeDescriptions[1].format = VK_FORMAT_R8G8B8_UINT;
			attributeDescriptions[1].offset = offsetof(Vertex, colorR);

			// Normal (location 2)
			attributeDescriptions[2].binding = 0;
			attributeDescriptions[2].location = 2;
			attributeDescriptions[2].format = VK_FORMAT_R8G8B8_SINT;
			attributeDescriptions[2].offset = offsetof(Vertex, nx);

			VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
			vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputInfo.vertexBindingDescriptionCount = 1;
			vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
			vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
			vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

			// Input assembly
			VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
			inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			inputAssembly.primitiveRestartEnable = VK_FALSE;

			// Viewport and scissor (dynamic)
			VkPipelineViewportStateCreateInfo viewportState{};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.scissorCount = 1;

			// Rasterization
			VkPipelineRasterizationStateCreateInfo rasterizer{};
			rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizer.depthClampEnable = VK_FALSE;
			rasterizer.rasterizerDiscardEnable = VK_FALSE;
			rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
			rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rasterizer.depthBiasEnable = VK_FALSE;

			// Multisampling
			VkPipelineMultisampleStateCreateInfo multisampling{};
			multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampling.sampleShadingEnable = VK_FALSE;
			multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			// Depth stencil
			VkPipelineDepthStencilStateCreateInfo depthStencil{};
			depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencil.depthTestEnable = VK_TRUE;
			depthStencil.depthWriteEnable = VK_TRUE;
			depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
			depthStencil.depthBoundsTestEnable = VK_FALSE;
			depthStencil.stencilTestEnable = VK_FALSE;

			// Color blending
			VkPipelineColorBlendAttachmentState colorBlendAttachment{};
			colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			colorBlendAttachment.blendEnable = VK_TRUE;
			colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
			colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

			VkPipelineColorBlendStateCreateInfo colorBlending{};
			colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlending.logicOpEnable = VK_FALSE;
			colorBlending.attachmentCount = 1;
			colorBlending.pAttachments = &colorBlendAttachment;

			// Dynamic state
			VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
			VkPipelineDynamicStateCreateInfo dynamicState{};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = 2;
			dynamicState.pDynamicStates = dynamicStates;

			// Create descriptor set layout for shadow map sampler (set 0, binding 0)
			{
				VkDescriptorSetLayoutBinding shadowSamplerBinding{};
				shadowSamplerBinding.binding = 0;
				shadowSamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				shadowSamplerBinding.descriptorCount = 1;
				shadowSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

				VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{};
				descriptorLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
				descriptorLayoutInfo.bindingCount = 1;
				descriptorLayoutInfo.pBindings = &shadowSamplerBinding;

				result = vkCreateDescriptorSetLayout(vkDevice, &descriptorLayoutInfo, nullptr, &sharedPipeline.descriptorSetLayout);
				if (result != VK_SUCCESS) {
					SPRaise("Failed to create model descriptor set layout (error code: %d)", result);
				}
			}

			// Pipeline layout with push constants and shadow map descriptor set
			VkPushConstantRange pushConstantRange{};
			pushConstantRange.offset = 0;
			if (sharedPipeline.physicalLighting) {
				// 172 + pad (4) + mat4 viewMatrix (64) + vec3 viewOrigin (12) = 252
				pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
				pushConstantRange.size = 252;
			} else {
				pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
				pushConstantRange.size = 172;
			}

			VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
			pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutInfo.setLayoutCount = 1;
			pipelineLayoutInfo.pSetLayouts = &sharedPipeline.descriptorSetLayout;
			pipelineLayoutInfo.pushConstantRangeCount = 1;
			pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

			result = vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &sharedPipeline.pipelineLayout);
			if (result != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
				vkDestroyShaderModule(vkDevice, fragShaderModule, nullptr);
				SPRaise("Failed to create pipeline layout (error code: %d)", result);
			}

			// Create graphics pipeline
			VkGraphicsPipelineCreateInfo pipelineInfo{};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = 2;
			pipelineInfo.pStages = shaderStages;
			pipelineInfo.pVertexInputState = &vertexInputInfo;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterizer;
			pipelineInfo.pMultisampleState = &multisampling;
			pipelineInfo.pDepthStencilState = &depthStencil;
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.pDynamicState = &dynamicState;
			pipelineInfo.layout = sharedPipeline.pipelineLayout;
			pipelineInfo.renderPass = renderPass;
			pipelineInfo.subpass = 0;

			result = vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1, &pipelineInfo, nullptr, &sharedPipeline.pipeline);

			// Cleanup shader modules
			vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
			vkDestroyShaderModule(vkDevice, fragShaderModule, nullptr);

			if (result != VK_SUCCESS) {
				SPRaise("Failed to create graphics pipeline (error code: %d)", result);
			}

			SPLog("Created shared model rendering pipeline (vertex colors)");

			// --- Create dynamic light pipeline ---
			{
				std::vector<uint32_t> dlVertCode = LoadSPIRVFile("Shaders/Vulkan/ModelDynamicLit.vert.spv");
				std::vector<uint32_t> dlFragCode = LoadSPIRVFile("Shaders/Vulkan/ModelDynamicLit.frag.spv");

				VkShaderModuleCreateInfo dlVertInfo{};
				dlVertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				dlVertInfo.codeSize = dlVertCode.size() * sizeof(uint32_t);
				dlVertInfo.pCode = dlVertCode.data();
				VkShaderModule dlVertModule;
				result = vkCreateShaderModule(vkDevice, &dlVertInfo, nullptr, &dlVertModule);
				if (result != VK_SUCCESS) {
					SPLog("Warning: Failed to create model dlight vertex shader module");
					return;
				}

				VkShaderModuleCreateInfo dlFragInfo{};
				dlFragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				dlFragInfo.codeSize = dlFragCode.size() * sizeof(uint32_t);
				dlFragInfo.pCode = dlFragCode.data();
				VkShaderModule dlFragModule;
				result = vkCreateShaderModule(vkDevice, &dlFragInfo, nullptr, &dlFragModule);
				if (result != VK_SUCCESS) {
					vkDestroyShaderModule(vkDevice, dlVertModule, nullptr);
					SPLog("Warning: Failed to create model dlight fragment shader module");
					return;
				}

				VkPipelineShaderStageCreateInfo dlStages[2]{};
				dlStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
				dlStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
				dlStages[0].module = dlVertModule;
				dlStages[0].pName = "main";
				dlStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
				dlStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
				dlStages[1].module = dlFragModule;
				dlStages[1].pName = "main";

				// Dlight pipeline layout: 208 bytes push constants
				VkPushConstantRange dlPushRange{};
				dlPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
				dlPushRange.offset = 0;
				dlPushRange.size = 208;

				VkPipelineLayoutCreateInfo dlLayoutInfo{};
				dlLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				dlLayoutInfo.pushConstantRangeCount = 1;
				dlLayoutInfo.pPushConstantRanges = &dlPushRange;

				result = vkCreatePipelineLayout(vkDevice, &dlLayoutInfo, nullptr, &sharedPipeline.dlightPipelineLayout);
				if (result != VK_SUCCESS) {
					vkDestroyShaderModule(vkDevice, dlVertModule, nullptr);
					vkDestroyShaderModule(vkDevice, dlFragModule, nullptr);
					SPLog("Warning: Failed to create model dlight pipeline layout");
					return;
				}

				// Depth: test LESS_OR_EQUAL, no write
				VkPipelineDepthStencilStateCreateInfo dlDepth{};
				dlDepth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
				dlDepth.depthTestEnable = VK_TRUE;
				dlDepth.depthWriteEnable = VK_FALSE;
				dlDepth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
				dlDepth.depthBoundsTestEnable = VK_FALSE;
				dlDepth.stencilTestEnable = VK_FALSE;

				// Additive blending
				VkPipelineColorBlendAttachmentState dlBlend{};
				dlBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
				                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
				dlBlend.blendEnable = VK_TRUE;
				dlBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				dlBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
				dlBlend.colorBlendOp = VK_BLEND_OP_ADD;
				dlBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
				dlBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				dlBlend.alphaBlendOp = VK_BLEND_OP_ADD;

				VkPipelineColorBlendStateCreateInfo dlColorBlending{};
				dlColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
				dlColorBlending.logicOpEnable = VK_FALSE;
				dlColorBlending.attachmentCount = 1;
				dlColorBlending.pAttachments = &dlBlend;

				VkGraphicsPipelineCreateInfo dlPipelineInfo{};
				dlPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
				dlPipelineInfo.stageCount = 2;
				dlPipelineInfo.pStages = dlStages;
				dlPipelineInfo.pVertexInputState = &vertexInputInfo;
				dlPipelineInfo.pInputAssemblyState = &inputAssembly;
				dlPipelineInfo.pViewportState = &viewportState;
				dlPipelineInfo.pRasterizationState = &rasterizer;
				dlPipelineInfo.pMultisampleState = &multisampling;
				dlPipelineInfo.pDepthStencilState = &dlDepth;
				dlPipelineInfo.pColorBlendState = &dlColorBlending;
				dlPipelineInfo.pDynamicState = &dynamicState;
				dlPipelineInfo.layout = sharedPipeline.dlightPipelineLayout;
				dlPipelineInfo.renderPass = renderPass;
				dlPipelineInfo.subpass = 0;

				result = vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1, &dlPipelineInfo, nullptr, &sharedPipeline.dlightPipeline);

				vkDestroyShaderModule(vkDevice, dlVertModule, nullptr);
				vkDestroyShaderModule(vkDevice, dlFragModule, nullptr);

				if (result != VK_SUCCESS) {
					SPLog("Warning: Failed to create model dlight pipeline (error code: %d)", result);
					sharedPipeline.dlightPipeline = VK_NULL_HANDLE;
				} else {
					SPLog("Created shared model dynamic light pipeline");
				}
			}

			// --- Create outline pipeline ---
			{
				std::vector<uint32_t> olVertCode = LoadSPIRVFile("Shaders/Vulkan/ModelOutline.vert.spv");
				std::vector<uint32_t> olFragCode = LoadSPIRVFile("Shaders/Vulkan/Outline.frag.spv");

				VkShaderModuleCreateInfo olVertInfo{};
				olVertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				olVertInfo.codeSize = olVertCode.size() * sizeof(uint32_t);
				olVertInfo.pCode = olVertCode.data();
				VkShaderModule olVertModule;
				result = vkCreateShaderModule(vkDevice, &olVertInfo, nullptr, &olVertModule);
				if (result != VK_SUCCESS) {
					SPLog("Warning: Failed to create model outline vertex shader module");
				} else {
					VkShaderModuleCreateInfo olFragInfo{};
					olFragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
					olFragInfo.codeSize = olFragCode.size() * sizeof(uint32_t);
					olFragInfo.pCode = olFragCode.data();
					VkShaderModule olFragModule;
					result = vkCreateShaderModule(vkDevice, &olFragInfo, nullptr, &olFragModule);
					if (result != VK_SUCCESS) {
						vkDestroyShaderModule(vkDevice, olVertModule, nullptr);
						SPLog("Warning: Failed to create model outline fragment shader module");
					} else {
						VkPipelineShaderStageCreateInfo olStages[2]{};
						olStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
						olStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
						olStages[0].module = olVertModule;
						olStages[0].pName = "main";
						olStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
						olStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
						olStages[1].module = olFragModule;
						olStages[1].pName = "main";

						VkPipelineRasterizationStateCreateInfo olRasterizer{};
						olRasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
						olRasterizer.depthClampEnable = VK_FALSE;
						olRasterizer.rasterizerDiscardEnable = VK_FALSE;
						olRasterizer.polygonMode = VK_POLYGON_MODE_LINE;
						olRasterizer.lineWidth = 1.0f;
						olRasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
						olRasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
						olRasterizer.depthBiasEnable = VK_TRUE;
						olRasterizer.depthBiasConstantFactor = 1.0f;
						olRasterizer.depthBiasSlopeFactor = 1.0f;
						olRasterizer.depthBiasClamp = 0.0f;

						// No blending for outlines
						VkPipelineColorBlendAttachmentState olBlend{};
						olBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
						                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
						olBlend.blendEnable = VK_FALSE;

						VkPipelineColorBlendStateCreateInfo olColorBlending{};
						olColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
						olColorBlending.logicOpEnable = VK_FALSE;
						olColorBlending.attachmentCount = 1;
						olColorBlending.pAttachments = &olBlend;

						VkGraphicsPipelineCreateInfo olPipelineInfo{};
						olPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
						olPipelineInfo.stageCount = 2;
						olPipelineInfo.pStages = olStages;
						olPipelineInfo.pVertexInputState = &vertexInputInfo;
						olPipelineInfo.pInputAssemblyState = &inputAssembly;
						olPipelineInfo.pViewportState = &viewportState;
						olPipelineInfo.pRasterizationState = &olRasterizer;
						olPipelineInfo.pMultisampleState = &multisampling;
						olPipelineInfo.pDepthStencilState = &depthStencil;
						olPipelineInfo.pColorBlendState = &olColorBlending;
						olPipelineInfo.pDynamicState = &dynamicState;
						olPipelineInfo.layout = sharedPipeline.pipelineLayout;
						olPipelineInfo.renderPass = renderPass;
						olPipelineInfo.subpass = 0;

						result = vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1, &olPipelineInfo, nullptr, &sharedPipeline.outlinesPipeline);

						vkDestroyShaderModule(vkDevice, olVertModule, nullptr);
						vkDestroyShaderModule(vkDevice, olFragModule, nullptr);

						if (result != VK_SUCCESS) {
							SPLog("Warning: Failed to create model outline pipeline (error code: %d)", result);
							sharedPipeline.outlinesPipeline = VK_NULL_HANDLE;
						} else {
							SPLog("Created shared model outline pipeline");
						}
					}
				}
			}
		}

	} // namespace draw
} // namespace spades
