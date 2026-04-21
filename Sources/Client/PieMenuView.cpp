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

#include "PieMenuView.h"

#include <cmath>

#include "Client.h"
#include "IFont.h"
#include "IRenderer.h"
#include <Core/Strings.h>

namespace spades {
	namespace client {

		namespace {
			constexpr float kDeadZone = 20.0F;
			constexpr float kRingRadius = 140.0F;
			constexpr float kSliceRadius = 64.0F;
		} // namespace

		PieMenuView::PieMenuView(Client* c, IFont* f)
		    : renderer(c->GetRenderer()), font(f) {
			worldLabels = {
				_Tr("Client", "Top"),
				_Tr("Client", "Right"),
				_Tr("Client", "Bottom"),
				_Tr("Client", "Left"),
			};
			playerLabels = {
				_Tr("Client", "Top"),
				_Tr("Client", "Right"),
				_Tr("Client", "Bottom"),
				_Tr("Client", "Left"),
			};
		}

		PieMenuView::~PieMenuView() {}

		void PieMenuView::Open(Variant v) {
			open = true;
			variant = v;
			cursor = {0.0F, 0.0F};
			selection = None;
		}

		int PieMenuView::Close() {
			int result = selection;
			open = false;
			selection = None;
			cursor = {0.0F, 0.0F};
			return result;
		}

		const std::string& PieMenuView::GetSelectionLabel() const {
			static const std::string empty;
			if (selection < 0 || selection > 3)
				return empty;
			const auto& labels =
			  (variant == Variant::Player) ? playerLabels : worldLabels;
			return labels[static_cast<size_t>(selection)];
		}

		void PieMenuView::HandleMouseDelta(float dx, float dy) {
			if (!open)
				return;

			cursor.x += dx;
			cursor.y += dy;

			float maxR = kRingRadius + kSliceRadius;
			float len = sqrtf(cursor.x * cursor.x + cursor.y * cursor.y);
			if (len > maxR) {
				cursor.x *= maxR / len;
				cursor.y *= maxR / len;
				len = maxR;
			}

			if (len < kDeadZone) {
				selection = None;
				return;
			}

			// angle: 0 = up, clockwise. atan2(x, -y) gives that.
			float angle = atan2f(cursor.x, -cursor.y);
			if (angle < 0.0F)
				angle += static_cast<float>(M_PI) * 2.0F;

			// 4 slices of 90 deg, centered on cardinal directions
			float quarter = static_cast<float>(M_PI) * 0.5F;
			int idx = static_cast<int>(floorf((angle + quarter * 0.5F) / quarter)) % 4;
			selection = idx;
		}

		void PieMenuView::Draw() {
			if (!open)
				return;

			float sw = renderer.ScreenWidth();
			float sh = renderer.ScreenHeight();
			Vector2 center = {sw * 0.5F, sh * 0.5F};

			// dim background circle (approximated by a filled square behind the ring)
			renderer.SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 0.35F));
			float r = kRingRadius + kSliceRadius;
			renderer.DrawFilledRect(center.x - r, center.y - r, center.x + r,
			                        center.y + r);

			const auto& labels =
			  (variant == Variant::Player) ? playerLabels : worldLabels;

			// slice positions (Top, Right, Bottom, Left)
			std::array<Vector2, 4> offsets = {{
				{0.0F, -kRingRadius},
				{kRingRadius, 0.0F},
				{0.0F, kRingRadius},
				{-kRingRadius, 0.0F},
			}};

			for (int i = 0; i < 4; i++) {
				Vector2 p = center + offsets[i];
				bool hot = (selection == i);

				Vector4 fill = hot ? MakeVector4(1.0F, 1.0F, 1.0F, 0.85F)
				                   : MakeVector4(0.0F, 0.0F, 0.0F, 0.6F);
				Vector4 edge = MakeVector4(1.0F, 1.0F, 1.0F, hot ? 1.0F : 0.5F);

				float half = kSliceRadius * 0.5F;
				renderer.SetColorAlphaPremultiplied(fill);
				renderer.DrawFilledRect(p.x - half, p.y - half, p.x + half, p.y + half);
				renderer.SetColorAlphaPremultiplied(edge);
				renderer.DrawOutlinedRect(p.x - half, p.y - half, p.x + half, p.y + half);

				// label
				const std::string& label = labels[i];
				Vector2 sz = font->Measure(label);
				Vector2 textPos = {p.x - sz.x * 0.5F, p.y - sz.y * 0.5F};
				Vector4 textColor = hot ? MakeVector4(0, 0, 0, 1) : MakeVector4(1, 1, 1, 1);
				Vector4 textShadow = hot ? MakeVector4(1, 1, 1, 0.3F) : MakeVector4(0, 0, 0, 0.5F);
				font->DrawShadow(label, textPos, 1.0F, textColor, textShadow);
			}

			// cursor dot
			Vector2 dot = center + cursor;
			renderer.SetColorAlphaPremultiplied(MakeVector4(1, 1, 1, 1));
			renderer.DrawFilledRect(dot.x - 2, dot.y - 2, dot.x + 2, dot.y + 2);
		}
	} // namespace client
} // namespace spades
