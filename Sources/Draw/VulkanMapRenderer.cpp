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

#include "VulkanMapRenderer.h"
#include "VulkanMapChunk.h"
#include "VulkanRenderer.h"
#include "VulkanBuffer.h"
#include "VulkanImage.h"
#include <Gui/SDLVulkanDevice.h>
#include <Client/GameMap.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/Settings.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <array>
#include <vector>

namespace spades {
	namespace draw {

		VulkanMapRenderer::VulkanMapRenderer(client::GameMap* map, VulkanRenderer& r)
		    : renderer(r),
		      device(static_cast<gui::SDLVulkanDevice*>(r.GetDevice().Unmanage())),
		      gameMap(map),
		      chunks(nullptr),
		      chunkInfos(nullptr),
		      depthonlyPipeline(VK_NULL_HANDLE),
		      basicPipeline(VK_NULL_HANDLE),
		      dlightPipeline(VK_NULL_HANDLE),
		      backfacePipeline(VK_NULL_HANDLE),
		      outlinesPipeline(VK_NULL_HANDLE),
		      pipelineLayout(VK_NULL_HANDLE),
		      descriptorSetLayout(VK_NULL_HANDLE),
		      descriptorPool(VK_NULL_HANDLE),
		      textureDescriptorSet(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			SPLog("Initializing Vulkan map renderer");

			int w = map->Width();
			int h = map->Height();
			int d = map->Depth();

			numChunkWidth = w >> VulkanMapChunk::SizeBits;
			numChunkHeight = h >> VulkanMapChunk::SizeBits;
			numChunkDepth = d >> VulkanMapChunk::SizeBits;

			if ((w & (VulkanMapChunk::Size - 1)) != 0)
				numChunkWidth++;
			if ((h & (VulkanMapChunk::Size - 1)) != 0)
				numChunkHeight++;
			if ((d & (VulkanMapChunk::Size - 1)) != 0)
				numChunkDepth++;

			numChunks = numChunkWidth * numChunkHeight * numChunkDepth;

			SPLog("Chunk count: %d (%d x %d x %d)", numChunks, numChunkWidth, numChunkHeight,
			      numChunkDepth);

			chunks = new VulkanMapChunk*[numChunks];
			chunkInfos = new ChunkRenderInfo[numChunks];

			for (int i = 0; i < numChunks; i++) {
				chunks[i] = nullptr;
				chunkInfos[i].rendered = false;
				chunkInfos[i].distance = 0.0f;
			}

			// Create chunks
			for (int cx = 0; cx < numChunkWidth; cx++) {
				for (int cy = 0; cy < numChunkHeight; cy++) {
					for (int cz = 0; cz < numChunkDepth; cz++) {
						int idx = GetChunkIndex(cx, cy, cz);
						chunks[idx] = new VulkanMapChunk(*this, map, cx, cy, cz);
					}
				}
			}

			SPLog("Vulkan map renderer initialized");
		}

		VulkanMapRenderer::~VulkanMapRenderer() {
			SPADES_MARK_FUNCTION();

			if (chunks) {
				for (int i = 0; i < numChunks; i++) {
					if (chunks[i]) {
						delete chunks[i];
					}
				}
				delete[] chunks;
			}

			if (chunkInfos) {
				delete[] chunkInfos;
			}

			DestroyPipelines();
		}

		void VulkanMapRenderer::PreloadShaders(VulkanRenderer& r) {
			SPADES_MARK_FUNCTION();
			// TODO: Preload shaders when shader system is implemented
		}

		void VulkanMapRenderer::GameMapChanged(int x, int y, int z, client::GameMap* map) {
			SPADES_MARK_FUNCTION();

			if (map != gameMap)
				return;

			int cx = x >> VulkanMapChunk::SizeBits;
			int cy = y >> VulkanMapChunk::SizeBits;
			int cz = z >> VulkanMapChunk::SizeBits;

			for (int dx = -1; dx <= 1; dx++) {
				for (int dy = -1; dy <= 1; dy++) {
					for (int dz = -1; dz <= 1; dz++) {
						int xx = (cx + dx) & (numChunkWidth - 1);
						int yy = (cy + dy) & (numChunkHeight - 1);
						int zz = cz + dz;

						if (zz < 0 || zz >= numChunkDepth)
							continue;

						VulkanMapChunk* chunk = GetChunk(xx, yy, zz);
						if (chunk) {
							chunk->SetNeedsUpdate();
						}
					}
				}
			}
		}

		void VulkanMapRenderer::Realize() {
			SPADES_MARK_FUNCTION();
			// Realize chunks based on view origin
			Vector3 viewOrigin = renderer.GetSceneDef().viewOrigin;
			RealizeChunks(viewOrigin);
		}

		void VulkanMapRenderer::RealizeChunks(Vector3 eye) {
			SPADES_MARK_FUNCTION();

			float cullDistance = 128.0F;
			float releaseDistance = cullDistance + 32.0F;

			// Calculate distance and realize/unrealize chunks based on distance
			for (int i = 0; i < numChunks; i++) {
				VulkanMapChunk* chunk = chunks[i];
				if (chunk) {
					float dist = chunk->DistanceFromEye(eye);
					chunkInfos[i].distance = dist;

					// Frustum culling via distance-based LOD
					if (dist < cullDistance) {
						chunk->SetRealized(true);
					} else if (dist > releaseDistance) {
						chunk->SetRealized(false);
					}
				}
			}

			// Update realized chunks that need updates (must be done outside render passes)
			for (int i = 0; i < numChunks; i++) {
				VulkanMapChunk* chunk = chunks[i];
				if (chunk && chunk->IsRealized()) {
					chunk->UpdateIfNeeded();
				}
			}
		}

		void VulkanMapRenderer::Prerender() {
			SPADES_MARK_FUNCTION();
			// Depth-only prerender pass is now handled via RenderDepthPass
			// This method can be used for other preprocessing if needed
		}

		void VulkanMapRenderer::RenderSunlightPass(VkCommandBuffer commandBuffer) {
			SPADES_MARK_FUNCTION();

			if (basicPipeline == VK_NULL_HANDLE) {
				SPLog("Warning: Map pipeline not initialized - map will not render");
				return;
			}

			Vector3 viewOrigin = renderer.GetSceneDef().viewOrigin;
			IntVector3 c = viewOrigin.Floor();
			c.x >>= VulkanMapChunk::SizeBits;
			c.y >>= VulkanMapChunk::SizeBits;
			c.z >>= VulkanMapChunk::SizeBits;

			// Bind the basic pipeline
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, basicPipeline);

			// Draw from nearest to farthest for optimal depth testing
			// Include all vertical chunks
			for (int cz = 0; cz < numChunkDepth; cz++) {
				DrawColumnSunlight(commandBuffer, c.x, c.y, cz, viewOrigin);
			}

			// Draw in a spiral pattern outward from the camera
			for (int dist = 1; dist <= 128 / VulkanMapChunk::Size; dist++) {
				for (int x = c.x - dist; x <= c.x + dist; x++) {
					for (int cz = 0; cz < numChunkDepth; cz++) {
						DrawColumnSunlight(commandBuffer, x, c.y + dist, cz, viewOrigin);
						DrawColumnSunlight(commandBuffer, x, c.y - dist, cz, viewOrigin);
					}
				}
				for (int y = c.y - dist + 1; y <= c.y + dist - 1; y++) {
					for (int cz = 0; cz < numChunkDepth; cz++) {
						DrawColumnSunlight(commandBuffer, c.x + dist, y, cz, viewOrigin);
						DrawColumnSunlight(commandBuffer, c.x - dist, y, cz, viewOrigin);
					}
				}
			}
		}

		void VulkanMapRenderer::RenderDynamicLightPass(VkCommandBuffer commandBuffer,
		                                               std::vector<void*> lights) {
			SPADES_MARK_FUNCTION();

			if (lights.empty())
				return;

			Vector3 viewOrigin = renderer.GetSceneDef().viewOrigin;
			IntVector3 c = viewOrigin.Floor();
			c.x >>= VulkanMapChunk::SizeBits;
			c.y >>= VulkanMapChunk::SizeBits;
			c.z >>= VulkanMapChunk::SizeBits;

			// Draw from nearest to farthest
			// Include all vertical chunks
			for (int cz = 0; cz < numChunkDepth; cz++) {
				DrawColumnDynamicLight(commandBuffer, c.x, c.y, cz, viewOrigin, lights);
			}

			// Draw in a spiral pattern outward from the camera
			for (int dist = 1; dist <= 128 / VulkanMapChunk::Size; dist++) {
				for (int x = c.x - dist; x <= c.x + dist; x++) {
					for (int cz = 0; cz < numChunkDepth; cz++) {
						DrawColumnDynamicLight(commandBuffer, x, c.y + dist, cz, viewOrigin, lights);
						DrawColumnDynamicLight(commandBuffer, x, c.y - dist, cz, viewOrigin, lights);
					}
				}
				for (int y = c.y - dist + 1; y <= c.y + dist - 1; y++) {
					for (int cz = 0; cz < numChunkDepth; cz++) {
						DrawColumnDynamicLight(commandBuffer, c.x + dist, y, cz, viewOrigin, lights);
						DrawColumnDynamicLight(commandBuffer, c.x - dist, y, cz, viewOrigin, lights);
					}
				}
			}
		}

		void VulkanMapRenderer::RenderOutlinePass(VkCommandBuffer commandBuffer) {
			SPADES_MARK_FUNCTION();

			Vector3 viewOrigin = renderer.GetSceneDef().viewOrigin;
			IntVector3 c = viewOrigin.Floor();
			c.x >>= VulkanMapChunk::SizeBits;
			c.y >>= VulkanMapChunk::SizeBits;
			c.z >>= VulkanMapChunk::SizeBits;

			// Draw from nearest to farthest
			// Include all vertical chunks
			for (int cz = 0; cz < numChunkDepth; cz++) {
				DrawColumnOutline(commandBuffer, c.x, c.y, cz, viewOrigin);
			}

			// Draw in a spiral pattern outward from the camera
			for (int dist = 1; dist <= 128 / VulkanMapChunk::Size; dist++) {
				for (int x = c.x - dist; x <= c.x + dist; x++) {
					for (int cz = 0; cz < numChunkDepth; cz++) {
						DrawColumnOutline(commandBuffer, x, c.y + dist, cz, viewOrigin);
						DrawColumnOutline(commandBuffer, x, c.y - dist, cz, viewOrigin);
					}
				}
				for (int y = c.y - dist + 1; y <= c.y + dist - 1; y++) {
					for (int cz = 0; cz < numChunkDepth; cz++) {
						DrawColumnOutline(commandBuffer, c.x + dist, y, cz, viewOrigin);
						DrawColumnOutline(commandBuffer, c.x - dist, y, cz, viewOrigin);
					}
				}
			}
		}

		void VulkanMapRenderer::RenderDepthPass(VkCommandBuffer commandBuffer) {
			SPADES_MARK_FUNCTION();

			// Depth-only pass for shadow mapping
			// This renders all visible chunks to the depth buffer only

			Vector3 viewOrigin = renderer.GetSceneDef().viewOrigin;
			IntVector3 c = viewOrigin.Floor();
			c.x >>= VulkanMapChunk::SizeBits;
			c.y >>= VulkanMapChunk::SizeBits;
			c.z >>= VulkanMapChunk::SizeBits;

			// Draw from nearest to farthest for optimal depth testing
			// Include all vertical chunks
			for (int cz = 0; cz < numChunkDepth; cz++) {
				DrawColumnDepth(commandBuffer, c.x, c.y, cz, viewOrigin);
			}

			// Draw in a spiral pattern outward from the camera
			for (int dist = 1; dist <= 128 / VulkanMapChunk::Size; dist++) {
				for (int x = c.x - dist; x <= c.x + dist; x++) {
					for (int cz = 0; cz < numChunkDepth; cz++) {
						DrawColumnDepth(commandBuffer, x, c.y + dist, cz, viewOrigin);
						DrawColumnDepth(commandBuffer, x, c.y - dist, cz, viewOrigin);
					}
				}
				for (int y = c.y - dist + 1; y <= c.y + dist - 1; y++) {
					for (int cz = 0; cz < numChunkDepth; cz++) {
						DrawColumnDepth(commandBuffer, c.x + dist, y, cz, viewOrigin);
						DrawColumnDepth(commandBuffer, c.x - dist, y, cz, viewOrigin);
					}
				}
			}
		}

		void VulkanMapRenderer::RenderShadowMapPass(VkCommandBuffer commandBuffer, VkPipelineLayout shadowPipelineLayout) {
			SPADES_MARK_FUNCTION();

			// Render all visible chunks for shadow mapping
			// This is called from within the shadow map render pass set up by VulkanShadowMapRenderer
			// Simply render all realized chunks
			for (int i = 0; i < numChunks; i++) {
				VulkanMapChunk* chunk = chunks[i];
				if (chunk && chunk->IsRealized()) {
					chunk->RenderShadowMapPass(commandBuffer, shadowPipelineLayout);
				}
			}
		}

		void VulkanMapRenderer::DrawColumnDepth(VkCommandBuffer commandBuffer, int cx, int cy, int cz,
		                                        Vector3 eye) {
			SPADES_MARK_FUNCTION();

			cx &= numChunkWidth - 1;
			cy &= numChunkHeight - 1;
			if (cz < 0 || cz >= numChunkDepth)
				return;

			VulkanMapChunk* chunk = GetChunk(cx, cy, cz);
			if (chunk && chunk->IsRealized()) {
				chunk->RenderDepthPass(commandBuffer);
			}
		}

		void VulkanMapRenderer::DrawColumnSunlight(VkCommandBuffer commandBuffer, int cx, int cy, int cz,
		                                           Vector3 eye) {
			SPADES_MARK_FUNCTION();

			cx &= numChunkWidth - 1;
			cy &= numChunkHeight - 1;
			if (cz < 0 || cz >= numChunkDepth)
				return;

			VulkanMapChunk* chunk = GetChunk(cx, cy, cz);
			if (chunk && chunk->IsRealized()) {
				chunk->RenderSunlightPass(commandBuffer);
			}
		}

		void VulkanMapRenderer::DrawColumnDynamicLight(VkCommandBuffer commandBuffer, int cx, int cy,
		                                               int cz, Vector3 eye,
		                                               const std::vector<void*>& lights) {
			SPADES_MARK_FUNCTION();

			cx &= numChunkWidth - 1;
			cy &= numChunkHeight - 1;
			if (cz < 0 || cz >= numChunkDepth)
				return;

			VulkanMapChunk* chunk = GetChunk(cx, cy, cz);
			if (chunk && chunk->IsRealized()) {
				chunk->RenderDynamicLightPass(commandBuffer, lights);
			}
		}

		void VulkanMapRenderer::DrawColumnOutline(VkCommandBuffer commandBuffer, int cx, int cy, int cz,
		                                          Vector3 eye) {
			SPADES_MARK_FUNCTION();

			cx &= numChunkWidth - 1;
			cy &= numChunkHeight - 1;
			if (cz < 0 || cz >= numChunkDepth)
				return;

			VulkanMapChunk* chunk = GetChunk(cx, cy, cz);
			if (chunk && chunk->IsRealized()) {
				chunk->RenderOutlinePass(commandBuffer);
			}
		}

		void VulkanMapRenderer::RenderBackface(VkCommandBuffer commandBuffer) {
			SPADES_MARK_FUNCTION();

			// Backface rendering is useful for water reflections and special effects
			// Render map geometry with reversed front face (backface culling off or CW)

			Vector3 viewOrigin = renderer.GetSceneDef().viewOrigin;
			IntVector3 c = viewOrigin.Floor();
			c.x >>= VulkanMapChunk::SizeBits;
			c.y >>= VulkanMapChunk::SizeBits;
			c.z >>= VulkanMapChunk::SizeBits;

			// For backfaces, draw from farthest to nearest (reverse order)
			// Draw in a reverse spiral pattern
			for (int dist = 128 / VulkanMapChunk::Size; dist >= 1; dist--) {
				for (int x = c.x - dist; x <= c.x + dist; x++) {
					for (int cz = 0; cz < numChunkDepth; cz++) {
						DrawColumnSunlight(commandBuffer, x, c.y + dist, cz, viewOrigin);
						DrawColumnSunlight(commandBuffer, x, c.y - dist, cz, viewOrigin);
					}
				}
				for (int y = c.y - dist + 1; y <= c.y + dist - 1; y++) {
					for (int cz = 0; cz < numChunkDepth; cz++) {
						DrawColumnSunlight(commandBuffer, c.x + dist, y, cz, viewOrigin);
						DrawColumnSunlight(commandBuffer, c.x - dist, y, cz, viewOrigin);
					}
				}
			}

			for (int cz = 0; cz < numChunkDepth; cz++) {
				DrawColumnSunlight(commandBuffer, c.x, c.y, cz, viewOrigin);
			}
		}

		void VulkanMapRenderer::CreatePipelines(VkRenderPass renderPass) {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

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

			std::vector<uint32_t> vertCode = LoadSPIRVFile("Shaders/BasicMap.vert.spv");
			std::vector<uint32_t> fragCode = LoadSPIRVFile("Shaders/BasicMap.frag.spv");

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

			// Vertex input state - matches VulkanMapChunk::Vertex
			// Vertex layout: uint8 x,y,z,pad + uint16 aoX,aoY + uint8 r,g,b,shading + int8 nx,ny,nz,pad2 + int8 sx,sy,sz,pad3
			// Total size: 4 + 4 + 4 + 4 + 4 = 20 bytes
			VkVertexInputBindingDescription bindingDescription{};
			bindingDescription.binding = 0;
			bindingDescription.stride = 20;
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};

			// Position (location 0) - x, y, z are uint8_t at offset 0
			attributeDescriptions[0].binding = 0;
			attributeDescriptions[0].location = 0;
			attributeDescriptions[0].format = VK_FORMAT_R8G8B8_UINT;
			attributeDescriptions[0].offset = 0;

			// AO coordinates (location 1) - aoX, aoY are uint16_t at offset 4
			attributeDescriptions[1].binding = 0;
			attributeDescriptions[1].location = 1;
			attributeDescriptions[1].format = VK_FORMAT_R16G16_UINT;
			attributeDescriptions[1].offset = 4;

			// Color (location 2) - colorRed, colorGreen, colorBlue are uint8_t at offset 8
			attributeDescriptions[2].binding = 0;
			attributeDescriptions[2].location = 2;
			attributeDescriptions[2].format = VK_FORMAT_R8G8B8_UINT;
			attributeDescriptions[2].offset = 8;

			// Normal (location 3) - nx, ny, nz are int8_t at offset 12
			attributeDescriptions[3].binding = 0;
			attributeDescriptions[3].location = 3;
			attributeDescriptions[3].format = VK_FORMAT_R8G8B8_SINT;
			attributeDescriptions[3].offset = 12;

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
			// Vulkan has Y-down NDC (opposite to OpenGL's Y-up), so CCW in OpenGL = CW in Vulkan
			rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
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
			colorBlendAttachment.blendEnable = VK_FALSE;

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

			// Pipeline layout with push constants (no descriptor sets needed for BasicMap shader)
			VkPushConstantRange pushConstantRange{};
			pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			pushConstantRange.offset = 0;
			// mat4 (64) + vec3 modelOrigin (12) + float fogDistance (4) +
			// vec3 viewOrigin (12) + float pad (4) + vec3 fogColor (12) = 108
			pushConstantRange.size = 108;

			VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
			pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutInfo.setLayoutCount = 0;
			pipelineLayoutInfo.pSetLayouts = nullptr;
			pipelineLayoutInfo.pushConstantRangeCount = 1;
			pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

			result = vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout);
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
			pipelineInfo.layout = pipelineLayout;
			pipelineInfo.renderPass = renderPass;
			pipelineInfo.subpass = 0;

			result = vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1, &pipelineInfo, nullptr, &basicPipeline);

			// Cleanup shader modules
			vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
			vkDestroyShaderModule(vkDevice, fragShaderModule, nullptr);

			if (result != VK_SUCCESS) {
				SPRaise("Failed to create graphics pipeline (error code: %d)", result);
			}

			// No descriptor sets needed for BasicMap shader - it renders voxels with vertex colors

			SPLog("Map renderer pipeline created successfully");
		}

		void VulkanMapRenderer::DestroyPipelines() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			if (depthonlyPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, depthonlyPipeline, nullptr);
				depthonlyPipeline = VK_NULL_HANDLE;
			}

			if (basicPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, basicPipeline, nullptr);
				basicPipeline = VK_NULL_HANDLE;
			}

			if (dlightPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, dlightPipeline, nullptr);
				dlightPipeline = VK_NULL_HANDLE;
			}

			if (backfacePipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, backfacePipeline, nullptr);
				backfacePipeline = VK_NULL_HANDLE;
			}

			if (outlinesPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, outlinesPipeline, nullptr);
				outlinesPipeline = VK_NULL_HANDLE;
			}

			if (pipelineLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, pipelineLayout, nullptr);
				pipelineLayout = VK_NULL_HANDLE;
			}

			if (descriptorSetLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(vkDevice, descriptorSetLayout, nullptr);
				descriptorSetLayout = VK_NULL_HANDLE;
			}

			if (descriptorPool != VK_NULL_HANDLE) {
				vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
				descriptorPool = VK_NULL_HANDLE;
			}
		}

	} // namespace draw
} // namespace spades
