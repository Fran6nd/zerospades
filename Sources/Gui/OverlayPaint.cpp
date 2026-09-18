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

#include "OverlayPaint.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <Client/IImage.h>
#include <Client/IRenderer.h>

namespace spades {
    namespace gui {
        namespace {
            // Points closer than this (in 2D units) are one point: a zero-length
            // edge has no direction to take a normal from.
            constexpr float kPolygonEpsilon = 1.0e-4F;

            // At a corner the fringe is pushed out along the mitre, which grows as
            // the corner sharpens. Cap the stretch (it scales 1/|m|^2, so a corner
            // vertex moves at most twice as far as an edge does) so a needle-thin
            // corner doesn't throw a long faint spike.
            constexpr float kMaxMiterScale = 4.0F;

            // One framebuffer pixel, in 2D units.
            float PixelSize(client::IRenderer& renderer) {
                const float ratio = renderer.ScreenPixelRatio();
                return ratio > 0.0F ? 1.0F / ratio : 1.0F;
            }

            Vector4 Premultiply(const Vector4& c) {
                return MakeVector4(c.x * c.w, c.y * c.w, c.z * c.w, c.w);
            }
        } // namespace

        void OverlayColorNP(client::IRenderer& renderer, const Vector4& c) {
            renderer.SetColorAlphaPremultiplied(MakeVector4(c.x * c.w, c.y * c.w, c.z * c.w, c.w));
        }
        void OverlayFillRect(client::IRenderer& renderer, float x, float y, float w, float h) {
            renderer.DrawImage((client::IImage*)NULL, AABB2(x, y, w, h));
        }
        void OverlayStrokeRect(client::IRenderer& renderer, float x, float y, float w, float h,
                               float t, const Vector4& c) {
            OverlayColorNP(renderer, c);
            OverlayFillRect(renderer, x, y, w, t);
            OverlayFillRect(renderer, x, y + h - t, w, t);
            OverlayFillRect(renderer, x, y, t, h);
            OverlayFillRect(renderer, x + w - t, y, t, h);
        }
        void OverlayFillConvexPolygon(client::IRenderer& renderer, const Vector2* points,
                                      std::size_t count, const Vector4& c) {
            if (c.w <= 0.0F)
                return;

            std::vector<Vector2> pts;
            pts.reserve(count);
            for (std::size_t i = 0; i < count; i++) {
                if (pts.empty() || (points[i] - pts.back()).GetLength() > kPolygonEpsilon)
                    pts.push_back(points[i]);
            }
            while (pts.size() > 1 && (pts.back() - pts.front()).GetLength() <= kPolygonEpsilon)
                pts.pop_back();
            const std::size_t n = pts.size();
            if (n < 3)
                return;

            // Twice the signed area gives the winding, so normals can be made to
            // point outwards whichever way the caller listed the points.
            float area2 = 0.0F;
            for (std::size_t i = 0; i < n; i++) {
                const Vector2& p = pts[i];
                const Vector2& q = pts[(i + 1) % n];
                area2 += p.x * q.y - q.x * p.y;
            }
            if (std::fabs(area2) <= kPolygonEpsilon)
                return;
            const float orient = area2 > 0.0F ? 1.0F : -1.0F;

            // Outward unit normal of edge i (from point i to point i + 1).
            std::vector<Vector2> edgeNormal(n);
            for (std::size_t i = 0; i < n; i++) {
                const Vector2 d = pts[(i + 1) % n] - pts[i];
                edgeNormal[i] = MakeVector2(d.y, -d.x) * (orient / d.GetLength());
            }

            // Each vertex moves half a pixel in (solid core) and half a pixel out
            // (transparent rim) along the mitre of its two edges, scaled so both
            // edges end up exactly half a pixel from where they were.
            const float half = PixelSize(renderer) * 0.5F;
            std::vector<Vector2> inner(n), outer(n);
            for (std::size_t i = 0; i < n; i++) {
                Vector2 m = (edgeNormal[(i + n - 1) % n] + edgeNormal[i]) * 0.5F;
                const float len2 = m.x * m.x + m.y * m.y;
                if (len2 > kPolygonEpsilon)
                    m *= std::min(1.0F / len2, kMaxMiterScale);
                inner[i] = pts[i] - m * half;
                outer[i] = pts[i] + m * half;
            }

            const Vector4 solid = Premultiply(c);
            const Vector4 clear = MakeVector4(0.0F, 0.0F, 0.0F, 0.0F);

            for (std::size_t i = 1; i + 1 < n; i++)
                renderer.DrawShadedTriangle(inner[0], inner[i], inner[i + 1], solid, solid, solid);

            for (std::size_t i = 0; i < n; i++) {
                const std::size_t j = (i + 1) % n;
                renderer.DrawShadedTriangle(inner[i], inner[j], outer[j], solid, solid, clear);
                renderer.DrawShadedTriangle(inner[i], outer[j], outer[i], solid, clear, clear);
            }
        }

        void OverlayStrokeLine(client::IRenderer& renderer, const Vector2& a, const Vector2& b,
                               float width, const Vector4& c) {
            const Vector2 d = b - a;
            const float len = d.GetLength();
            if (len <= kPolygonEpsilon || width <= 0.0F)
                return;

            Vector4 col = c;
            const float pixel = PixelSize(renderer);
            if (width < pixel) {
                col.w *= width / pixel;
                width = pixel;
            }

            const Vector2 n = MakeVector2(-d.y, d.x) * (width * 0.5F / len);
            const Vector2 quad[4] = {a + n, b + n, b - n, a - n};
            OverlayFillConvexPolygon(renderer, quad, 4, col);
        }

        bool OverlayInRect(const Vector2& p, float x, float y, float w, float h) {
            return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
        }
    } // namespace gui
} // namespace spades
