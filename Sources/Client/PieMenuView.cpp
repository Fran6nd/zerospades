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
			constexpr float kDeadZone = 60.0F;
			constexpr float kRingInner = 70.0F;
			constexpr float kRingOuter = 160.0F;
			constexpr float kSliceGapDeg = 4.0F;
			constexpr int kSegmentsPerSlice = 48;
			constexpr float kLabelRadius = 115.0F;

			// Draw a filled annular segment (pie slice without the center)
			// via parallelogram tessellation along the arc.
			void DrawRingSegment(IRenderer& r, Vector2 center, float rInner,
			                     float rOuter, float aStart, float aEnd,
			                     int segments) {
				if (segments <= 0 || rOuter <= rInner || aEnd <= aStart)
					return;

				const AABB2 inRect(0.0F, 0.0F, 1.0F, 1.0F);
				float step = (aEnd - aStart) / static_cast<float>(segments);

				float c0 = cosf(aStart), s0 = sinf(aStart);
				for (int i = 0; i < segments; i++) {
					float a1 = aStart + step * static_cast<float>(i + 1);
					float c1 = cosf(a1), s1 = sinf(a1);

					Vector2 outerNear = center + MakeVector2(c0 * rOuter, s0 * rOuter);
					Vector2 outerFar  = center + MakeVector2(c1 * rOuter, s1 * rOuter);
					Vector2 innerNear = center + MakeVector2(c0 * rInner, s0 * rInner);

					// Parallelogram approximation of the true ring segment.
					// For small `step` the error vs. the correct innerFar is sub-pixel.
					r.DrawImage(nullptr, outerNear, outerFar, innerNear, inRect);

					c0 = c1;
					s0 = s1;
				}
			}
		} // namespace

		PieMenuView::PieMenuView(Client* c, IFont* f, IFont* big)
		    : renderer(c->GetRenderer()), font(f), bigFont(big) {
			worldLabels = {
				_Tr("Client", "Top"),
				_Tr("Client", "Right"),
				_Tr("Client", "Bottom"),
				_Tr("Client", "Left"),
			};
			playerLabels = {
				_Tr("Client", "Follow Me"),
				_Tr("Client", "Retreat"),
				_Tr("Client", "Help Me"),
				_Tr("Client", "Thank You"),
			};
		}

		PieMenuView::~PieMenuView() {}

		void PieMenuView::Open(Variant v, int tgtId) {
			open = true;
			variant = v;
			targetPlayerId = tgtId;
			cursor = {0.0F, 0.0F};
			selection = None;
			openPhase = 0.0F;
			highlight = {0.0F, 0.0F, 0.0F, 0.0F};
		}

		int PieMenuView::Close() {
			int result = selection;
			open = false;
			selection = None;
			targetPlayerId = -1;
			cursor = {0.0F, 0.0F};
			openPhase = 0.0F;
			highlight = {0.0F, 0.0F, 0.0F, 0.0F};
			return result;
		}

		void PieMenuView::Update(float dt) {
			if (!open)
				return;

			constexpr float kOpenRate = 1.0F / 0.12F;
			constexpr float kHighlightUpRate = 1.0F / 0.10F;
			constexpr float kHighlightDownRate = 1.0F / 0.15F;

			openPhase = std::min(1.0F, openPhase + dt * kOpenRate);

			for (int i = 0; i < 4; i++) {
				float target = (selection == i) ? 1.0F : 0.0F;
				float rate = (target > highlight[i]) ? kHighlightUpRate : kHighlightDownRate;
				float delta = dt * rate;
				if (target > highlight[i])
					highlight[i] = std::min(target, highlight[i] + delta);
				else
					highlight[i] = std::max(target, highlight[i] - delta);
			}
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

			float maxR = kRingOuter + 20.0F;
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

			const auto& labels =
			  (variant == Variant::Player) ? playerLabels : worldLabels;

			// Slice angular centers in screen math (y-down):
			// Top=-π/2, Right=0, Bottom=+π/2, Left=π.
			const float kPI = static_cast<float>(M_PI);
			const float halfSliceRad = kPI * 0.25F;
			const float gapRad = kSliceGapDeg * kPI / 180.0F;
			const float sliceCenters[4] = {
				-kPI * 0.5F, 0.0F, kPI * 0.5F, kPI,
			};

			// Ease-out open animation: scale from 0.85 → 1.0, alpha from 0 → 1.
			float eased = 1.0F - (1.0F - openPhase) * (1.0F - openPhase);
			float scale = 0.85F + 0.15F * eased;
			float alpha = eased;

			float rInner = kRingInner * scale;
			float rOuter = kRingOuter * scale;
			float rLabel = kLabelRadius * scale;

			// Backing disc
			renderer.SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 0.55F * alpha));
			DrawRingSegment(renderer, center, 0.0F, rOuter,
			                0.0F, kPI * 2.0F, kSegmentsPerSlice * 4);

			// Slices
			for (int i = 0; i < 4; i++) {
				float h = highlight[i];
				float a0 = sliceCenters[i] - halfSliceRad + gapRad * 0.5F;
				float a1 = sliceCenters[i] + halfSliceRad - gapRad * 0.5F;

				float fillA = (0.08F + (0.85F - 0.08F) * h) * alpha;
				renderer.SetColorAlphaPremultiplied(MakeVector4(fillA, fillA, fillA, fillA));
				float rOutSlice = rOuter + 10.0F * h;
				DrawRingSegment(renderer, center, rInner, rOutSlice, a0, a1,
				                kSegmentsPerSlice);
			}

			// Outer and inner outline rings (thin)
			renderer.SetColorAlphaPremultiplied(MakeVector4(alpha * 0.5F, alpha * 0.5F,
			                                                alpha * 0.5F, alpha * 0.5F));
			DrawRingSegment(renderer, center, rOuter - 1.0F, rOuter,
			                0.0F, kPI * 2.0F, kSegmentsPerSlice * 4);
			DrawRingSegment(renderer, center, rInner, rInner + 1.0F,
			                0.0F, kPI * 2.0F, kSegmentsPerSlice * 4);

			// Labels at kLabelRadius along each cardinal
			for (int i = 0; i < 4; i++) {
				float h = highlight[i];
				Vector2 dir = {cosf(sliceCenters[i]), sinf(sliceCenters[i])};
				Vector2 p = center + dir * rLabel;

				const std::string& label = labels[i];
				Vector2 sz = font->Measure(label);
				Vector2 textPos = {p.x - sz.x * 0.5F, p.y - sz.y * 0.5F};

				float textA = (0.85F + 0.15F * h) * alpha;
				Vector4 textColor = MakeVector4(textA, textA, textA, textA);
				Vector4 textShadow = MakeVector4(0, 0, 0, 0.6F * alpha);
				font->DrawShadow(label, textPos, 1.0F, textColor, textShadow);
			}

			// Center readout: currently selected slice label, scaled by its highlight
			if (selection >= 0 && selection < 4 && bigFont) {
				float h = highlight[selection];
				const std::string& center_label = labels[selection];
				Vector2 sz = bigFont->Measure(center_label);
				Vector2 pos = {center.x - sz.x * 0.5F, center.y - sz.y * 0.5F};
				float a = h * alpha;
				Vector4 col = MakeVector4(a, a, a, a);
				Vector4 shd = MakeVector4(0, 0, 0, 0.7F * a);
				bigFont->DrawShadow(center_label, pos, 1.0F, col, shd);
			}
		}
	} // namespace client
} // namespace spades
