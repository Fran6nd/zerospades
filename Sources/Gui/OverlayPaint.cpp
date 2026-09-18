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
#include <array>
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

            // Polygons up to this many points are worked on without touching the
            // heap; every overlay shape drawn per frame (lines, cube facets) fits.
            constexpr std::size_t kInlinePolygonPoints = 16;

            // Working storage for one polygon's points: on the stack for the small
            // shapes drawn every frame, on the heap only for a larger one.
            class PointScratch {
            public:
                explicit PointScratch(std::size_t capacity) {
                    if (capacity > local.size()) {
                        heap.resize(capacity);
                        data = heap.data();
                    } else {
                        data = local.data();
                    }
                }
                PointScratch(const PointScratch&) = delete;
                PointScratch& operator=(const PointScratch&) = delete;

                Vector2& operator[](std::size_t i) { return data[i]; }
                const Vector2& operator[](std::size_t i) const { return data[i]; }
                Vector2* Data() { return data; }

            private:
                std::array<Vector2, kInlinePolygonPoints> local;
                std::vector<Vector2> heap;
                Vector2* data;
            };

            // Draws a polygon given as its solid core (`inner`) and the outline its
            // anti-aliased rim fades out to (`outer`), point for point: the core as
            // a fan, then each edge's rim as a quad fading from `solid` to nothing.
            void DrawFringedPolygon(client::IRenderer& renderer, const Vector2* inner,
                                    const Vector2* outer, std::size_t n, const Vector4& solid) {
                const Vector4 clear = MakeVector4(0.0F, 0.0F, 0.0F, 0.0F);

                for (std::size_t i = 1; i + 1 < n; i++)
                    renderer.DrawShadedTriangle(inner[0], inner[i], inner[i + 1], solid, solid,
                                                solid);

                for (std::size_t i = 0; i < n; i++) {
                    const std::size_t j = (i + 1) % n;
                    renderer.DrawShadedTriangle(inner[i], inner[j], outer[j], solid, solid, clear);
                    renderer.DrawShadedTriangle(inner[i], outer[j], outer[i], solid, clear, clear);
                }
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

            PointScratch pts(count);
            std::size_t n = 0;
            for (std::size_t i = 0; i < count; i++) {
                if (n == 0 || (points[i] - pts[n - 1]).GetLength() > kPolygonEpsilon)
                    pts[n++] = points[i];
            }
            while (n > 1 && (pts[n - 1] - pts[0]).GetLength() <= kPolygonEpsilon)
                n--;
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
            PointScratch edgeNormal(n);
            for (std::size_t i = 0; i < n; i++) {
                const Vector2 d = pts[(i + 1) % n] - pts[i];
                edgeNormal[i] = MakeVector2(d.y, -d.x) * (orient / d.GetLength());
            }

            // Each vertex moves half a pixel in (solid core) and half a pixel out
            // (transparent rim) along the mitre of its two edges, scaled so both
            // edges end up exactly half a pixel from where they were.
            const float half = PixelSize(renderer) * 0.5F;
            PointScratch inner(n), outer(n);
            for (std::size_t i = 0; i < n; i++) {
                Vector2 m = (edgeNormal[(i + n - 1) % n] + edgeNormal[i]) * 0.5F;
                const float len2 = m.x * m.x + m.y * m.y;
                if (len2 > kPolygonEpsilon)
                    m *= std::min(1.0F / len2, kMaxMiterScale);
                inner[i] = pts[i] - m * half;
                outer[i] = pts[i] + m * half;
            }

            DrawFringedPolygon(renderer, inner.Data(), outer.Data(), n, Premultiply(c));
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

            if (col.w <= 0.0F)
                return;

            // The same geometry OverlayFillConvexPolygon gives the line's rectangle,
            // built directly: every side moves half a pixel in for the core and
            // half a pixel out for the rim. The core stops at zero rather than
            // turning inside out when the line is shorter or thinner than a pixel.
            const float half = pixel * 0.5F;
            const Vector2 along = d * (1.0F / len);
            const Vector2 across = MakeVector2(-along.y, along.x);
            const Vector2 centre = (a + b) * 0.5F;
            const float halfLength = len * 0.5F, halfWidth = width * 0.5F;

            const Vector2 innerAlong = along * std::max(halfLength - half, 0.0F);
            const Vector2 innerAcross = across * std::max(halfWidth - half, 0.0F);
            const Vector2 outerAlong = along * (halfLength + half);
            const Vector2 outerAcross = across * (halfWidth + half);

            const Vector2 inner[4] = {
              centre - innerAlong + innerAcross, centre + innerAlong + innerAcross,
              centre + innerAlong - innerAcross, centre - innerAlong - innerAcross};
            const Vector2 outer[4] = {
              centre - outerAlong + outerAcross, centre + outerAlong + outerAcross,
              centre + outerAlong - outerAcross, centre - outerAlong - outerAcross};
            DrawFringedPolygon(renderer, inner, outer, 4, Premultiply(col));
        }

        bool OverlayInRect(const Vector2& p, float x, float y, float w, float h) {
            return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
        }
    } // namespace gui
} // namespace spades
