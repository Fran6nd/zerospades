/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include <algorithm>
#include <cmath>

#include "IRenderer.h"
#include "TeamplayMarker.h"

namespace spades {
	namespace client {
		namespace {
			/** Widening a diamond's border by `w` perpendicular to its edges moves each
			 * tip out by `w * sqrt(2)`, since the edges run at 45 degrees. */
			constexpr float kEdgeToTip = 1.41421356F;

			/** How far the drop shadow sits below the marker, in pixels. */
			constexpr float kShadowOffset = 1.0F;

			/** Outline and rim widths, perpendicular to the edges, as a fraction of the
			 * half size. Both are held to a pixel at least so a small marker keeps
			 * its structure instead of turning into a flat blob. */
			constexpr float kOutlineRatio = 0.14F;
			constexpr float kRimRatio = 0.14F;

			/** The core's half size as a fraction of the body's. */
			constexpr float kCoreRatio = 0.3F;

			/** How far the rim and the core are lifted toward white. */
			constexpr float kRimTint = 0.45F;
			constexpr float kCoreTint = 0.75F;

			float OutlineWidth(float halfSize) { return std::max(1.0F, halfSize * kOutlineRatio); }
			float RimWidth(float halfSize) { return std::max(1.0F, halfSize * kRimRatio); }

			Vector3 TowardWhite(const Vector3& c, float amount) {
				return c + (MakeVector3(1, 1, 1) - c) * amount;
			}

			Vector4 Premultiplied(const Vector3& c, float alpha) {
				return MakeVector4(c.x * alpha, c.y * alpha, c.z * alpha, alpha);
			}

			/** A filled diamond is a parallelogram, which the renderer draws natively
			 * from three of its corners. */
			void FillDiamond(IRenderer& renderer, const Vector2& c, float halfSize,
							 const Vector4& premultipliedColor) {
				if (halfSize <= 0.0F || premultipliedColor.w <= 0.0F)
					return;

				renderer.SetColorAlphaPremultiplied(premultipliedColor);
				renderer.DrawImage(nullptr, MakeVector2(c.x, c.y - halfSize),
								   MakeVector2(c.x + halfSize, c.y),
								   MakeVector2(c.x - halfSize, c.y), AABB2(0, 0, 1, 1));
			}
		} // namespace

		float GetPingDiamondExtent(float halfSize) {
			return halfSize + OutlineWidth(halfSize) * kEdgeToTip + kShadowOffset;
		}

		void DrawPingDiamond(IRenderer& renderer, Vector2 center, float halfSize,
							 const Vector3& color, float alpha) {
			if (halfSize <= 0.0F || alpha <= 0.0F)
				return;

			// Diamond tips on pixel centres keep both diagonals symmetric.
			center.x = std::floor(center.x) + 0.5F;
			center.y = std::floor(center.y) + 0.5F;

			const float outlineHalf = halfSize + OutlineWidth(halfSize) * kEdgeToTip;
			const float bodyHalf = halfSize - RimWidth(halfSize) * kEdgeToTip;
			const float coreHalf = halfSize * kCoreRatio;

			const Vector3 black = MakeVector3(0, 0, 0);

			// Back to front: shadow, outline, rim, body, core.
			FillDiamond(renderer, center + MakeVector2(0.0F, kShadowOffset),
						outlineHalf + 0.5F, Premultiplied(black, 0.35F * alpha));
			FillDiamond(renderer, center, outlineHalf, Premultiplied(black, 0.85F * alpha));
			FillDiamond(renderer, center, halfSize,
						Premultiplied(TowardWhite(color, kRimTint), alpha));
			FillDiamond(renderer, center, bodyHalf, Premultiplied(color, alpha));

			// Below a pixel the core would only shimmer as the marker moves.
			if (coreHalf >= 1.0F)
				FillDiamond(renderer, center, coreHalf,
							Premultiplied(TowardWhite(color, kCoreTint), alpha));
		}
	} // namespace client
} // namespace spades
