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

#include "GLMapShadowRenderer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include "GLProfiler.h"
#include "GLRadiosityRenderer.h"
#include "GLRenderer.h"
#include "IGLDevice.h"
#include <Client/GameMap.h>
#include <Core/ConcurrentDispatch.h>
#include <Core/Debug.h>

namespace spades {
	namespace draw {
		namespace {
			/** The depth a texel holds when its sunlight meets nothing above the floor. */
			constexpr int kNoHitDepth = 64;

			/** The deepest layer that casts shadows; the one under it is the floor. */
			constexpr int kLastCastingLayer = 62;

			/**
			 * The longest shadow the bake supports, in voxels per voxel of drop: the sun
			 * is kept at least 18.4° high. The *Daytime and Weather* sun is in full
			 * daylight down to 20.7°, so its shadows are exact whenever the light is.
			 */
			constexpr float kMaxShear = 3.0F;

			/** The step the shear is quantized to, so that a sun on a voxel diagonal, as
			 * the default noon one, bakes exactly along it. */
			constexpr float kShearQuantum = 1.0F / 1024.0F;

			/** How far the shear may drift from the sun before the map is baked again: a
			 * voxel at the bottom of the map. */
			constexpr float kRebakeShear = 1.0F / 64.0F;

			constexpr float kInfinity = std::numeric_limits<float>::infinity();

			int CountTrailingZeros(uint64_t v) {
				SPAssert(v != 0);
#if defined(_MSC_VER)
				unsigned long index;
				_BitScanForward64(&index, v);
				return static_cast<int>(index);
#else
				return __builtin_ctzll(v);
#endif
			}

			/**
			 * The columns a texel's sunlight crosses along one map axis. The texel's
			 * footprint is a unit square that moves by `slope` per voxel of drop, so the
			 * `k`th column it reaches, `origin + k * step`, lies under it while the depth
			 * is between `Enter(k)` and `Leave(k)`, both exclusive.
			 */
			struct SunlightAxis {
				int origin;
				int step;
				float slope;

				SunlightAxis(int origin, float shear)
				    : origin(origin), step(shear < 0.0F ? -1 : 1), slope(std::fabs(shear)) {}

				int Column(int k) const { return origin + k * step; }

				float Enter(int k) const {
					if (slope == 0.0F)
						return k == 0 ? -kInfinity : kInfinity;
					return static_cast<float>(k - 1) / slope;
				}

				float Leave(int k) const {
					if (slope == 0.0F)
						return k == 0 ? kInfinity : -kInfinity;
					return static_cast<float>(k + 1) / slope;
				}

				/** The last column entered before `depth`. */
				int LastBefore(float depth) const {
					if (slope == 0.0F)
						return 0;
					return static_cast<int>(std::ceil(depth * slope + 1.0F)) - 1;
				}

				/** The first column still under the footprint after `depth`. */
				int FirstAfter(float depth) const {
					if (slope == 0.0F || !(depth > 0.0F))
						return 0;
					return std::max(0, static_cast<int>(std::floor(depth * slope - 1.0F)) + 1);
				}
			};

			uint32_t BuildPixel(int depth, uint32_t color, GLMapShadowRenderer::Face face) {
				int r = static_cast<uint8_t>(color) >> 2;
				int g = static_cast<uint8_t>(color >> 8) >> 2;
				int b = static_cast<uint8_t>(color >> 16) >> 2;

				// The top bits of red and green say which face the sunlight met.
				uint32_t side = face != GLMapShadowRenderer::Face::Top ? 1 : 0;
				uint32_t sideX = face == GLMapShadowRenderer::Face::SideX ? 1 : 0;

				return r + (g << 8) + (b << 16) + (static_cast<uint32_t>(depth) << 24) +
				       (side << 7) + (sideX << 15);
			}
		} // namespace

		/** Where the sunlight through a texel met the map. */
		struct GLMapShadowRenderer::Hit {
			/** The depth down to which the texel is lit. */
			int depth = kNoHitDepth;
			Face face = Face::Top;
			/** The voxel met, or the floor under the texel when none was. */
			int x = 0, y = 0, z = 0;
		};

		/** Bakes the whole map for a new shear from a copy of its solid columns, so it
		 * never reads the map while the game edits it. */
		class GLMapShadowRenderer::BakeDispatch : public ConcurrentDispatch {
			int w, h;
			std::vector<uint64_t> columns;

		public:
			const Vector2 shear;
			std::vector<Hit> hits;
			std::atomic<bool> done{false};

			BakeDispatch(const client::GameMap& map, Vector2 shear)
			    : w(map.Width()), h(map.Height()), shear(shear) {
				columns.resize(static_cast<std::size_t>(w * h));
				for (int y = 0; y < h; y++)
					for (int x = 0; x < w; x++)
						columns[x + y * w] = map.GetSolidMap(x, y);
			}

			void Run() override {
				SPADES_MARK_FUNCTION();

				auto column = [this](int x, int y) {
					return columns[(x & (w - 1)) + (y & (h - 1)) * w];
				};

				hits.resize(columns.size());
				for (int y = 0; y < h; y++)
					for (int x = 0; x < w; x++)
						hits[x + y * w] = TraceSunlight(column, x, y, shear);

				done = true;
			}
		};

		template <class SolidColumn>
		GLMapShadowRenderer::Hit GLMapShadowRenderer::TraceSunlight(const SolidColumn& solidColumn,
		                                                            int x, int y, Vector2 shear) {
			const SunlightAxis axisX{x, shear.x};
			const SunlightAxis axisY{y, shear.y};
			constexpr float kEnd = static_cast<float>(kLastCastingLayer + 1);

			// The footprint sweeps the columns in order of when it enters them, so the
			// walk stops once nothing it reaches can be met sooner than the best hit. A
			// top face wins a tie against a side face: the light was over it already.
			Hit best;
			float bestEntry = kInfinity;
			bool bestTop = false;
			auto mayBeat = [&](float entry) {
				return entry < bestEntry || (entry == bestEntry && !bestTop);
			};

			for (int kx = 0, lastKx = axisX.LastBefore(kEnd); kx <= lastKx; kx++) {
				const float enterX = axisX.Enter(kx);
				if (!mayBeat(enterX))
					break;
				const float leaveX = axisX.Leave(kx);

				for (int ky = axisY.FirstAfter(enterX),
				         lastKy = axisY.LastBefore(std::min(leaveX, kEnd));
				     ky <= lastKy; ky++) {
					const float enterY = axisY.Enter(ky);
					const float enter = std::max(enterX, enterY);
					if (!mayBeat(enter))
						break;

					const float leave = std::min({leaveX, axisY.Leave(ky), kEnd});
					if (enter >= leave)
						continue;

					// The layers the footprint is over this column for.
					const int zLo = static_cast<int>(std::floor(std::max(enter, 0.0F)));
					const int zHi = std::min(static_cast<int>(std::ceil(leave)) - 1, kLastCastingLayer);
					if (zLo > zHi)
						continue;

					const int columnX = axisX.Column(kx);
					const int columnY = axisY.Column(ky);
					uint64_t solid = solidColumn(columnX, columnY) >> zLo;
					const int layers = zHi - zLo + 1;
					if (layers < 64)
						solid &= (1ULL << layers) - 1;
					if (solid == 0)
						continue;

					const int z = zLo + CountTrailingZeros(solid);

					// Light already over the column when it reaches the voxel meets its top;
					// otherwise it slides in through a side as it enters the column.
					const bool top = static_cast<float>(z) > enter;
					const float entry = top ? static_cast<float>(z) : enter;
					if (!(entry < bestEntry || (entry == bestEntry && top && !bestTop)))
						continue;

					bestEntry = entry;
					bestTop = top;
					best.x = columnX;
					best.y = columnY;
					best.z = z;
					if (top) {
						best.face = Face::Top;
						best.depth = z;
					} else {
						// The face is lit down to where it leaves the texel's light, or ends.
						const bool sideX = enterX >= enterY;
						const float slope = sideX ? axisX.slope : axisY.slope;
						best.face = sideX ? Face::SideX : Face::SideY;
						best.depth = static_cast<int>(
						  std::min(std::ceil(enter + 1.0F / slope), static_cast<float>(z + 1)));
					}
				}
			}

			if (best.depth == kNoHitDepth) {
				best.x = static_cast<int>(std::floor(static_cast<float>(x) + shear.x * kNoHitDepth));
				best.y = static_cast<int>(std::floor(static_cast<float>(y) + shear.y * kNoHitDepth));
				best.z = kLastCastingLayer + 1;
			}
			return best;
		}

		Vector2 GLMapShadowRenderer::ShearForSun(Vector3 sunDirection) {
			// The light travels away from the sun, and per voxel of drop it moves
			// sideways by the sun's run over its rise.
			const Vector2 run = MakeVector2(sunDirection.x, sunDirection.y);
			const float runLength = run.GetLength();
			if (runLength < 1.0E-6F)
				return MakeVector2(0.0F, 0.0F); // overhead

			const float rise = -sunDirection.z;
			const float length = rise > 0.0F ? std::min(runLength / rise, kMaxShear) : kMaxShear;
			Vector2 shear = run * (-length / runLength);
			shear.x = std::round(shear.x / kShearQuantum) * kShearQuantum;
			shear.y = std::round(shear.y / kShearQuantum) * kShearQuantum;
			return shear;
		}

		Vector3 GLMapShadowRenderer::SunDirectionForShear(Vector2 shear) {
			return MakeVector3(-shear.x, -shear.y, -1.0F).Normalize();
		}

		GLMapShadowRenderer::GLMapShadowRenderer(GLRenderer& renderer, client::GameMap* map)
		    : renderer(renderer), device(renderer.GetGLDevice()), map(map), baked(false) {
			SPADES_MARK_FUNCTION();
			texture = device.GenTexture();
			coarseTexture = device.GenTexture();
			device.BindTexture(IGLDevice::Texture2D, texture);
			device.TexImage2D(IGLDevice::Texture2D, 0, IGLDevice::RGBA, map->Width(), map->Height(),
			                  0, IGLDevice::RGBA, IGLDevice::UnsignedByte, NULL);
			device.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureMagFilter,
			                    IGLDevice::Nearest);
			device.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureMinFilter,
			                    IGLDevice::Nearest);
			device.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureWrapS, IGLDevice::Repeat);
			device.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureWrapT, IGLDevice::Repeat);

			device.BindTexture(IGLDevice::Texture2D, coarseTexture);
			device.TexImage2D(IGLDevice::Texture2D, 0, IGLDevice::RGBA8, map->Width() / CoarseSize,
			                  map->Height() / CoarseSize, 0, IGLDevice::BGRA,
			                  IGLDevice::UnsignedByte, NULL);
			device.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureMagFilter,
			                    IGLDevice::Nearest);
			device.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureMinFilter,
			                    IGLDevice::Nearest);
			device.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureWrapS, IGLDevice::Repeat);
			device.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureWrapT, IGLDevice::Repeat);

			w = map->Width();
			h = map->Height();
			d = map->Depth();

			// Replaced by the sun's own on the first update, which bakes the whole map.
			shear = ShearForSun(renderer.GetSunDirection());

			updateBitmapPitch = (w + 31) / 32;
			updateBitmap.resize(updateBitmapPitch * h);

			coarseBitmap.resize((w * h) >> (CoarseBits * 2));

			bitmap.resize(w * h);
			std::fill(updateBitmap.begin(), updateBitmap.end(), 0xffffffffUL);
			std::fill(bitmap.begin(), bitmap.end(), 0xffffffffUL);
		}

		GLMapShadowRenderer::~GLMapShadowRenderer() {
			SPADES_MARK_FUNCTION();

			if (rebake)
				rebake->Join();

			device.DeleteTexture(texture);
			device.DeleteTexture(coarseTexture);
		}

		bool GLMapShadowRenderer::FollowSun() {
			const Vector2 target = ShearForSun(renderer.GetSunDirection());

			if (!baked) {
				// The first update bakes every texel, so it bakes them for this sun.
				shear = target;
				baked = true;
				return false;
			}

			if (rebake) {
				if (!rebake->done.load())
					return false;

				// The radiosity reads `bitmap` on its own thread; swap only while it rests.
				GLRadiosityRenderer* radiosity = renderer.GetRadiosityRenderer();
				if (radiosity && radiosity->IsUpdating())
					return false;

				CompleteRebake();
				return true;
			}

			// Below the horizon the sun casts no shadow to follow.
			if (renderer.GetSunlight() <= 0.0F)
				return false;

			const Vector2 drift = target - shear;
			if (std::max(std::fabs(drift.x), std::fabs(drift.y)) <= kRebakeShear)
				return false;

			changedSinceRebake.clear();
			rebake.reset(new BakeDispatch(*map, target));
			rebake->Start();
			return false;
		}

		void GLMapShadowRenderer::CompleteRebake() {
			SPADES_MARK_FUNCTION();

			rebake->Join();
			shear = rebake->shear;

			for (std::size_t i = 0; i < bitmap.size(); i++) {
				const Hit& hit = rebake->hits[i];
				bitmap[i] = BuildPixel(hit.depth, map->GetColorWrapped(hit.x, hit.y, hit.z), hit.face);
			}
			rebake.reset();

			// The copy the bake read predates these; bake them again from the map.
			for (const IntVector3& v : changedSinceRebake)
				MarkVoxelUpdate(v.x, v.y, v.z);
			changedSinceRebake.clear();
		}

		void GLMapShadowRenderer::Update() {
			SPADES_MARK_FUNCTION();

			GLProfiler::Context profiler(renderer.GetGLProfiler(), "Terrain Shadow Map");
			GLRadiosityRenderer* radiosity = renderer.GetRadiosityRenderer();

			std::vector<uint8_t> coarseUpdateBitmap;
			coarseUpdateBitmap.resize(coarseBitmap.size());
			std::fill(coarseUpdateBitmap.begin(), coarseUpdateBitmap.end(), 0);

			device.BindTexture(IGLDevice::Texture2D, texture);

			if (FollowSun()) {
				// A new projection came in whole. The radiosity follows it on its own terms.
				device.TexSubImage2D(IGLDevice::Texture2D, 0, 0, 0, w, h, IGLDevice::RGBA,
				                     IGLDevice::UnsignedByte, bitmap.data());
				std::fill(coarseUpdateBitmap.begin(), coarseUpdateBitmap.end(), 1);
			}

			for (size_t i = 0; i < updateBitmap.size(); i++) {
				int y = static_cast<int>(i / updateBitmapPitch);
				int x = static_cast<int>((i - y * updateBitmapPitch) * 32);
				if (updateBitmap[i] == 0)
					continue;

				size_t bitmapPixelPosBase = i * 32;

				uint32_t pixels[32];
				bool modified = false;
				for (int j = 0; j < 32; j++) {
					pixels[j] = GeneratePixel(x + j, y);
					if (bitmap[bitmapPixelPosBase + j] != pixels[j]) {
						if (radiosity) {
							IntVector3 v = GetHitVoxel(x + j, y, pixels[j]);
							radiosity->GameMapChanged(v.x, v.y, v.z, map);

							v = GetHitVoxel(x + j, y, bitmap[bitmapPixelPosBase + j]);
							radiosity->GameMapChanged(v.x, v.y, v.z, map);
						}
						bitmap[bitmapPixelPosBase + j] = pixels[j];
						modified = true;
					}
				}

				if (modified) {
					if (!coarseUpdateBitmap[(x >> CoarseBits) +
					                        (y >> CoarseBits) * (w >> CoarseBits)])
						for (int j = 0; j < 32; j += CoarseSize)
							coarseUpdateBitmap[((x + j) >> CoarseBits) +
							                   (y >> CoarseBits) * (w >> CoarseBits)] = 1;

					device.TexSubImage2D(IGLDevice::Texture2D, 0, x, y, 32, 1, IGLDevice::RGBA,
					                     IGLDevice::UnsignedByte, pixels);
				}

				updateBitmap[i] = 0;
			}

			{
				bool coarseUpdated = false;
				int bx = 0, by = 0;
				for (size_t i = 0; i < coarseUpdateBitmap.size(); i++) {
					if (coarseUpdateBitmap[i]) {
						int minValue = -1, maxValue = 0;

						uint32_t *bmp = bitmap.data();
						bmp += bx + by * w;
						for (int y = 0; y < CoarseSize; y++) {
							for (int x = 0; x < CoarseSize; x++) {
								uint32_t value = bmp[x];
								int depth = (int)(value >> 24);
								if (minValue == -1) {
									minValue = maxValue = depth;
								} else {
									if (depth < minValue)
										minValue = depth;
									if (depth > maxValue)
										maxValue = depth;
								}
							}
							bmp += w;
						}

						uint32_t out = minValue << 16;
						out |= maxValue << 8;
						coarseBitmap[i] = out;

						coarseUpdated = true;
					}
					bx += CoarseSize;
					if (bx >= w) {
						bx = 0;
						by += CoarseSize;
					}
				}
				if (coarseUpdated) {
					GLProfiler::Context profiler(renderer.GetGLProfiler(),
					                             "Coarse Shadow Map Upload");

					device.BindTexture(IGLDevice::Texture2D, coarseTexture);
					device.TexSubImage2D(IGLDevice::Texture2D, 0, 0, 0, w >> CoarseBits,
					                     h >> CoarseBits, IGLDevice::BGRA, IGLDevice::UnsignedByte,
					                     coarseBitmap.data());
				}
			}
		}

		uint32_t GLMapShadowRenderer::GeneratePixel(int x, int y) {
			auto column = [this](int x, int y) { return map->GetSolidMapWrapped(x, y); };
			const Hit hit = TraceSunlight(column, x, y, shear);
			return BuildPixel(hit.depth, map->GetColorWrapped(hit.x, hit.y, hit.z), hit.face);
		}

		IntVector3 GLMapShadowRenderer::GetHitVoxel(int x, int y, uint32_t pixel) const {
			const int depth = static_cast<int>(pixel >> 24);
			const float depthF = static_cast<float>(depth);
			return IntVector3::Make(
			  static_cast<int>(std::floor(static_cast<float>(x) + 0.5F + shear.x * depthF)) & (w - 1),
			  static_cast<int>(std::floor(static_cast<float>(y) + 0.5F + shear.y * depthF)) & (h - 1),
			  depth);
		}

		void GLMapShadowRenderer::MarkUpdate(int x, int y) {
			x &= w - 1;
			y &= h - 1;
			updateBitmap[(x >> 5) + y * updateBitmapPitch] |= 1UL << (x & 31);
		}

		void GLMapShadowRenderer::MarkVoxelUpdate(int x, int y, int z) {
			// The texels whose footprint is over column `c` at some depth in `[z, z + 1]`.
			auto texels = [z](int c, float shear, int& first, int& last) {
				const float a = shear * static_cast<float>(z);
				const float b = shear * static_cast<float>(z + 1);
				first = static_cast<int>(std::floor(static_cast<float>(c - 1) - std::max(a, b))) + 1;
				last = static_cast<int>(std::ceil(static_cast<float>(c + 1) - std::min(a, b))) - 1;
			};

			int firstX, lastX, firstY, lastY;
			texels(x, shear.x, firstX, lastX);
			texels(y, shear.y, firstY, lastY);
			for (int ty = firstY; ty <= lastY; ty++)
				for (int tx = firstX; tx <= lastX; tx++)
					MarkUpdate(tx, ty);
		}

		void GLMapShadowRenderer::GameMapChanged(int x, int y, int z, client::GameMap* m) {
			MarkVoxelUpdate(x, y, z);

			// The bake in progress read the map before this change.
			if (rebake)
				changedSinceRebake.push_back(IntVector3::Make(x, y, z));
		}
	} // namespace draw
} // namespace spades
