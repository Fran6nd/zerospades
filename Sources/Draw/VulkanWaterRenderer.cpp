#include "VulkanWaterRenderer.h"
#include "VulkanRenderer.h"
#include "VulkanFramebufferManager.h"
#include "VulkanBuffer.h"
#include "VulkanImage.h"
#include "VulkanProgram.h"
#include "VulkanPipeline.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Core/Settings.h>
#include <Core/ConcurrentDispatch.h>
#include <Client/GameMap.h>

SPADES_SETTING(r_water);

namespace spades {
	namespace draw {

		// Minimal wave tank interface (ported from GLWaterRenderer)
		class IWaveTank : public ConcurrentDispatch {
		protected:
			float dt;
			int size, samples;

		private:
			uint32_t* bitmap;

			int Encode8bit(float v) {
				v = (v + 1.0F) * 0.5F * 255.0F;
				v = floorf(v + 0.5F);

				int i = (int)v;
				if (i < 0)
					i = 0;
				if (i > 255)
					i = 255;
				return i;
			}

			uint32_t MakeBitmapPixel(float dx, float dy, float h) {
				float x = dx, y = dy, z = 0.04F;
				float scale = 200.0F;
				x *= scale;
				y *= scale;
				z *= scale;

				uint32_t out;
				out = Encode8bit(z);
				out |= Encode8bit(y) << 8;
				out |= Encode8bit(x) << 16;
				out |= Encode8bit(h * -10.0F) << 24;
				return out;
			}

			void MakeBitmapRow(float* h1, float* h2, float* h3, uint32_t* out) {
				out[0] = MakeBitmapPixel(h2[1] - h2[size - 1], h3[0] - h1[0], h2[0]);
				out[size - 1] = MakeBitmapPixel(h2[0] - h2[size - 2], h3[size - 1] - h1[size - 1], h2[size - 1]);
				for (int x = 1; x < size - 1; x++)
					out[x] = MakeBitmapPixel(h2[x + 1] - h2[x - 1], h3[x] - h1[x], h2[x]);
			}

		public:
			IWaveTank(int size) : size(size) {
				bitmap = new uint32_t[size * size];
				samples = size * size;
			}
			virtual ~IWaveTank() { delete[] bitmap; }
			void SetTimeStep(float dt) { this->dt = dt; }

			int GetSize() const { return size; }

			uint32_t* GetBitmap() const { return bitmap; }

			void MakeBitmap(float* height) {
				MakeBitmapRow(height + (size - 1) * size, height, height + size, bitmap);
				MakeBitmapRow(height + (size - 2) * size, height + (size - 1) * size, height,
							bitmap + (size - 1) * size);
				for (int y = 1; y < size - 1; y++) {
					MakeBitmapRow(height + (y - 1) * size, height + y * size,
									height + (y + 1) * size, bitmap + y * size);
				}
			}
		};

		// For now, only implement StandardWaveTank (FTCS solver) to keep port simple
		class StandardWaveTank : public IWaveTank {
			float* height;
			float* heightFiltered;
			float* velocity;

			template <bool xy> void DoPDELine(float* vy, float* y1, float* y2, float* yy) {
				int pitch = xy ? size : 1;
				for (int i = 0; i < size; i++) {
					float v1 = *y1, v2 = *y2, v = *yy;
					float force = v1 + v2 - (v + v);
					force *= dt * 80.0F;
					*vy += force;

					y1 += pitch;
					y2 += pitch;
					yy += pitch;
					vy += pitch;
				}
			}

			template <bool xy> void Denoise(float* arr) {
				int pitch = xy ? size : 1;
				if ((arr[0] > 0.0F && arr[(size - 1) * pitch] < 0.0F && arr[pitch] < 0.0F) ||
				    (arr[0] < 0.0F && arr[(size - 1) * pitch] > 0.0F && arr[pitch] > 0.0F)) {
					float ttl = (arr[1] + arr[(size - 1) * pitch]) * 0.5F;
					arr[0] = ttl;
				}
				if ((arr[(size - 1) * pitch] > 0.0F && arr[(size - 2) * pitch] < 0.0F &&
				     arr[0] < 0.0F) ||
				    (arr[(size - 1) * pitch] < 0.0F && arr[(size - 2) * pitch] > 0.0F &&
				     arr[0] > 0.0F)) {
					float ttl = (arr[0] + arr[(size - 2) * pitch]) * 0.5F;
					arr[(size - 1) * pitch] = ttl;
				}
				for (int i = 1; i < size - 1; i++) {
					if ((arr[i * pitch] > 0.0F && arr[(i - 1) * pitch] < 0.0F &&
					     arr[(i + 1) * pitch] < 0.0F) ||
					    (arr[i * pitch] < 0.0F && arr[(i - 1) * pitch] > 0.0F &&
					     arr[(i + 1) * pitch] > 0.0F)) {
						float ttl = (arr[(i + 1) * pitch] + arr[(i - 1) * pitch]) * 0.5F;
						arr[i * pitch] = ttl;
					}
				}
			}

		public:
			StandardWaveTank(int size) : IWaveTank(size) {
				height = new float[size * size];
				heightFiltered = new float[size * size];
				velocity = new float[size * size];
				std::fill(height, height + size * size, 0.0F);
				std::fill(velocity, velocity + size * size, 0.0F);
			}

			~StandardWaveTank() {
				delete[] height;
				delete[] heightFiltered;
				delete[] velocity;
			}

			void Run() override {
				// advance time
				for (int i = 0; i < samples; i++)
					height[i] += velocity[i] * dt;

				// solve ddz/dtt = c^2 (ddz/dxx + ddz/dyy)

				// do ddz/dyy
				DoPDELine<false>(velocity, height + (size - 1) * size, height + size, height);
				DoPDELine<false>(velocity + (size - 1) * size, height + (size - 2) * size, height,
					height + (size - 1) * size);
				for (int y = 1; y < size - 1; y++) {
					DoPDELine<false>(velocity + y * size, height + (y - 1) * size,
									height + (y + 1) * size, height + y * size);
				}

				// do ddz/dxx
				DoPDELine<true>(velocity, height + (size - 1), height + 1, height);
				DoPDELine<true>(velocity + (size - 1), height + (size - 2), height, height + (size - 1));
				for (int x = 1; x < size - 1; x++)
					DoPDELine<true>(velocity + x, height + (x - 1), height + (x + 1), height + x);

				// make average 0
				float sum = 0.0F;
				for (int i = 0; i < samples; i++)
					sum += height[i];
				sum /= (float)samples;
				for (int i = 0; i < samples; i++)
					height[i] -= sum;

				// limit energy
				sum = 0.0F;
				for (int i = 0; i < samples; i++) {
					sum += height[i] * height[i];
					sum += velocity[i] * velocity[i];
				}
				sum = sqrtf(sum / (float)samples / 2.0F) * 80.0F;
				if (sum > 1.0F) {
					sum = 1.0F / sum;
					for (int i = 0; i < samples; i++) {
						height[i] *= sum;
						velocity[i] *= sum;
					}
				}

				// denoise
				for (int i = 0; i < size; i++)
					Denoise<true>(height + i);
				for (int i = 0; i < size; i++)
					Denoise<false>(height + i * size);

				// add randomness
				int count = (int)floorf(dt * 600.0F);
				if (count > 400)
					count = 400;

				for (int i = 0; i < count; i++) {
					int ox = SampleRandomInt(0, size - 3);
					int oy = SampleRandomInt(0, size - 3);
					static const float gauss[] = {
						0.225610111284052F, 0.548779777431897F, 0.225610111284052F
					};
					float strength = (SampleRandomFloat() - SampleRandomFloat()) * 0.15F * 100.0F;
					for (int x = 0; x < 3; x++)
					for (int y = 0; y < 3; y++) {
						velocity[(x + ox) + (y + oy) * size] += strength * gauss[x] * gauss[y];
					}
				}

				for (int i = 0; i < samples; i++)
					heightFiltered[i] = height[i]; // * height[i] * 100.0F;

				// build bitmap
				MakeBitmap(heightFiltered);
			}
		};

		VulkanWaterRenderer::VulkanWaterRenderer(VulkanRenderer& r, client::GameMap* map)
	: renderer(r), device(r.GetDevice()), gameMap(map), waterProgram(nullptr),
	  descriptorPool(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();
			SPLog("VulkanWaterRenderer created");

			if (!map)
				return;

			// Create water color image
			w = map->Width();
			h = map->Height();
			updateBitmapPitch = (w + 31) / 32;
			updateBitmap.resize(updateBitmapPitch * h);
			bitmap.resize(w * h);
			std::fill(updateBitmap.begin(), updateBitmap.end(), 0xffffffffUL);
			std::fill(bitmap.begin(), bitmap.end(), 0xffffffffUL);

			textureImage = Handle<VulkanImage>::New(device, w, h, VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			// Create wave tanks (basic single layer by default)
			size_t numLayers = ((int)r_water >= 2) ? 3 : 1;
			for (size_t i = 0; i < numLayers; i++) {
				int size = ((int)r_water >= 3) ? (1 << 8) : (1 << 7);
				StandardWaveTank* tank = new StandardWaveTank(size);
				waveTanksPlaceholder.push_back((void*)tank);
			}

			// Create wave image for single-layer case
			if (!waveTanksPlaceholder.empty()) {
				IWaveTank* tank = (IWaveTank*)waveTanksPlaceholder[0];
				waveImage = Handle<VulkanImage>::New(device, tank->GetSize(), tank->GetSize(), VK_FORMAT_R8G8B8A8_UNORM,
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			}

			// Build mesh
			{
				std::vector<float> vertices;
				std::vector<uint32_t> indices;

				int meshSize = 16;
				if ((int)r_water >= 2)
					meshSize = 128;
				float meshSizeInv = 1.0F / (float)meshSize;
				for (int y = -meshSize; y <= meshSize; y++) {
					for (int x = -meshSize; x <= meshSize; x++) {
						float vx = (float)(x)*meshSizeInv;
						float vy = (float)(y)*meshSizeInv;
						vx *= vx * vx;
						vy *= vy * vy;
						vertices.push_back(vx);
						vertices.push_back(vy);
					}
				}
#define VID(x, y) (((x) + meshSize) + ((y) + meshSize) * (meshSize * 2 + 1))
				for (int x = -meshSize; x < meshSize; x++) {
					for (int y = -meshSize; y < meshSize; y++) {
						indices.push_back(VID(x, y));
						indices.push_back(VID(x + 1, y));
						indices.push_back(VID(x, y + 1));

						indices.push_back(VID(x + 1, y));
						indices.push_back(VID(x + 1, y + 1));
						indices.push_back(VID(x, y + 1));
					}
				}

				if (!vertices.empty()) {
					size_t vbSize = vertices.size() * sizeof(float);
					vertexBuffer = Handle<VulkanBuffer>::New(device, vbSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
						VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
					void* vdata = vertexBuffer->Map();
					memcpy(vdata, vertices.data(), vbSize);
					vertexBuffer->Unmap();
				}

				if (!indices.empty()) {
					size_t ibSize = indices.size() * sizeof(uint32_t);
					indexBuffer = Handle<VulkanBuffer>::New(device, ibSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
						VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
					void* idata = indexBuffer->Map();
					memcpy(idata, indices.data(), ibSize);
					indexBuffer->Unmap();

					numIndices = static_cast<unsigned int>(indices.size());
				}
			}
		}

		VulkanWaterRenderer::~VulkanWaterRenderer() {
			SPADES_MARK_FUNCTION();
			SPLog("VulkanWaterRenderer destroyed");

			// free wave tanks
			for (void* p : waveTanksPlaceholder) {
				IWaveTank* t = (IWaveTank*)p;
				t->Join();
				delete t;
			}
			waveTanksPlaceholder.clear();

			// Destroy images and buffers
			// VulkanImage and VulkanBuffer are ref-counted (Handle) and will be freed automatically

			CleanupDescriptorResources();
		}

		void VulkanWaterRenderer::PreloadShaders(VulkanRenderer& r) {
			SPADES_MARK_FUNCTION();
		SPLog("Preloading Vulkan water shaders");
		// For now, only the basic Water shader is converted (Water2 and Water3 need conversion)
		r.RegisterProgram("Shaders/Water.vk.program");
		}

		void VulkanWaterRenderer::GameMapChanged(int x, int y, int z, client::GameMap* map) {
			SPADES_MARK_FUNCTION();
			if (map != gameMap)
				return;
			if (z < 63)
				return;
			MarkUpdate(x, y);
		}

		void VulkanWaterRenderer::Realize() {
			SPADES_MARK_FUNCTION();
		SPLog("VulkanWaterRenderer::Realize: creating pipeline");

		// Select program variant based on settings
		// For now, use the basic Water shader (Water2 and Water3 need conversion)
		std::string name = "Shaders/Water.vk.program";

		waterProgram = renderer.RegisterProgram(name);
		if (!waterProgram) {
			SPLog("Failed to register water program: %s", name.c_str());
			return;
		}
		if (!waterProgram->IsLinked())
			waterProgram->Link();

		// Configure pipeline: two floats per vertex (vec2), triangle list, alpha blending
		VulkanPipelineConfig cfg;
		VkVertexInputBindingDescription vb{};
		vb.binding = 0;
		vb.stride = sizeof(float) * 2;
		vb.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		cfg.vertexBindings.push_back(vb);

		VkVertexInputAttributeDescription attr{};
		attr.location = 0;
		attr.binding = 0;
		attr.format = VK_FORMAT_R32G32_SFLOAT;
		attr.offset = 0;
		cfg.vertexAttributes.push_back(attr);

		cfg.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		cfg.blendEnable = VK_TRUE;
		cfg.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		cfg.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		cfg.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		cfg.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

		cfg.depthTestEnable = VK_TRUE;
		cfg.depthWriteEnable = VK_FALSE; // transparent water
		cfg.depthCompareOp = VK_COMPARE_OP_LESS;

		// Create descriptor resources
		CreateDescriptorPool();
		CreateUniformBuffers();
		CreateDescriptorSets();

		// Create pipeline using water render pass (with LOAD_OP to preserve scene content)
		VkRenderPass waterRenderPass = renderer.GetFramebufferManager()->GetWaterRenderPass();
		waterPipeline = Handle<VulkanPipeline>::New(device, waterProgram, cfg, waterRenderPass);

		SPLog("VulkanWaterRenderer pipeline and descriptors created");
		}

		void VulkanWaterRenderer::Prerender() {
			SPADES_MARK_FUNCTION();
		}

	void VulkanWaterRenderer::RenderSunlightPass(VkCommandBuffer commandBuffer) {
		SPADES_MARK_FUNCTION();
		if (!waterPipeline || numIndices == 0) return;

		// Get current frame index for proper double/triple buffering
		uint32_t frameIndex = renderer.GetCurrentFrameIndex();

		// Update uniform buffers with current renderer state
		UpdateUniformBuffers(frameIndex);

		// Update descriptor sets with textures and uniform buffers
		VulkanFramebufferManager* fbManager = renderer.GetFramebufferManager();
		if (!fbManager || !textureImage || !waveImage) {
			SPLog("Warning: Missing required resources for water rendering");
			return;
		}

		// Get screen and depth textures from framebuffer manager
		// The water shader samples from the main render target for underwater refraction
		Handle<VulkanImage> screenImage = fbManager->GetColorImage();
		Handle<VulkanImage> depthImage = fbManager->GetDepthImage();

		if (!screenImage || !depthImage) {
			SPLog("Warning: Framebuffer manager textures not available");
			return;
		}

		std::vector<VkWriteDescriptorSet> descriptorWrites;

		// Binding 0: screenTexture (from framebuffer)
		VkDescriptorImageInfo screenImageInfo{};
		screenImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		screenImageInfo.imageView = screenImage->GetImageView();
		screenImageInfo.sampler = screenImage->GetSampler();

		VkWriteDescriptorSet screenTextureWrite{};
		screenTextureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		screenTextureWrite.dstSet = descriptorSets[frameIndex];
		screenTextureWrite.dstBinding = 0;
		screenTextureWrite.dstArrayElement = 0;
		screenTextureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		screenTextureWrite.descriptorCount = 1;
		screenTextureWrite.pImageInfo = &screenImageInfo;
		descriptorWrites.push_back(screenTextureWrite);

		// Binding 1: depthTexture (from framebuffer)
		VkDescriptorImageInfo depthImageInfo{};
		depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		depthImageInfo.imageView = depthImage->GetImageView();
		depthImageInfo.sampler = depthImage->GetSampler();

		VkWriteDescriptorSet depthTextureWrite{};
		depthTextureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		depthTextureWrite.dstSet = descriptorSets[frameIndex];
		depthTextureWrite.dstBinding = 1;
		depthTextureWrite.dstArrayElement = 0;
		depthTextureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		depthTextureWrite.descriptorCount = 1;
		depthTextureWrite.pImageInfo = &depthImageInfo;
		descriptorWrites.push_back(depthTextureWrite);

		// Binding 2: mainTexture (water color)
		VkDescriptorImageInfo mainImageInfo{};
		mainImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		mainImageInfo.imageView = textureImage->GetImageView();
		mainImageInfo.sampler = textureImage->GetSampler();

		VkWriteDescriptorSet mainTextureWrite{};
		mainTextureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		mainTextureWrite.dstSet = descriptorSets[frameIndex];
		mainTextureWrite.dstBinding = 2;
		mainTextureWrite.dstArrayElement = 0;
		mainTextureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		mainTextureWrite.descriptorCount = 1;
		mainTextureWrite.pImageInfo = &mainImageInfo;
		descriptorWrites.push_back(mainTextureWrite);

		// Binding 3: waveTexture
		VkDescriptorImageInfo waveImageInfo{};
		waveImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		waveImageInfo.imageView = waveImage->GetImageView();
		waveImageInfo.sampler = waveImage->GetSampler();

		VkWriteDescriptorSet waveTextureWrite{};
		waveTextureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		waveTextureWrite.dstSet = descriptorSets[frameIndex];
		waveTextureWrite.dstBinding = 3;
		waveTextureWrite.dstArrayElement = 0;
		waveTextureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		waveTextureWrite.descriptorCount = 1;
		waveTextureWrite.pImageInfo = &waveImageInfo;
		descriptorWrites.push_back(waveTextureWrite);

		// Binding 4: WaterUBO
		VkDescriptorBufferInfo waterUBOInfo{};
		waterUBOInfo.buffer = waterUBOs[frameIndex]->GetBuffer();
		waterUBOInfo.offset = 0;
		waterUBOInfo.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet waterUBOWrite{};
		waterUBOWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		waterUBOWrite.dstSet = descriptorSets[frameIndex];
		waterUBOWrite.dstBinding = 4;
		waterUBOWrite.dstArrayElement = 0;
		waterUBOWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		waterUBOWrite.descriptorCount = 1;
		waterUBOWrite.pBufferInfo = &waterUBOInfo;
		descriptorWrites.push_back(waterUBOWrite);

		// Binding 5: WaterMatricesUBO
		VkDescriptorBufferInfo waterMatricesUBOInfo{};
		waterMatricesUBOInfo.buffer = waterMatricesUBOs[frameIndex]->GetBuffer();
		waterMatricesUBOInfo.offset = 0;
		waterMatricesUBOInfo.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet waterMatricesUBOWrite{};
		waterMatricesUBOWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		waterMatricesUBOWrite.dstSet = descriptorSets[frameIndex];
		waterMatricesUBOWrite.dstBinding = 5;
		waterMatricesUBOWrite.dstArrayElement = 0;
		waterMatricesUBOWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		waterMatricesUBOWrite.descriptorCount = 1;
		waterMatricesUBOWrite.pBufferInfo = &waterMatricesUBOInfo;
		descriptorWrites.push_back(waterMatricesUBOWrite);

		// Binding 6: mirrorTexture (for reflections)
		Handle<VulkanImage> mirrorColorImage = fbManager->GetMirrorColorImage();
		if (mirrorColorImage) {
			VkDescriptorImageInfo mirrorColorInfo{};
			mirrorColorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			mirrorColorInfo.imageView = mirrorColorImage->GetImageView();
			mirrorColorInfo.sampler = mirrorColorImage->GetSampler();

			VkWriteDescriptorSet mirrorColorWrite{};
			mirrorColorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			mirrorColorWrite.dstSet = descriptorSets[frameIndex];
			mirrorColorWrite.dstBinding = 6;
			mirrorColorWrite.dstArrayElement = 0;
			mirrorColorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			mirrorColorWrite.descriptorCount = 1;
			mirrorColorWrite.pImageInfo = &mirrorColorInfo;
			descriptorWrites.push_back(mirrorColorWrite);
		}

		// Binding 7: mirrorDepthTexture (for depth-aware reflections)
		Handle<VulkanImage> mirrorDepthImage = fbManager->GetMirrorDepthImage();
		if (mirrorDepthImage) {
			VkDescriptorImageInfo mirrorDepthInfo{};
			mirrorDepthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			mirrorDepthInfo.imageView = mirrorDepthImage->GetImageView();
			mirrorDepthInfo.sampler = mirrorDepthImage->GetSampler();

			VkWriteDescriptorSet mirrorDepthWrite{};
			mirrorDepthWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			mirrorDepthWrite.dstSet = descriptorSets[frameIndex];
			mirrorDepthWrite.dstBinding = 7;
			mirrorDepthWrite.dstArrayElement = 0;
			mirrorDepthWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			mirrorDepthWrite.descriptorCount = 1;
			mirrorDepthWrite.pImageInfo = &mirrorDepthInfo;
			descriptorWrites.push_back(mirrorDepthWrite);
		}

		// Update descriptor sets
		vkUpdateDescriptorSets(device->GetDevice(), static_cast<uint32_t>(descriptorWrites.size()),
		                      descriptorWrites.data(), 0, nullptr);

		// Bind pipeline
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, waterPipeline->GetPipeline());

		// Dynamic viewport/scissor
		VkExtent2D extent = device->GetSwapchainExtent();
		VkViewport vp{};
		vp.x = 0.0f;
		vp.y = (float)extent.height;
		vp.width = (float)extent.width;
		vp.height = -(float)extent.height;
		vp.minDepth = 0.0f;
		vp.maxDepth = 1.0f;
		vkCmdSetViewport(commandBuffer, 0, 1, &vp);

		VkRect2D scissor{};
		scissor.offset = {0, 0};
		scissor.extent = extent;
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

		// Bind descriptor sets
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		                       waterProgram->GetPipelineLayout(), 0, 1,
		                       &descriptorSets[frameIndex], 0, nullptr);

		// Bind vertex/index buffers
		VkDeviceSize offset = 0;
		VkBuffer vbuf = vertexBuffer->GetBuffer();
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vbuf, &offset);
		vkCmdBindIndexBuffer(commandBuffer, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

		// Draw
		vkCmdDrawIndexed(commandBuffer, numIndices, 1, 0, 0, 0);
	}

		void VulkanWaterRenderer::RenderDynamicLightPass(VkCommandBuffer commandBuffer, std::vector<void*> lights) {
			SPADES_MARK_FUNCTION();

			if (lights.empty() || numIndices == 0)
				return;

			// Bind pipeline for dynamic lights
			// Note: This would use a specialized water dynamic light pipeline
			// For now, using the sunlight pipeline as placeholder until shaders are ready
			if (!waterPipeline)
				return;

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, waterPipeline->GetPipeline());

			// Set viewport and scissor
			VkExtent2D extent = device->GetSwapchainExtent();
			VkViewport vp{};
			vp.x = 0.0f;
			vp.y = (float)extent.height;
			vp.width = (float)extent.width;
			vp.height = -(float)extent.height;
			vp.minDepth = 0.0f;
			vp.maxDepth = 1.0f;
			vkCmdSetViewport(commandBuffer, 0, 1, &vp);

			VkRect2D scissor{};
			scissor.offset = {0, 0};
			scissor.extent = extent;
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			// Bind descriptor sets for textures and light uniforms

			// Bind vertex/index buffers
			VkDeviceSize offset = 0;
			VkBuffer vbuf = vertexBuffer->GetBuffer();
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vbuf, &offset);
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

			// Draw water with dynamic lights
			vkCmdDrawIndexed(commandBuffer, numIndices, 1, 0, 0, 0);
		}

	void VulkanWaterRenderer::MarkUpdate(int x, int y) {
			x &= w - 1;
			y &= h - 1;
			updateBitmap[(x >> 5) + y * updateBitmapPitch] |= 1UL << (x & 31);
		}

		void VulkanWaterRenderer::Update(float dt) {
			SPADES_MARK_FUNCTION();
			// Wait for simulations and upload wave/texture data
			for (size_t i = 0; i < waveTanksPlaceholder.size(); i++) {
				IWaveTank* t = (IWaveTank*)waveTanksPlaceholder[i];
				// wait for previous run
				t->Join();
			}

			// Upload wave data for first layer as example
			if (!waveTanksPlaceholder.empty()) {
				IWaveTank* t = (IWaveTank*)waveTanksPlaceholder[0];
				// Create staging buffer
				size_t bmpSize = t->GetSize() * t->GetSize() * sizeof(uint32_t);
				Handle<VulkanBuffer> staging = Handle<VulkanBuffer>::New(
					device, bmpSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				void* data = staging->Map();
				memcpy(data, t->GetBitmap(), bmpSize);
				staging->Unmap();

				// One-time command buffer copy
				VkCommandBufferAllocateInfo allocInfo{};
				allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
				allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
				allocInfo.commandPool = device->GetCommandPool();
				allocInfo.commandBufferCount = 1;

				VkCommandBuffer commandBuffer;
				if (vkAllocateCommandBuffers(device->GetDevice(), &allocInfo, &commandBuffer) != VK_SUCCESS) {
					SPRaise("Failed to allocate command buffer for wave texture update");
				}

				VkCommandBufferBeginInfo beginInfo{};
				beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
				if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
					SPRaise("Failed to begin command buffer for wave texture update");
				}

				// Transition waveImage to TRANSFER_DST_OPTIMAL
				waveImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					0, VK_ACCESS_TRANSFER_WRITE_BIT,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

				// Copy buffer to image (full image)
				// Note: VulkanImage::CopyFromBuffer expects the buffer to contain tightly packed pixels
				waveImage->CopyFromBuffer(commandBuffer, staging->GetBuffer());

				// Transition waveImage to SHADER_READ_ONLY_OPTIMAL
				waveImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

				vkEndCommandBuffer(commandBuffer);

				VkSubmitInfo submitInfo{};
				submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				submitInfo.commandBufferCount = 1;
				submitInfo.pCommandBuffers = &commandBuffer;

				vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
				vkQueueWaitIdle(device->GetGraphicsQueue());

				vkFreeCommandBuffers(device->GetDevice(), device->GetCommandPool(), 1, &commandBuffer);
			}

			// Update water color texture from map (full update for now)
			{
				bool fullUpdate = true;
				for (size_t i = 0; i < updateBitmap.size(); i++) {
					if (updateBitmap[i] == 0) {
						fullUpdate = false;
						break;
					}
				}

				if (fullUpdate) {
					uint32_t* pixels = bitmap.data();
					bool modified = false;
					int x = 0, y = 0;
					for (int i = w * h; i > 0; i--) {
						uint32_t col = gameMap->GetColor(x, y, 63);

						x++;
						if (x == w) {
							x = 0;
							y++;
						}

						// Linearize color as GL does
						int r = (uint8_t)(col);
						int g = (uint8_t)(col >> 8);
						int b = (uint8_t)(col >> 16);
						r = (r * r + 128) >> 8;
						g = (g * g + 128) >> 8;
						b = (b * b + 128) >> 8;
						uint32_t lin = b | (g << 8) | (r << 16);

						if (*pixels != lin)
							modified = true;
						else {
							pixels++;
							continue;
						}
						*(pixels++) = lin;
					}

					if (modified) {
						// Upload full bitmap via staging buffer
						size_t bmpSize = w * h * sizeof(uint32_t);
						Handle<VulkanBuffer> staging = Handle<VulkanBuffer>::New(
							device, bmpSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
							VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
						void* data = staging->Map();
						memcpy(data, bitmap.data(), bmpSize);
						staging->Unmap();

						VkCommandBufferAllocateInfo allocInfo{};
						allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
						allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
						allocInfo.commandPool = device->GetCommandPool();
						allocInfo.commandBufferCount = 1;

						VkCommandBuffer commandBuffer;
						if (vkAllocateCommandBuffers(device->GetDevice(), &allocInfo, &commandBuffer) != VK_SUCCESS) {
							SPRaise("Failed to allocate command buffer for water texture update");
						}

						VkCommandBufferBeginInfo beginInfo{};
						beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
						beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
						if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
							SPRaise("Failed to begin command buffer for water texture update");
						}

						textureImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
							0, VK_ACCESS_TRANSFER_WRITE_BIT,
							VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

						textureImage->CopyFromBuffer(commandBuffer, staging->GetBuffer());

						textureImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
							VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
							VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

						vkEndCommandBuffer(commandBuffer);

						VkSubmitInfo submitInfo{};
						submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
						submitInfo.commandBufferCount = 1;
						submitInfo.pCommandBuffers = &commandBuffer;

						vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
						vkQueueWaitIdle(device->GetGraphicsQueue());

						vkFreeCommandBuffers(device->GetDevice(), device->GetCommandPool(), 1, &commandBuffer);
					}

					for (size_t i = 0; i < updateBitmap.size(); i++)
						updateBitmap[i] = 0;
				} else {
					// partial updates omitted for now
					for (size_t i = 0; i < updateBitmap.size(); i++)
						updateBitmap[i] = 0;
				}
			}
		}


	void VulkanWaterRenderer::CreateDescriptorPool() {
		// Create descriptor pool for water rendering (3 frames in flight)
		uint32_t frameCount = 3;
		std::vector<VkDescriptorPoolSize> poolSizes(2);
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[0].descriptorCount = 4 * frameCount; // 4 samplers per frame
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[1].descriptorCount = 2 * frameCount; // 2 UBOs per frame

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.maxSets = frameCount;

		if (vkCreateDescriptorPool(device->GetDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
			SPRaise("Failed to create water descriptor pool");
		}
	}

	void VulkanWaterRenderer::CreateUniformBuffers() {
		// Create uniform buffers for each frame in flight
		uint32_t frameCount = 3;
		waterUBOs.resize(frameCount);
		waterMatricesUBOs.resize(frameCount);

		// WaterUBO size: 4*4 + 4*4 + 2*4 + 2*4 + 4*4 + 4*4 + 4*4 + 2*4 + 2*4 = 108 bytes -> pad to 112
		size_t waterUBOSize = 112;
		// WaterMatricesUBO size: 3*4*16 + 4*4 + 4 + 3*4 = 212 bytes -> pad to 224
		size_t waterMatricesUBOSize = 224;

		for (uint32_t i = 0; i < frameCount; i++) {
			waterUBOs[i] = Handle<VulkanBuffer>::New(device, waterUBOSize,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			waterMatricesUBOs[i] = Handle<VulkanBuffer>::New(device, waterMatricesUBOSize,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		}
	}

	void VulkanWaterRenderer::CreateDescriptorSets() {
		// Allocate descriptor sets (one per frame)
		uint32_t frameCount = 3;
		descriptorSets.resize(frameCount);

		VkDescriptorSetLayout descriptorSetLayout = waterProgram->GetDescriptorSetLayout();
		std::vector<VkDescriptorSetLayout> layouts(frameCount, descriptorSetLayout);
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = frameCount;
		allocInfo.pSetLayouts = layouts.data();

		if (vkAllocateDescriptorSets(device->GetDevice(), &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
			SPRaise("Failed to allocate water descriptor sets");
		}

		// Note: Descriptor set contents will be updated in UpdateUniformBuffers()
		// when we have valid texture handles
	}

	void VulkanWaterRenderer::UpdateUniformBuffers(uint32_t frameIndex) {
		const client::SceneDefinition& sceneDef = renderer.GetSceneDef();
		float fogDist = renderer.GetFogDistance();
		Vector3 fogCol = renderer.GetFogColorForSolidPass();
		fogCol *= fogCol; // linearize

		Vector3 skyCol = renderer.GetFogColor();
		skyCol *= skyCol; // linearize

		float waterLevel = 63.0f;
		float waterRange = 128.0f;

		// Build model matrix (translate to view origin, scale by water range)
		Matrix4 modelMatrix = Matrix4::Translate(sceneDef.viewOrigin.x, sceneDef.viewOrigin.y, waterLevel);
		modelMatrix = modelMatrix * Matrix4::Scale(waterRange, waterRange, 1.0f);

		Matrix4 viewMatrix = renderer.GetViewMatrix();
		Matrix4 projectionViewMatrix = renderer.GetProjectionViewMatrix();

		// Update WaterMatricesUBO (binding 5)
		struct WaterMatricesUBO {
			Matrix4 projectionViewModelMatrix;
			Matrix4 modelMatrix;
			Matrix4 viewModelMatrix;
			Vector4 viewOriginVector;
			float fogDistance;
			float _pad0[3];
		} waterMatricesData;

		waterMatricesData.projectionViewModelMatrix = projectionViewMatrix * modelMatrix;
		waterMatricesData.modelMatrix = modelMatrix;
		waterMatricesData.viewModelMatrix = viewMatrix * modelMatrix;
		waterMatricesData.viewOriginVector = MakeVector4(sceneDef.viewOrigin.x, sceneDef.viewOrigin.y, sceneDef.viewOrigin.z, 0.0f);
		waterMatricesData.fogDistance = fogDist;

		void* data = waterMatricesUBOs[frameIndex]->Map();
		memcpy(data, &waterMatricesData, sizeof(waterMatricesData));
		waterMatricesUBOs[frameIndex]->Unmap();

		// Update WaterUBO (binding 4)
		struct WaterUBO {
			Vector4 fogColor;
			Vector4 skyColor;
			Vector2 zNearFar;
			Vector2 _pad0;
			Vector4 fovTan;
			Vector4 waterPlane;
			Vector4 viewOriginVector;
			Vector2 displaceScale;
			Vector2 _pad1;
		} waterData;

		waterData.fogColor = MakeVector4(fogCol.x, fogCol.y, fogCol.z, 0.0f);
		waterData.skyColor = MakeVector4(skyCol.x, skyCol.y, skyCol.z, 0.0f);
		waterData.zNearFar = MakeVector2(sceneDef.zNear, sceneDef.zFar);

		waterData.fovTan = MakeVector4(
			tanf(sceneDef.fovX * 0.5f),
			-tanf(sceneDef.fovY * 0.5f),
			-tanf(sceneDef.fovX * 0.5f),
			tanf(sceneDef.fovY * 0.5f)
		);

		// Calculate water plane in view coordinates
		Matrix4 waterModelView = viewMatrix * modelMatrix;
		Vector3 planeNormal = waterModelView.GetAxis(2);
		float planeD = -Vector3::Dot(planeNormal, waterModelView.GetOrigin());
		waterData.waterPlane = MakeVector4(planeNormal.x, planeNormal.y, planeNormal.z, planeD);

		waterData.viewOriginVector = MakeVector4(sceneDef.viewOrigin.x, sceneDef.viewOrigin.y, sceneDef.viewOrigin.z, 0.0f);
		waterData.displaceScale = MakeVector2(1.0f / tanf(sceneDef.fovX * 0.5f), 1.0f / tanf(sceneDef.fovY * 0.5f));

		data = waterUBOs[frameIndex]->Map();
		memcpy(data, &waterData, sizeof(waterData));
		waterUBOs[frameIndex]->Unmap();
	}

	void VulkanWaterRenderer::CleanupDescriptorResources() {
		VkDevice vkDevice = device->GetDevice();

		if (descriptorPool != VK_NULL_HANDLE) {
			vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
			descriptorPool = VK_NULL_HANDLE;
		}

		waterUBOs.clear();
		waterMatricesUBOs.clear();
		descriptorSets.clear();
	}

	} // namespace draw
} // namespace spades
