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
#include <kiss_fft130/kiss_fft.h>
#include <cmath>

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

		// SinCos lookup table for FFT wave tank
		struct SinCosTable {
			float sinCoarse[256];
			float cosCoarse[256];
			float sinFine[256];
			float cosFine[256];

		public:
			SinCosTable() {
				for (int i = 0; i < 256; i++) {
					float ang = (float)i / 256.0F * (M_PI_F * 2.0F);
					sinCoarse[i] = sinf(ang);
					cosCoarse[i] = cosf(ang);

					ang = (float)i / 65536.0F * (M_PI_F * 2.0F);
					sinFine[i] = sinf(ang);
					cosFine[i] = cosf(ang);
				}
			}

			void Compute(unsigned int step, float& outSin, float& outCos) {
				step &= 0xFFFF;
				if (step == 0) {
					outSin = 0;
					outCos = 1.0F;
					return;
				}

				int fine = step & 0xFF;
				int coarse = step >> 8;

				outSin = sinCoarse[coarse];
				outCos = cosCoarse[coarse];

				if (fine != 0) {
					float c = cosFine[fine];
					float s = sinFine[fine];
					float c2 = outCos * c - outSin * s;
					float s2 = outCos * s + outSin * c;
					outCos = c2;
					outSin = s2;
				}
			}
		};

		static SinCosTable sinCosTable;

		// FFT-based wave solver for more realistic waves
		template <int SizeBits> class FFTWaveTank : public IWaveTank {
			enum { Size = 1 << SizeBits, SizeHalf = Size / 2 };
			kiss_fft_cfg fft;

			typedef kiss_fft_cpx Complex;

			struct Cell {
				float magnitude;
				uint32_t phase;
				float phasePerSecond;

				float m00, m01;
				float m10, m11;
			};

			Cell cells[SizeHalf + 1][Size];

			Complex spectrum[SizeHalf + 1][Size];

			Complex temp1[Size];
			Complex temp2[Size];
			Complex temp3[Size][Size];

			float height[Size][Size];

		public:
			FFTWaveTank() : IWaveTank(Size) {
				auto* getRandom = SampleRandomFloat;

				fft = kiss_fft_alloc(Size, 1, NULL, NULL);

				for (int x = 0; x < Size; x++) {
					for (int y = 0; y <= SizeHalf; y++) {
						Cell& cell = cells[y][x];
						if (x == 0 && y == 0) {
							cell.magnitude = 0;
							cell.phasePerSecond = 0.0F;
							cell.phase = 0;
						} else {
							int cx = std::min(x, Size - x);
							float dist = (float)sqrt(cx * cx + y * y);
							float mag = 0.8F / dist / (float)Size;
							mag /= dist;

							float scal = dist / (float)SizeHalf;
							scal *= scal;
							mag *= expf(-scal * 3.0F);

							cell.magnitude = mag;
							cell.phase = static_cast<uint32_t>(SampleRandom());
							cell.phasePerSecond = dist * 1.0E+9F * 128 / Size;
						}

						cell.m00 = getRandom() - getRandom();
						cell.m01 = getRandom() - getRandom();
						cell.m10 = getRandom() - getRandom();
						cell.m11 = getRandom() - getRandom();
					}
				}
			}
			~FFTWaveTank() { kiss_fft_free(fft); }

			void Run() override {
				// advance cells
				for (int x = 0; x < Size; x++) {
					for (int y = 0; y <= SizeHalf; y++) {
						Cell& cell = cells[y][x];
						uint32_t dphase;
						dphase = (uint32_t)(cell.phasePerSecond * dt);
						cell.phase += dphase;

						unsigned int phase = cell.phase >> 16;
						float c, s;
						sinCosTable.Compute(phase, s, c);

						float u, v;
						u = c * cell.m00 + s * cell.m01;
						v = c * cell.m10 + s * cell.m11;

						spectrum[y][x].r = u * cell.magnitude;
						spectrum[y][x].i = v * cell.magnitude;
					}
				}

				// rfft
				for (int y = 0; y <= SizeHalf; y++) {
					for (int x = 0; x < Size; x++)
						temp1[x] = spectrum[y][x];

					kiss_fft(fft, temp1, temp2);

					if (y == 0) {
						for (int x = 0; x < Size; x++)
							temp3[x][0] = temp2[x];
					} else if (y == SizeHalf) {
						for (int x = 0; x < Size; x++) {
							temp3[x][SizeHalf].r = temp2[x].r;
							temp3[x][SizeHalf].i = 0.0F;
						}
					} else {
						for (int x = 0; x < Size; x++) {
							temp3[x][y] = temp2[x];
							temp3[x][Size - y].r = temp2[x].r;
							temp3[x][Size - y].i = -temp2[x].i;
						}
					}
				}
				for (int x = 0; x < Size; x++) {
					kiss_fft(fft, temp3[x], temp2);
					for (int y = 0; y < Size; y++)
						height[x][y] = temp2[y].r;
				}

				MakeBitmap((float*)height);
			}
		};

		// FTCS PDE solver (fallback)
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
	  descriptorPool(VK_NULL_HANDLE), waveStagingBufferSize(0),
	  uploadFence(VK_NULL_HANDLE),
	  occlusionQueryPool(VK_NULL_HANDLE), occlusionQueryActive(false), lastOcclusionResult(1) {
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

			// Create wave tanks using FFT solver for realistic waves
			size_t numLayers = ((int)r_water >= 2) ? 3 : 1;
			for (size_t i = 0; i < numLayers; i++) {
				IWaveTank* tank;
				if ((int)r_water >= 3)
					tank = new FFTWaveTank<8>();
				else
					tank = new FFTWaveTank<7>();
				waveTanksPlaceholder.push_back((void*)tank);
			}

			// Create wave image(s)
			if (!waveTanksPlaceholder.empty()) {
				IWaveTank* tank = (IWaveTank*)waveTanksPlaceholder[0];
				uint32_t size = tank->GetSize();
				uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(size))) + 1;

				if (numLayers == 1) {
					// Single layer - use 2D texture with mipmaps
					waveImage = Handle<VulkanImage>::New(device, size, size, 1, mipLevels,
						VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
						VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				} else {
					// Multiple layers - use 2D array texture with mipmaps
					waveImageArray = Handle<VulkanImage>::New(device, size, size,
						static_cast<uint32_t>(numLayers), mipLevels,
						VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
						VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				}

				// Pre-allocate staging buffers for wave uploads
				waveStagingBufferSize = size * size * sizeof(uint32_t);
				for (size_t i = 0; i < numLayers; i++) {
					waveStagingBufferPool.push_back(Handle<VulkanBuffer>::New(
						device, waveStagingBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
						VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
				}

				// Create single fence for batched async uploads
				VkFenceCreateInfo fenceInfo{};
				fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
				fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
				vkCreateFence(device->GetDevice(), &fenceInfo, nullptr, &uploadFence);
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

			// Destroy fence
			if (uploadFence != VK_NULL_HANDLE) {
				vkDestroyFence(device->GetDevice(), uploadFence, nullptr);
			}

			// Destroy occlusion query pool
			if (occlusionQueryPool != VK_NULL_HANDLE) {
				vkDestroyQueryPool(device->GetDevice(), occlusionQueryPool, nullptr);
			}

			CleanupDescriptorResources();
		}

		void VulkanWaterRenderer::PreloadShaders(VulkanRenderer& r) {
			SPADES_MARK_FUNCTION();
		SPLog("Preloading Vulkan water shaders");
		if ((int)r_water >= 3)
			r.RegisterProgram("Shaders/Water3.vk.program");
		else if ((int)r_water >= 2)
			r.RegisterProgram("Shaders/Water2.vk.program");
		else
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

	void VulkanWaterRenderer::SetGameMap(client::GameMap* map) {
		SPADES_MARK_FUNCTION();

		if (gameMap == map)
			return;

		gameMap = map;

		// If we now have a valid map and textures don't exist, create them
		if (gameMap && !textureImage) {
			SPLog("SetGameMap: Creating water resources for new map");
			w = gameMap->Width();
			h = gameMap->Height();
			updateBitmapPitch = (w + 31) / 32;
			updateBitmap.resize(updateBitmapPitch * h);
			bitmap.resize(w * h);
			std::fill(updateBitmap.begin(), updateBitmap.end(), 0xffffffffUL);
			std::fill(bitmap.begin(), bitmap.end(), 0xffffffffUL);

			textureImage = Handle<VulkanImage>::New(device, w, h, VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			// Create wave tanks using FFT solver for realistic waves
			size_t numLayers = ((int)r_water >= 2) ? 3 : 1;

			// Clear existing wave tanks if any
			for (void* p : waveTanksPlaceholder) {
				IWaveTank* t = (IWaveTank*)p;
				t->Join();
				delete t;
			}
			waveTanksPlaceholder.clear();
			waveStagingBufferPool.clear();

			for (size_t i = 0; i < numLayers; i++) {
				IWaveTank* tank;
				if ((int)r_water >= 3)
					tank = new FFTWaveTank<8>();
				else
					tank = new FFTWaveTank<7>();
				waveTanksPlaceholder.push_back((void*)tank);
			}

			// Create wave image(s)
			if (!waveTanksPlaceholder.empty()) {
				IWaveTank* tank = (IWaveTank*)waveTanksPlaceholder[0];
				uint32_t size = tank->GetSize();
				uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(size))) + 1;

				if (numLayers == 1) {
					// Single layer - use 2D texture with mipmaps
					waveImage = Handle<VulkanImage>::New(device, size, size, 1, mipLevels,
						VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
						VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				} else {
					// Multiple layers - use 2D array texture with mipmaps
					waveImageArray = Handle<VulkanImage>::New(device, size, size,
						static_cast<uint32_t>(numLayers), mipLevels,
						VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
						VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				}

				// Pre-allocate staging buffers for wave uploads
				waveStagingBufferSize = size * size * sizeof(uint32_t);
				for (size_t i = 0; i < numLayers; i++) {
					waveStagingBufferPool.push_back(Handle<VulkanBuffer>::New(
						device, waveStagingBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
						VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
				}

				// Create single fence for batched async uploads if not already created
				if (uploadFence == VK_NULL_HANDLE) {
					VkFenceCreateInfo fenceInfo{};
					fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
					fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
					vkCreateFence(device->GetDevice(), &fenceInfo, nullptr, &uploadFence);
				}
			}

			// Trigger full update of water color texture
			for (size_t i = 0; i < updateBitmap.size(); i++)
				updateBitmap[i] = 0xffffffffUL;
		}
	}

		void VulkanWaterRenderer::Realize() {
			SPADES_MARK_FUNCTION();
		SPLog("VulkanWaterRenderer::Realize: creating pipeline");

		// Create water resources if not already created (in case constructor returned early due to null map)
		if (!textureImage && gameMap) {
			SPLog("Creating water resources (textures, wave images)");
			w = gameMap->Width();
			h = gameMap->Height();
			updateBitmapPitch = (w + 31) / 32;
			updateBitmap.resize(updateBitmapPitch * h);
			bitmap.resize(w * h);
			std::fill(updateBitmap.begin(), updateBitmap.end(), 0xffffffffUL);
			std::fill(bitmap.begin(), bitmap.end(), 0xffffffffUL);

			textureImage = Handle<VulkanImage>::New(device, w, h, VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			// Create wave tanks using FFT solver for realistic waves
			size_t numLayers = ((int)r_water >= 2) ? 3 : 1;
			for (size_t i = 0; i < numLayers; i++) {
				IWaveTank* tank;
				if ((int)r_water >= 3)
					tank = new FFTWaveTank<8>();
				else
					tank = new FFTWaveTank<7>();
				waveTanksPlaceholder.push_back((void*)tank);
			}

			// Create wave image(s)
			if (!waveTanksPlaceholder.empty()) {
				IWaveTank* tank = (IWaveTank*)waveTanksPlaceholder[0];
				uint32_t size = tank->GetSize();
				uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(size))) + 1;

				if (numLayers == 1) {
					// Single layer - use 2D texture with mipmaps
					waveImage = Handle<VulkanImage>::New(device, size, size, 1, mipLevels,
						VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
						VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				} else {
					// Multiple layers - use 2D array texture with mipmaps
					waveImageArray = Handle<VulkanImage>::New(device, size, size,
						static_cast<uint32_t>(numLayers), mipLevels,
						VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
						VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				}

				// Pre-allocate staging buffers for wave uploads
				waveStagingBufferSize = size * size * sizeof(uint32_t);
				for (size_t i = 0; i < numLayers; i++) {
					waveStagingBufferPool.push_back(Handle<VulkanBuffer>::New(
						device, waveStagingBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
						VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
				}

				// Create single fence for batched async uploads
				VkFenceCreateInfo fenceInfo{};
				fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
				fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
				vkCreateFence(device->GetDevice(), &fenceInfo, nullptr, &uploadFence);
			}
		}

		// Build mesh if not already built (in case constructor returned early due to null map)
		if (numIndices == 0) {
			SPLog("Building water mesh");
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
				SPLog("Water mesh built: %d indices", numIndices);
			}
		}

		// Select program variant based on water quality setting (matches GL renderer)
		std::string name;
		if ((int)r_water >= 3)
			name = "Shaders/Water3.vk.program";
		else if ((int)r_water >= 2)
			name = "Shaders/Water2.vk.program";
		else
			name = "Shaders/Water.vk.program";

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
		cfg.frontFace = VK_FRONT_FACE_CLOCKWISE; // Account for negative viewport height (Y-flip)
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
		waterPipeline = Handle<VulkanPipeline>::New(device, waterProgram, cfg, waterRenderPass, renderer.GetPipelineCache());

		// Create occlusion query pool
		VkQueryPoolCreateInfo queryPoolInfo{};
		queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		queryPoolInfo.queryType = VK_QUERY_TYPE_OCCLUSION;
		queryPoolInfo.queryCount = 1;
		if (vkCreateQueryPool(device->GetDevice(), &queryPoolInfo, nullptr, &occlusionQueryPool) != VK_SUCCESS) {
			SPLog("Warning: Failed to create occlusion query pool");
			occlusionQueryPool = VK_NULL_HANDLE;
		}

		SPLog("VulkanWaterRenderer pipeline and descriptors created");
		}

		void VulkanWaterRenderer::Prerender() {
			SPADES_MARK_FUNCTION();
		}

	void VulkanWaterRenderer::RenderSunlightPass(VkCommandBuffer commandBuffer) {
		SPADES_MARK_FUNCTION();

		// Lazy initialization: realize pipeline on first use
		if (!waterPipeline) {
			SPLog("Water pipeline not created yet, calling Realize()");
			Realize();
		}

		SPLog("RenderSunlightPass called: waterPipeline=%p, numIndices=%d", waterPipeline.GetPointerOrNull(), numIndices);
		if (!waterPipeline || numIndices == 0) {
			SPLog("Early return: waterPipeline or numIndices is 0");
			return;
		}

		// Check occlusion query result from previous frame
		if (occlusionQueryPool != VK_NULL_HANDLE && occlusionQueryActive) {
			uint64_t result = 0;
			VkResult queryResult = vkGetQueryPoolResults(device->GetDevice(), occlusionQueryPool,
				0, 1, sizeof(result), &result, sizeof(result),
				VK_QUERY_RESULT_64_BIT);
			if (queryResult == VK_SUCCESS) {
				lastOcclusionResult = result;
				occlusionQueryActive = false;
				SPLog("Occlusion query result: %llu", result);
			}
		}

		// Skip water rendering if no samples passed in previous frame
		SPLog("lastOcclusionResult = %llu", lastOcclusionResult);
		// TEMPORARILY DISABLED for debugging
		// if (lastOcclusionResult == 0) {
		// 	SPLog("Skipping water rendering: occluded (lastOcclusionResult=0)");
		// 	return;
		// }

		// Get current frame index for proper double/triple buffering
		uint32_t frameIndex = renderer.GetCurrentFrameIndex();

		// Update uniform buffers with current renderer state
		UpdateUniformBuffers(frameIndex);

		// Update descriptor sets with textures and uniform buffers
		VulkanFramebufferManager* fbManager = renderer.GetFramebufferManager();
		Handle<VulkanImage> activeWaveImage = waveImageArray ? waveImageArray : waveImage;
		if (!fbManager || !textureImage || !activeWaveImage) {
			SPLog("Warning: Missing required resources - fbManager=%p, textureImage=%p, activeWaveImage=%p",
			      fbManager, textureImage.GetPointerOrNull(), activeWaveImage.GetPointerOrNull());
			return;
		}

		// Get screen copy images for refraction (copy of main scene, not the render target)
		Handle<VulkanImage> screenImage = fbManager->GetScreenCopyColorImage();
		Handle<VulkanImage> depthImage = fbManager->GetScreenCopyDepthImage();

		if (!screenImage || !depthImage) {
			SPLog("Warning: Screen copy textures not available");
			return;
		}

		// Only update dynamic descriptors that change per-frame
		// Static descriptors (bindings 2, 3/8, 5) are pre-bound in CreateDescriptorSets()
		std::vector<VkWriteDescriptorSet> descriptorWrites;
		std::vector<VkDescriptorImageInfo> imageInfos; // Keep alive until vkUpdateDescriptorSets
		imageInfos.reserve(4);

		// Binding 0: screenTexture (from framebuffer) - dynamic
		imageInfos.push_back({});
		imageInfos.back().imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfos.back().imageView = screenImage->GetImageView();
		imageInfos.back().sampler = screenImage->GetSampler();

		VkWriteDescriptorSet screenTextureWrite{};
		screenTextureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		screenTextureWrite.dstSet = descriptorSets[frameIndex];
		screenTextureWrite.dstBinding = 0;
		screenTextureWrite.dstArrayElement = 0;
		screenTextureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		screenTextureWrite.descriptorCount = 1;
		screenTextureWrite.pImageInfo = &imageInfos.back();
		descriptorWrites.push_back(screenTextureWrite);

		// Binding 1: depthTexture (from framebuffer) - dynamic
		imageInfos.push_back({});
		imageInfos.back().imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfos.back().imageView = depthImage->GetImageView();
		imageInfos.back().sampler = depthImage->GetSampler();

		VkWriteDescriptorSet depthTextureWrite{};
		depthTextureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		depthTextureWrite.dstSet = descriptorSets[frameIndex];
		depthTextureWrite.dstBinding = 1;
		depthTextureWrite.dstArrayElement = 0;
		depthTextureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		depthTextureWrite.descriptorCount = 1;
		depthTextureWrite.pImageInfo = &imageInfos.back();
		descriptorWrites.push_back(depthTextureWrite);

		// Bindings 2, 3/8, 5 are pre-bound (static) - no update needed

		// Binding 6: mirrorTexture (for reflections) - dynamic, only for r_water >= 2
		// Binding 7: mirrorDepthTexture (for depth-aware reflections) - dynamic, only for r_water >= 3
		SPLog("r_water = %d", (int)r_water);
		if ((int)r_water >= 2) {
			Handle<VulkanImage> mirrorColorImage = fbManager->GetMirrorColorImage();
			SPLog("mirrorColorImage = %p", mirrorColorImage.GetPointerOrNull());
			if (mirrorColorImage) {
				imageInfos.push_back({});
				imageInfos.back().imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				imageInfos.back().imageView = mirrorColorImage->GetImageView();
				imageInfos.back().sampler = mirrorColorImage->GetSampler();

				VkWriteDescriptorSet mirrorColorWrite{};
				mirrorColorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				mirrorColorWrite.dstSet = descriptorSets[frameIndex];
				mirrorColorWrite.dstBinding = 6;
				mirrorColorWrite.dstArrayElement = 0;
				mirrorColorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				mirrorColorWrite.descriptorCount = 1;
				mirrorColorWrite.pImageInfo = &imageInfos.back();
				descriptorWrites.push_back(mirrorColorWrite);
				SPLog("Bound mirrorColorImage to binding 6");
			} else {
				SPLog("WARNING: mirrorColorImage is null, skipping binding 6");
			}

			if ((int)r_water >= 3) {
				Handle<VulkanImage> mirrorDepthImage = fbManager->GetMirrorDepthImage();
				SPLog("mirrorDepthImage = %p", mirrorDepthImage.GetPointerOrNull());
				if (mirrorDepthImage) {
					imageInfos.push_back({});
					imageInfos.back().imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					imageInfos.back().imageView = mirrorDepthImage->GetImageView();
					imageInfos.back().sampler = mirrorDepthImage->GetSampler();

					VkWriteDescriptorSet mirrorDepthWrite{};
					mirrorDepthWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					mirrorDepthWrite.dstSet = descriptorSets[frameIndex];
					mirrorDepthWrite.dstBinding = 7;
					mirrorDepthWrite.dstArrayElement = 0;
					mirrorDepthWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					mirrorDepthWrite.descriptorCount = 1;
					mirrorDepthWrite.pImageInfo = &imageInfos.back();
					descriptorWrites.push_back(mirrorDepthWrite);
					SPLog("Bound mirrorDepthImage to binding 7");
				} else {
					SPLog("WARNING: mirrorDepthImage is null, skipping binding 7");
				}
			}
		} else {
			SPLog("r_water < 2, skipping mirror texture bindings");
		}

		// Update only dynamic descriptor sets
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

		// Push constants for per-frame water data (replaces WaterUBO)
		vkCmdPushConstants(commandBuffer, waterProgram->GetPipelineLayout(),
		                   VK_SHADER_STAGE_FRAGMENT_BIT, 0,
		                   sizeof(waterPushConstants), &waterPushConstants);

		// Bind vertex/index buffers
		VkDeviceSize offset = 0;
		VkBuffer vbuf = vertexBuffer->GetBuffer();
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vbuf, &offset);
		vkCmdBindIndexBuffer(commandBuffer, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

		// Begin occlusion query
		if (occlusionQueryPool != VK_NULL_HANDLE) {
			vkCmdResetQueryPool(commandBuffer, occlusionQueryPool, 0, 1);
			vkCmdBeginQuery(commandBuffer, occlusionQueryPool, 0, 0);
		}

		// Draw
		vkCmdDrawIndexed(commandBuffer, numIndices, 1, 0, 0, 0);

		// End occlusion query
		if (occlusionQueryPool != VK_NULL_HANDLE) {
			vkCmdEndQuery(commandBuffer, occlusionQueryPool, 0);
			occlusionQueryActive = true;
		}
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

			// Wait for simulations to complete
			for (size_t i = 0; i < waveTanksPlaceholder.size(); i++) {
				IWaveTank* t = (IWaveTank*)waveTanksPlaceholder[i];
				t->Join();
			}

			// Check if we have any uploads to do
			if (uploadFence == VK_NULL_HANDLE) {
				// Start wave simulations for next frame
				for (size_t i = 0; i < waveTanksPlaceholder.size(); i++) {
					IWaveTank* tank = (IWaveTank*)waveTanksPlaceholder[i];
					switch (i) {
						case 0: tank->SetTimeStep(dt); break;
						case 1: tank->SetTimeStep(dt * 0.15704F / 0.08F); break;
						case 2: tank->SetTimeStep(dt * 0.02344F / 0.08F); break;
					}
					tank->Start();
				}
				return;
			}

			// Wait for previous batched upload to complete
			vkWaitForFences(device->GetDevice(), 1, &uploadFence, VK_TRUE, UINT64_MAX);
			vkResetFences(device->GetDevice(), 1, &uploadFence);

			// Allocate single command buffer for all uploads
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

			bool hasCommands = false;

			// Upload wave data for all layers
			if (!waveTanksPlaceholder.empty()) {
				IWaveTank* t = (IWaveTank*)waveTanksPlaceholder[0];
				size_t bmpSize = t->GetSize() * t->GetSize() * sizeof(uint32_t);

				Handle<VulkanImage> targetImage = waveImageArray ? waveImageArray : waveImage;

				// Transition to TRANSFER_DST_OPTIMAL
				targetImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					0, VK_ACCESS_TRANSFER_WRITE_BIT,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

				// Upload each layer using pooled staging buffers
				for (size_t i = 0; i < waveTanksPlaceholder.size(); i++) {
					IWaveTank* tank = (IWaveTank*)waveTanksPlaceholder[i];
					Handle<VulkanBuffer>& staging = waveStagingBufferPool[i];
					void* data = staging->Map();
					memcpy(data, tank->GetBitmap(), bmpSize);
					staging->Unmap();

					if (waveTanksPlaceholder.size() == 1) {
						targetImage->CopyFromBuffer(commandBuffer, staging->GetBuffer());
					} else {
						targetImage->CopyFromBufferToLayer(commandBuffer, staging->GetBuffer(), static_cast<uint32_t>(i));
					}
				}

				// Generate mipmaps
				targetImage->GenerateMipmaps(commandBuffer);
				hasCommands = true;
			}

			// Update water color texture from map
			bool fullUpdate = true;
			for (size_t i = 0; i < updateBitmap.size(); i++) {
				if (updateBitmap[i] == 0) {
					fullUpdate = false;
					break;
				}
			}

			// Keep staging buffers alive until submission completes
			std::vector<Handle<VulkanBuffer>> stagingBuffers;

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
					stagingBuffers.push_back(staging);

					textureImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						0, VK_ACCESS_TRANSFER_WRITE_BIT,
						VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

					textureImage->CopyFromBuffer(commandBuffer, staging->GetBuffer());

					textureImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

					hasCommands = true;
				}

				for (size_t i = 0; i < updateBitmap.size(); i++)
					updateBitmap[i] = 0;
			} else {
				// Partial update - upload only changed regions
				bool hasPartialUpdates = false;

				textureImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

				for (size_t i = 0; i < updateBitmap.size(); i++) {
					if (updateBitmap[i] == 0) continue;

					int y = static_cast<int>(i / updateBitmapPitch);
					int x = static_cast<int>((i - y * updateBitmapPitch) * 32);

					uint32_t* pixels = bitmap.data() + x + y * w;
					bool modified = false;
					for (int j = 0; j < 32; j++) {
						uint32_t col = gameMap->GetColor(x + j, y, 63);
						int r = (uint8_t)(col);
						int g = (uint8_t)(col >> 8);
						int b = (uint8_t)(col >> 16);
						r = (r * r + 128) >> 8;
						g = (g * g + 128) >> 8;
						b = (b * b + 128) >> 8;
						uint32_t lin = b | (g << 8) | (r << 16);

						if (pixels[j] != lin) modified = true;
						pixels[j] = lin;
					}

					if (modified) {
						Handle<VulkanBuffer> staging = Handle<VulkanBuffer>::New(
							device, 32 * sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
							VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
						void* data = staging->Map();
						memcpy(data, pixels, 32 * sizeof(uint32_t));
						staging->Unmap();
						stagingBuffers.push_back(staging);

						textureImage->CopyRegionFromBuffer(commandBuffer, staging->GetBuffer(),
							static_cast<uint32_t>(x), static_cast<uint32_t>(y), 32, 1);
						hasPartialUpdates = true;
					}

					updateBitmap[i] = 0;
				}

				if (hasPartialUpdates) {
					textureImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
					hasCommands = true;
				}
			}

			vkEndCommandBuffer(commandBuffer);

			// Submit single batched command buffer
			if (hasCommands) {
				VkSubmitInfo submitInfo{};
				submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				submitInfo.commandBufferCount = 1;
				submitInfo.pCommandBuffers = &commandBuffer;

				vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, uploadFence);
			}

			vkFreeCommandBuffers(device->GetDevice(), device->GetCommandPool(), 1, &commandBuffer);

			// Start wave simulations for next frame
			for (size_t i = 0; i < waveTanksPlaceholder.size(); i++) {
				IWaveTank* tank = (IWaveTank*)waveTanksPlaceholder[i];
				switch (i) {
					case 0: tank->SetTimeStep(dt); break;
					case 1: tank->SetTimeStep(dt * 0.15704F / 0.08F); break;
					case 2: tank->SetTimeStep(dt * 0.02344F / 0.08F); break;
				}
				tank->Start();
			}
		}


	void VulkanWaterRenderer::CreateDescriptorPool() {
		// Create descriptor pool for water rendering (3 frames in flight)
		uint32_t frameCount = 3;
		// Water3: 6 samplers (screen, depth, main, waveArray, mirror, mirrorDepth)
		// Water2: 5 samplers (screen, depth, main, waveArray, mirror)
		// Water:  4 samplers (screen, depth, main, wave)
		uint32_t samplersPerFrame = ((int)r_water >= 3) ? 6 : (((int)r_water >= 2) ? 5 : 4);
		std::vector<VkDescriptorPoolSize> poolSizes(2);
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[0].descriptorCount = samplersPerFrame * frameCount;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[1].descriptorCount = 1 * frameCount; // 1 UBO per frame (WaterMatricesUBO only, WaterUBO is now push constants)

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
		// Note: WaterUBO is now push constants for better performance
		uint32_t frameCount = 3;
		waterMatricesUBOs.resize(frameCount);

		// WaterMatricesUBO size: 4*4*16 + 4*4 + 4 + 3*4 = 276 bytes -> pad to 288
		size_t waterMatricesUBOSize = 288;

		for (uint32_t i = 0; i < frameCount; i++) {
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

	// Pre-bind static descriptors that don't change between frames
	// This reduces vkUpdateDescriptorSets overhead each frame
	for (uint32_t i = 0; i < frameCount; i++) {
		std::vector<VkWriteDescriptorSet> staticWrites;
		std::vector<VkDescriptorImageInfo> imageInfos;  // Keep alive until vkUpdateDescriptorSets
		std::vector<VkDescriptorBufferInfo> bufferInfos; // Keep alive until vkUpdateDescriptorSets

		// Reserve space to prevent reallocation (which would invalidate pointers)
		imageInfos.reserve(3);    // max: textureImage + waveImage + spare
		bufferInfos.reserve(1);   // waterMatricesUBO

		// Binding 2: mainTexture (water color) - static
		if (textureImage) {
			imageInfos.push_back({});
			imageInfos.back().imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfos.back().imageView = textureImage->GetImageView();
			imageInfos.back().sampler = textureImage->GetSampler();

			VkWriteDescriptorSet mainTextureWrite{};
			mainTextureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			mainTextureWrite.dstSet = descriptorSets[i];
			mainTextureWrite.dstBinding = 2;
			mainTextureWrite.dstArrayElement = 0;
			mainTextureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			mainTextureWrite.descriptorCount = 1;
			mainTextureWrite.pImageInfo = &imageInfos.back();
			staticWrites.push_back(mainTextureWrite);
		}

		// Binding 3/8: waveTexture or waveTextureArray - static
		Handle<VulkanImage> activeWaveImage = waveImageArray ? waveImageArray : waveImage;
		if (activeWaveImage) {
			imageInfos.push_back({});
			imageInfos.back().imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfos.back().imageView = activeWaveImage->GetImageView();
			imageInfos.back().sampler = activeWaveImage->GetSampler();

			// Binding 3 for single wave texture or array
			VkWriteDescriptorSet waveTextureWrite{};
			waveTextureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			waveTextureWrite.dstSet = descriptorSets[i];
			waveTextureWrite.dstBinding = waveImageArray ? 8 : 3;
			waveTextureWrite.dstArrayElement = 0;
			waveTextureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			waveTextureWrite.descriptorCount = 1;
			waveTextureWrite.pImageInfo = &imageInfos.back();
			staticWrites.push_back(waveTextureWrite);
		}

		// Binding 5: WaterMatricesUBO - buffer is static per frame index
		if (waterMatricesUBOs[i]) {
			bufferInfos.push_back({});
			bufferInfos.back().buffer = waterMatricesUBOs[i]->GetBuffer();
			bufferInfos.back().offset = 0;
			bufferInfos.back().range = VK_WHOLE_SIZE;

			VkWriteDescriptorSet waterMatricesUBOWrite{};
			waterMatricesUBOWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			waterMatricesUBOWrite.dstSet = descriptorSets[i];
			waterMatricesUBOWrite.dstBinding = 5;
			waterMatricesUBOWrite.dstArrayElement = 0;
			waterMatricesUBOWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			waterMatricesUBOWrite.descriptorCount = 1;
			waterMatricesUBOWrite.pBufferInfo = &bufferInfos.back();
			staticWrites.push_back(waterMatricesUBOWrite);
		}

		if (!staticWrites.empty()) {
			vkUpdateDescriptorSets(device->GetDevice(),
			                       static_cast<uint32_t>(staticWrites.size()),
			                       staticWrites.data(), 0, nullptr);
		}
	}
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
			Matrix4 viewMatrix; // Added for Water3 SSR
			Vector4 viewOriginVector;
			float fogDistance;
			float _pad0[3];
		} waterMatricesData;

		// Shaders transform vertices to world space via modelMatrix, then use these
		// matrices on the world-space position. So we pass PV and V (not PV*M, V*M)
		// to avoid double-applying the model matrix.
		waterMatricesData.projectionViewModelMatrix = projectionViewMatrix;
		waterMatricesData.modelMatrix = modelMatrix;
		waterMatricesData.viewModelMatrix = viewMatrix;
		waterMatricesData.viewMatrix = viewMatrix;
		waterMatricesData.viewOriginVector = MakeVector4(sceneDef.viewOrigin.x, sceneDef.viewOrigin.y, sceneDef.viewOrigin.z, 0.0f);
		waterMatricesData.fogDistance = fogDist;

		void* data = waterMatricesUBOs[frameIndex]->Map();
		memcpy(data, &waterMatricesData, sizeof(waterMatricesData));
		waterMatricesUBOs[frameIndex]->Unmap();

		// Update push constants (replaces WaterUBO binding 4)
		waterPushConstants.fogColor = MakeVector4(fogCol.x, fogCol.y, fogCol.z, 0.0f);
		waterPushConstants.skyColor = MakeVector4(skyCol.x, skyCol.y, skyCol.z, 0.0f);
		waterPushConstants.zNearFar = MakeVector2(sceneDef.zNear, sceneDef.zFar);

		waterPushConstants.fovTan = MakeVector4(
			tanf(sceneDef.fovX * 0.5f),
			-tanf(sceneDef.fovY * 0.5f),
			-tanf(sceneDef.fovX * 0.5f),
			tanf(sceneDef.fovY * 0.5f)
		);

		// Calculate water plane in view coordinates
		Matrix4 waterModelView = viewMatrix * modelMatrix;
		Vector3 planeNormal = waterModelView.GetAxis(2);
		float planeD = -Vector3::Dot(planeNormal, waterModelView.GetOrigin());
		waterPushConstants.waterPlane = MakeVector4(planeNormal.x, planeNormal.y, planeNormal.z, planeD);

		waterPushConstants.viewOriginVector = MakeVector4(sceneDef.viewOrigin.x, sceneDef.viewOrigin.y, sceneDef.viewOrigin.z, 0.0f);
		waterPushConstants.displaceScale = MakeVector2(1.0f / tanf(sceneDef.fovX * 0.5f), 1.0f / tanf(sceneDef.fovY * 0.5f));
	}

	void VulkanWaterRenderer::CleanupDescriptorResources() {
		VkDevice vkDevice = device->GetDevice();

		if (descriptorPool != VK_NULL_HANDLE) {
			vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
			descriptorPool = VK_NULL_HANDLE;
		}

		waterMatricesUBOs.clear();
		descriptorSets.clear();
	}

	} // namespace draw
} // namespace spades
