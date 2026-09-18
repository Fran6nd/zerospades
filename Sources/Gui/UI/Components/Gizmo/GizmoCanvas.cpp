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

#include "GizmoCanvas.h"

#include <algorithm>
#include <cmath>

#include <Client/IRenderer.h>

namespace spades {
	namespace gui {
		namespace {
			// Segments per circle, by radius: smooth at any size, bounded cost.
			int CircleSegments(float radius) {
				return std::max(12, std::min(64, int(radius * 0.75F)));
			}
		} // namespace

		GizmoCanvas::GizmoCanvas(client::IRenderer& renderer) : renderer(renderer) {}

		void GizmoCanvas::SetColor(const Vector4& c) {
			renderer.SetColorAlphaPremultiplied(MakeVector4(c.x * c.w, c.y * c.w, c.z * c.w, c.w));
		}

		void GizmoCanvas::Triangle(const Vector2& a, const Vector2& b, const Vector2& c,
		                           const Vector4& color) {
			SetColor(color);
			renderer.DrawFilledTriangle(a, b, c);
		}

		void GizmoCanvas::Convex(const Vector2* points, int count, const Vector4& color) {
			if (count < 3)
				return;
			SetColor(color);
			for (int i = 1; i + 1 < count; i++)
				renderer.DrawFilledTriangle(points[0], points[i], points[i + 1]);
		}

		void GizmoCanvas::Segment(const Vector2& a, const Vector2& b, float width, float extend) {
			Vector2 d = b - a;
			float len = d.GetLength();
			if (len < 1.0e-4F)
				return;
			Vector2 dir = d * (1.0F / len);
			Vector2 n = MakeVector2(-dir.y, dir.x) * (width * 0.5F);
			Vector2 p = a - dir * extend, q = b + dir * extend;
			renderer.DrawFilledTriangle(p + n, q + n, q - n);
			renderer.DrawFilledTriangle(p + n, q - n, p - n);
		}

		void GizmoCanvas::Line(const Vector2& a, const Vector2& b, float width,
		                       const Vector4& color) {
			SetColor(color);
			Segment(a, b, width, 0.0F);
		}

		void GizmoCanvas::Polyline(const std::vector<Vector2>& points, float width,
		                           const Vector4& color, bool closed) {
			size_t n = points.size();
			if (n < 2)
				return;
			SetColor(color);
			float extend = width * 0.5F;
			for (size_t i = 0; i + 1 < n; i++)
				Segment(points[i], points[i + 1], width, extend);
			if (closed && n > 2)
				Segment(points[n - 1], points[0], width, extend);
		}

		void GizmoCanvas::Disc(const Vector2& center, float radius, const Vector4& color) {
			if (radius <= 0.0F)
				return;
			SetColor(color);
			int segments = CircleSegments(radius);
			float step = 2.0F * M_PI_F / float(segments);
			Vector2 prev = center + MakeVector2(radius, 0.0F);
			for (int i = 1; i <= segments; i++) {
				float a = float(i) * step;
				Vector2 cur = center + MakeVector2(std::cos(a), std::sin(a)) * radius;
				renderer.DrawFilledTriangle(center, prev, cur);
				prev = cur;
			}
		}

		void GizmoCanvas::Circle(const Vector2& center, float radius, float width,
		                         const Vector4& color) {
			float inner = std::max(0.0F, radius - width * 0.5F);
			float outer = radius + width * 0.5F;
			if (outer <= 0.0F)
				return;
			SetColor(color);
			int segments = CircleSegments(outer);
			float step = 2.0F * M_PI_F / float(segments);
			Vector2 d1 = MakeVector2(1.0F, 0.0F);
			for (int i = 1; i <= segments; i++) {
				float a = float(i) * step;
				Vector2 d2 = MakeVector2(std::cos(a), std::sin(a));
				renderer.DrawFilledTriangle(center + d1 * inner, center + d1 * outer,
				                            center + d2 * outer);
				renderer.DrawFilledTriangle(center + d1 * inner, center + d2 * outer,
				                            center + d2 * inner);
				d1 = d2;
			}
		}
	} // namespace gui
} // namespace spades
