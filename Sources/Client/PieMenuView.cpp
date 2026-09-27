/*
 Copyright (c) 2026 Francois ND
 based on code of OpenSpades (c) yvt 2013.

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
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include "PieMenuView.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <vector>

#include "Client.h"
#include "IFont.h"
#include "IRenderer.h"
#include <Core/Debug.h>
#include <Core/Settings.h>
#include <Core/Strings.h>

SPADES_SETTING(cg_keyAltAttack);

namespace spades {
	namespace client {

		namespace {
			constexpr float kDeadZone = 66.0F;
			constexpr float kRingInner = 77.0F;
			constexpr float kRingOuter = 176.0F;
			constexpr float kSliceGapDeg = 1.0F;
			constexpr float kLabelRadius = 126.5F;
			// How far past kRingOuter a slice reaches when it is fully highlighted.
			constexpr float kHighlightReach = 10.0F;

			// Half the width of a circle of radius r along a line dy from its centre.
			float CircleHalfWidth(float dy, float r) {
				float s = r * r - dy * dy;
				return (s > 0.0F) ? sqrtf(s) : 0.0F;
			}

			// What a shape covers along one horizontal line: at most two intervals,
			// since a line through the hole of a ring splits it in two.
			struct RowSpans {
				int count = 0;
				std::array<float, 2> lo{};
				std::array<float, 2> hi{};

				void Add(float a, float b) {
					if (b > a && count < 2) {
						lo[static_cast<size_t>(count)] = a;
						hi[static_cast<size_t>(count)] = b;
						count++;
					}
				}
			};

			// Lines sampled per pixel row. Coverage across a row is exact, so these
			// only have to resolve edges that run close to horizontal, like the top
			// and bottom of a ring.
			constexpr int kSubRows = 8;
			// Coverage steps a run of pixels is drawn at; neighbours that round to
			// the same step merge into one rect.
			constexpr float kCoverageLevels = 255.0F;

			// Fills a shape with anti-aliased edges. The 2D pass is not multisampled,
			// so a shape drawn as bare rects or triangles comes out stair-stepped;
			// instead each pixel's coverage is worked out here and the pixel drawn
			// with its alpha scaled by it. `spansAt(y)` gives the shape's extent along
			// the horizontal line at screen y, `bounds` must contain the shape, and
			// `color` is premultiplied.
			template <class SpanFn>
			void FillShape(IRenderer& r, Vector4 color, const AABB2& bounds, SpanFn&& spansAt) {
				const int px0 = static_cast<int>(floorf(bounds.GetMinX()));
				const int px1 = static_cast<int>(ceilf(bounds.GetMaxX()));
				const int py0 = static_cast<int>(floorf(bounds.min.y));
				const int py1 = static_cast<int>(ceilf(bounds.max.y));
				if (px1 <= px0 || py1 <= py0 || color.w <= 0.0F)
					return;

				const int width = px1 - px0;
				const float widthF = static_cast<float>(width);
				constexpr float kSampleWeight = 1.0F / static_cast<float>(kSubRows);

				// A span's partial end pixels go straight into `partial`; the run of
				// pixels it covers in full goes into `delta` as a difference array,
				// so a row costs its width however many spans and samples land in it.
				std::vector<float> partial(static_cast<size_t>(width) + 1);
				std::vector<float> delta(static_cast<size_t>(width) + 1);

				for (int py = py0; py < py1; py++) {
					int touchedLo = width;
					int touchedHi = 0;

					for (int sub = 0; sub < kSubRows; sub++) {
						float y = static_cast<float>(py) +
								  (static_cast<float>(sub) + 0.5F) * kSampleWeight;
						RowSpans spans = spansAt(y);
						for (int k = 0; k < spans.count; k++) {
							float a = Clamp(spans.lo[static_cast<size_t>(k)] -
											  static_cast<float>(px0), 0.0F, widthF);
							float b = Clamp(spans.hi[static_cast<size_t>(k)] -
											  static_cast<float>(px0), 0.0F, widthF);
							if (b <= a)
								continue;

							int ia = static_cast<int>(floorf(a));
							int ib = static_cast<int>(floorf(b));
							if (ia == ib) {
								partial[static_cast<size_t>(ia)] += (b - a) * kSampleWeight;
							} else {
								partial[static_cast<size_t>(ia)] +=
								  (static_cast<float>(ia + 1) - a) * kSampleWeight;
								delta[static_cast<size_t>(ia + 1)] += kSampleWeight;
								delta[static_cast<size_t>(ib)] -= kSampleWeight;
								if (ib < width)
									partial[static_cast<size_t>(ib)] +=
									  (b - static_cast<float>(ib)) * kSampleWeight;
							}
							touchedLo = std::min(touchedLo, ia);
							touchedHi = std::max(touchedHi, std::min(ib + 1, width));
						}
					}

					// Walk the touched pixels and draw each run of equal coverage.
					// One step past the end closes the last run.
					float running = 0.0F;
					int runStart = touchedLo;
					int runLevel = 0;
					for (int i = touchedLo; i <= touchedHi; i++) {
						int level = 0;
						if (i < touchedHi) {
							running += delta[static_cast<size_t>(i)];
							float cov = Clamp(running + partial[static_cast<size_t>(i)],
											  0.0F, 1.0F);
							level = static_cast<int>(cov * kCoverageLevels + 0.5F);
						}
						if (level == runLevel)
							continue;
						if (runLevel > 0) {
							r.SetColorAlphaPremultiplied(
							  color * (static_cast<float>(runLevel) / kCoverageLevels));
							r.DrawImage(nullptr, AABB2(static_cast<float>(px0 + runStart),
													   static_cast<float>(py),
													   static_cast<float>(i - runStart), 1.0F));
						}
						runStart = i;
						runLevel = level;
					}

					// Only what this row touched is dirty; a slice covers a fraction
					// of its bounds, so clearing the whole width would be most of the work.
					if (touchedHi >= touchedLo) {
						std::fill(partial.begin() + touchedLo, partial.begin() + touchedHi + 1, 0.0F);
						std::fill(delta.begin() + touchedLo, delta.begin() + touchedHi + 1, 0.0F);
					}
				}
			}

			void DrawDiscFill(IRenderer& r, Vector4 color, Vector2 center, float rOut) {
				FillShape(r, color,
						  AABB2(center.x - rOut, center.y - rOut, 2.0F * rOut, 2.0F * rOut),
						  [&](float y) {
							  RowSpans spans;
							  float w = CircleHalfWidth(y - center.y, rOut);
							  spans.Add(center.x - w, center.x + w);
							  return spans;
						  });
			}

			void DrawAnnulusFill(IRenderer& r, Vector4 color, Vector2 center, float rIn,
								 float rOut) {
				FillShape(r, color,
						  AABB2(center.x - rOut, center.y - rOut, 2.0F * rOut, 2.0F * rOut),
						  [&](float y) {
							  RowSpans spans;
							  float dy = y - center.y;
							  float wOut = CircleHalfWidth(dy, rOut);
							  float wIn = CircleHalfWidth(dy, rIn);
							  if (wIn > 0.0F) {
								  spans.Add(center.x - wOut, center.x - wIn);
								  spans.Add(center.x + wIn, center.x + wOut);
							  } else {
								  spans.Add(center.x - wOut, center.x + wOut);
							  }
							  return spans;
						  });
			}

			// Annulus ∩ wedge. Rays at θ_c ± α are encoded as (s1,c1) and (s2,c2).
			// Inside-wedge half-planes:   -x·s1 + y·c1 ≥ 0	  and	x·s2 - y·c2 ≥ 0.
			void DrawSliceFill(IRenderer& r, Vector4 color, Vector2 center, float rIn,
							   float rOut, float s1, float c1, float s2, float c2) {
				const float kInf = std::numeric_limits<float>::infinity();
				FillShape(
				  r, color, AABB2(center.x - rOut, center.y - rOut, 2.0F * rOut, 2.0F * rOut),
				  [&](float y) {
					  RowSpans spans;
					  float dy = y - center.y;

					  // Clip by wedge rays.
					  float xLoW = -kInf, xHiW = kInf;
					  if (s1 > 0.0F) {
						  xHiW = std::min(xHiW, dy * c1 / s1);
					  } else if (s1 < 0.0F) {
						  xLoW = std::max(xLoW, dy * c1 / s1);
					  } else if (dy * c1 < 0.0F) {
						  return spans;
					  }
					  if (s2 > 0.0F) {
						  xLoW = std::max(xLoW, dy * c2 / s2);
					  } else if (s2 < 0.0F) {
						  xHiW = std::min(xHiW, dy * c2 / s2);
					  } else if (dy * c2 > 0.0F) {
						  return spans;
					  }

					  auto clipped = [&](float xA, float xB) {
						  spans.Add(center.x + std::max(xA, xLoW),
									center.x + std::min(xB, xHiW));
					  };
					  float wOut = CircleHalfWidth(dy, rOut);
					  float wIn = CircleHalfWidth(dy, rIn);
					  if (wIn > 0.0F) {
						  clipped(-wOut, -wIn);
						  clipped(wIn, wOut);
					  } else {
						  clipped(-wOut, wOut);
					  }
					  return spans;
				  });
			}

			// Binding names are stored verbosely; the hint has to stay readable at
			// the size it is drawn, so the mouse buttons get their common short forms.
			std::string ShortKeyName(const std::string& key) {
				if (EqualsIgnoringCase(key, "LeftMouseButton"))
					return "LMB";
				if (EqualsIgnoringCase(key, "RightMouseButton"))
					return "RMB";
				if (EqualsIgnoringCase(key, "MiddleMouseButton"))
					return "MMB";
				return ToUpperCase(key);
			}

			// Ring definitions. Slice order is top, then clockwise; Affirmative and
			// Negative occupy the same slots in both variants so the gesture transfers.
			struct PageDef {
				const char* name;
				bool global;
				const char* labels[PieMenuView::kSliceCount];
				// Slices that drop a Teamplay ping on whatever the crosshair was
				// on instead of talking, falling back to the same message on chat
				// where the server does not allow pings. The ping carries the
				// slice's own label as its reason: the extension assigns no reason
				// values, and a receiving client renders the string as it came, so
				// a made-up token would only show up as "tear" on another screen.
				bool pings[PieMenuView::kSliceCount];
			};

			// Layout rule, held across every ring: slice 0 is at the top and the
			// rest follow clockwise, the vertical axis carries a pair of opposites,
			// the right half is about them and the left half is about us.
			//
			// The ring a message sits on is what says how it travels, so nobody has
			// to remember it slice by slice: the first ring points at a place, the
			// second one talks, the third one calls the game.

			// Every slice names somewhere, so every slice drops a marker there: on the
			// ground, or on the player the crosshair was on, where the ray met them.
			// A server without the extension gets the same words on team chat.
			constexpr PageDef kPointPage = {
			  "Point", false,
			  {"Enemy Here!", "Tear It Down!", "Watch This Spot",
			   "Go Here!", "Let's Dig Here", "Help Me Build"},
			  {true, true, true, true, true, true}};

			// Offered when the crosshair is on terrain rather than on a player.
			const PageDef kWorldPages[] = {
				kPointPage,
				// Said to the team, about nowhere in particular: a marker would only
				// put "thank you" on a piece of ground.
				{"Social", false,
				 {"Affirmative", "Thank You", "Hi!",
				  "Negative", "Sorry!", "Help Me"},
				 {false, false, false, false, false, false}},
				// The objective rather than the ground: where the intel is and what
				// to do about it is already known to everyone, so none of it points.
				{"Tactics", false,
				 {"Attack!", "Get the Intel!", "Enemy Has the Intel!",
				  "Fall Back!", "Regroup on Me", "Defend the Intel!"},
				 {false, false, false, false, false, false}},
			};

			// Offered while the crosshair is on a teammate, and sent to them alone.
			// A person is not a place, so their own rings do not point; the rings keep
			// the order they have on terrain, and Social keeps the same six words, so
			// the gesture is the same whoever it is aimed at. Point comes last, one
			// flip back from the first ring, so calling a spot never needs the
			// crosshair taken off a player first.
			const PageDef kTeammatePages[] = {
				// Directions are relative to the teammate under the crosshair, which
				// makes them exact in a way a broadcast "our right" cannot be. Up,
				// down, left and right sit where they point; the two threats that
				// have no direction take the lower corners.
				{"Warn", false,
				 {"Above You!", "On Your Right!", "Behind You!",
				  "Below You!", "Sniper on You!", "On Your Left!"},
				 {false, false, false, false, false, false}},
				{"Social", false,
				 {"Affirmative", "Thank You", "Hi!",
				  "Negative", "Sorry!", "Help Me"},
				 {false, false, false, false, false, false}},
				{"Cooperate", false,
				 {"Follow Me", "Cover Me", "Let Me Through",
				  "Stay Here", "Boost Me Up", "Help Me Build"},
				 {false, false, false, false, false, false}},
				kPointPage,
			};

			// Offered only while the crosshair is on an enemy. The taunt goes out on
			// global chat so the whole server reads it, addressed to the player it was
			// aimed at; Point, as on a teammate, is one flip away.
			const PageDef kEnemyPages[] = {
				// A taunt goes to the whole server, at the player it was aimed at;
				// a team marker has no business carrying one.
				{"Taunt", true,
				 {"I See You", "Nice Try", "Miss Me?",
				  "Too Easy", "Behind You...", "Say Goodbye"},
				 {false, false, false, false, false, false}},
				kPointPage,
			};
		} // namespace

		PieMenuView::PieMenuView(Client* c, IFont* f, IFont* big)
			: renderer(c->GetRenderer()), font(f), bigFont(big) {
			auto buildPages = [](const PageDef* defs, size_t count) {
				std::vector<Page> pages;
				pages.reserve(count);
				for (size_t i = 0; i < count; i++) {
					const PageDef& def = defs[i];
					Page p;
					p.name = def.name;
					p.global = def.global;
					for (int s = 0; s < kSliceCount; s++) {
						p.labels[static_cast<size_t>(s)] = def.labels[s];
						p.pings[static_cast<size_t>(s)] = def.pings[s];
					}
					pages.push_back(std::move(p));
				}
				return pages;
			};

			worldPages = buildPages(kWorldPages, std::size(kWorldPages));
			teammatePages = buildPages(kTeammatePages, std::size(kTeammatePages));
			enemyPages = buildPages(kEnemyPages, std::size(kEnemyPages));

			const float halfSliceRad = kSliceSpan * 0.5F - DEG2RAD(kSliceGapDeg) * 0.5F;
			for (int i = 0; i < kSliceCount; i++) {
				float center = -kHalfPi + kSliceSpan * static_cast<float>(i);
				sliceCenterAngles[i] = center;
				float t1 = center - halfSliceRad;
				float t2 = center + halfSliceRad;
				sliceRays[i] = {sinf(t1), cosf(t1), sinf(t2), cosf(t2)};
			}
		}

		PieMenuView::~PieMenuView() {}

		const PieMenuView::Page& PieMenuView::CurrentPage() const {
			const auto& pages = CurrentPages();
			SPAssert(!pages.empty());
			size_t idx = static_cast<size_t>(std::max(0, page));
			if (idx >= pages.size())
				idx = pages.size() - 1;
			return pages[idx];
		}

		void PieMenuView::RestorePage() {
			int remembered = lastPage[static_cast<size_t>(variant)];
			page = std::max(0, std::min(remembered, GetPageCount() - 1));
		}

		void PieMenuView::Open(Variant v, int tgtId) {
			open = true;
			variant = v;
			targetPlayerId = tgtId;
			cursor = {0.0F, 0.0F};
			selection = None;
			openPhase = 0.0F;
			pagePhase = 1.0F;
			highlight.fill(0.0F);
			RestorePage();
		}

		int PieMenuView::Close() {
			int result = selection;
			open = false;
			selection = None;
			targetPlayerId = -1;
			cursor = {0.0F, 0.0F};
			page = 0;
			openPhase = 0.0F;
			highlight.fill(0.0F);
			return result;
		}

		void PieMenuView::CyclePage(int dir) {
			if (!open || dir == 0)
				return;

			int count = GetPageCount();
			if (count <= 1)
				return;

			page = ((page + dir) % count + count) % count;
			lastPage[static_cast<size_t>(variant)] = page;
			pagePhase = 0.0F;
			hintNeeded = false;
		}

		void PieMenuView::Update(float dt) {
			if (!open)
				return;

			constexpr float kOpenRate = 1.0F / 0.12F;
			constexpr float kPageRate = 1.0F / 0.10F;
			constexpr float kHighlightUpRate = 1.0F / 0.10F;
			constexpr float kHighlightDownRate = 1.0F / 0.15F;

			openPhase = std::min(1.0F, openPhase + dt * kOpenRate);
			pagePhase = std::min(1.0F, pagePhase + dt * kPageRate);
			hintTime += dt;

			for (int i = 0; i < kSliceCount; i++) {
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
			if (selection < 0 || selection >= kSliceCount)
				return empty;
			return CurrentPage().labels[static_cast<size_t>(selection)];
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
				angle += kTwoPi;

			// kSliceCount equal slices, top slice centered at angle 0.
			int idx = static_cast<int>(floorf((angle + kSliceSpan * 0.5F) / kSliceSpan)) % kSliceCount;
			selection = idx;
		}

		void PieMenuView::Draw() {
			if (!open)
				return;

			float sw = renderer.ScreenWidth();
			float sh = renderer.ScreenHeight();
			
			Vector2 center = {sw * 0.5F, sh * 0.5F};

			const auto& labels = CurrentPage().labels;

			// Ease-out open animation: scale from 0.85 → 1.0, alpha from 0 → 1.
			float eased = 1.0F - (1.0F - openPhase) * (1.0F - openPhase);
			float scale = 0.85F + 0.15F * eased;
			float alpha = eased;

			// Labels fade back in after a ring flip so the swap reads as a change.
			float easedPage = 1.0F - (1.0F - pagePhase) * (1.0F - pagePhase);

			float rInner = kRingInner * scale;
			float rOuter = kRingOuter * scale;
			float rLabel = kLabelRadius * scale;

			// Backing disc
			DrawDiscFill(renderer, MakeVector4(0, 0, 0, 0.55F * alpha), center, rOuter);

			// A highlighted slice reaches past the others, so how far out it goes
			// is its own and is asked for in both passes below.
			auto sliceOuter = [&](int i) { return rOuter + kHighlightReach * highlight[i]; };

			// Slices
			for (int i = 0; i < kSliceCount; i++) {
				float h = highlight[i];
				float fillA = (0.08F + (0.85F - 0.08F) * h) * alpha;
				const SliceRay& rr = sliceRays[i];
				DrawSliceFill(renderer, MakeVector4(fillA, fillA, fillA, fillA), center, rInner,
							  sliceOuter(i), rr.s1, rr.c1, rr.s2, rr.c2);
			}

			// Outer and inner outline rings (thin). The outer one is drawn slice by
			// slice, along each one's own edge: a single ring across the whole pie
			// would sit at the resting radius and cut over the top of whichever
			// slice had reached past it.
			const float outlineA = alpha * 0.5F;
			const Vector4 outlineColor = MakeVector4(outlineA, outlineA, outlineA, outlineA);
			for (int i = 0; i < kSliceCount; i++) {
				const SliceRay& rr = sliceRays[i];
				float rOutSlice = sliceOuter(i);
				DrawSliceFill(renderer, outlineColor, center, rOutSlice - 1.0F, rOutSlice,
							  rr.s1, rr.c1, rr.s2, rr.c2);
			}
			DrawAnnulusFill(renderer, outlineColor, center, rInner, rInner + 1.0F);

			// Labels at kLabelRadius along each slice center
			for (int i = 0; i < kSliceCount; i++) {
				float h = highlight[i];
				Vector2 dir = {cosf(sliceCenterAngles[i]), sinf(sliceCenterAngles[i])};
				Vector2 p = center + dir * rLabel;

				const std::string& label = labels[i];
				Vector2 sz = font->Measure(label);
				Vector2 textPos = {p.x - sz.x * 0.5F, p.y - sz.y * 0.5F};

				float textA = (0.85F + 0.15F * h) * alpha * easedPage;
				Vector4 textColor = MakeVector4(1, 1, 1, textA);
				Vector4 textShadow = MakeVector4(0, 0, 0, 0.6F * textA);
				font->DrawShadow(label, textPos, 1.0F, textColor, textShadow);
			}

			// Center readout: currently selected slice label, scaled by its highlight
			if (selection >= 0 && selection < kSliceCount && bigFont) {
				float h = highlight[selection];
				const std::string& centerLabel = labels[selection];
				Vector2 sz = bigFont->Measure(centerLabel);
				Vector2 pos = {center.x - sz.x * 0.5F, center.y - sz.y * 0.5F};
				float a = h * alpha * easedPage;
				Vector4 col = MakeVector4(a, a, a, a);
				Vector4 shd = MakeVector4(0, 0, 0, 0.7F * a);
				bigFont->DrawShadow(centerLabel, pos, 1.0F, col, shd);
			} else if (font) {
				// Nothing aimed at yet: name the ring instead, so which one is up
				// can be read at a glance rather than inferred from six labels.
				const std::string& pageName = CurrentPage().name;
				Vector2 sz = font->Measure(pageName);
				Vector2 pos = {center.x - sz.x * 0.5F, center.y - sz.y * 0.5F};
				float a = alpha * easedPage * 0.55F;
				font->DrawShadow(pageName, pos, 1.0F, MakeVector4(1, 1, 1, a),
								 MakeVector4(0, 0, 0, 0.6F * a));
			}

			DrawPageIndicator(center, rOuter, alpha);
		}

		void PieMenuView::DrawPageIndicator(Vector2 center, float rOuter, float alpha) {
			int pageCount = GetPageCount();
			if (pageCount <= 1)
				return;

			constexpr float kDotRadius = 3.0F;
			constexpr float kDotSpacing = 13.0F;
			constexpr float kDotOffset = 16.0F;

			// Pulses only until the player finds the flip, then settles into a
			// quiet position readout that never asks for attention again.
			float pulse = 0.75F + 0.25F * sinf(hintTime * 4.0F);
			float indicatorA = alpha * (hintNeeded ? pulse : 0.4F);

			float dotY = center.y + rOuter + kDotOffset;
			float dotX = center.x - kDotSpacing * static_cast<float>(pageCount - 1) * 0.5F;
			for (int i = 0; i < pageCount; i++) {
				bool active = (i == page);
				float a = indicatorA * (active ? 0.95F : 0.35F);
				DrawDiscFill(renderer, MakeVector4(a, a, a, a),
							 MakeVector2(dotX + kDotSpacing * static_cast<float>(i), dotY),
							 active ? kDotRadius : kDotRadius - 1.0F);
			}

			if (!hintNeeded || !font)
				return;

			std::string hint = _Tr("Client", "{0} More", ShortKeyName(cg_keyAltAttack));
			Vector2 sz = font->Measure(hint);
			Vector2 pos = {center.x - sz.x * 0.5F, dotY + kDotRadius + 6.0F};
			float a = alpha * pulse;
			font->DrawShadow(hint, pos, 1.0F, MakeVector4(1, 1, 1, a),
							 MakeVector4(0, 0, 0, 0.6F * a));
		}
	} // namespace client
} // namespace spades
