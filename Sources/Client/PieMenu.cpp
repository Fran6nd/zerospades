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

#include "PieMenu.h"

#include <cmath>
#include <cstdlib>
#include <iterator>

#include <Core/CP437.h>
#include <Core/Math.h>
#include <Core/Strings.h>

namespace spades {
	namespace client {

		namespace {
			// Ring definitions. Slice order is top, then clockwise; Affirmative and
			// Negative occupy the same slots in every context so the gesture transfers.
			struct PieDef {
				std::uint8_t contexts;
				const char* theme;
				PieAction action;
				bool global;
				// What each slice draws. The Text it sends is built from it by
				// `textFormat`, where `{}` stands for the label.
				const char* labels[kPieSliceCount];
				const char* textFormat;
			};

			constexpr std::uint8_t kPlayerContexts = PieContextTeammate | PieContextEnemy;

			// Layout rule, held across every ring: slice 0 is at the top and the
			// rest follow clockwise, the vertical axis carries a pair of opposites,
			// the right half is about them and the left half is about us.
			//
			// The ring a message sits on is what says how it travels, so nobody has
			// to remember it slice by slice: the first ring points at a place, the
			// second one talks, the third one calls the game.
			//
			// A ring set is content rather than chrome — a server may supply its own,
			// in whatever language its players speak — so none of it goes through the
			// translation catalogue.
			//
			// Pies are offered in the order they are defined within each context, so
			// Point is defined twice: first on terrain, and last on a player, one flip
			// back from the first ring, so calling a spot never needs the crosshair
			// taken off a player first.
			const PieDef kPieDefs[] = {
			  // Every slice names somewhere, so every slice drops a marker there: on
			  // the ground, or on the player the crosshair was on, where the ray met
			  // them. A server without the extension gets the same words on team chat.
			  {PieContextWorld, "Point", PieAction::Ping, false,
			   {"Enemy Here!", "Tear It Down!", "Watch This Spot",
				"Go Here!", "Let's Dig Here", "Help Me Build"},
			   "{}"},
			  // Said to the team, about nowhere in particular: a marker would only
			  // put "thank you" on a piece of ground.
			  {PieContextWorld, "Social", PieAction::Chat, false,
			   {"Affirmative", "Thank You", "Hi!",
				"Negative", "Sorry!", "Help Me"},
			   "{}"},
			  // The objective rather than the ground: where the intel is and what to
			  // do about it is already known to everyone, so none of it points.
			  {PieContextWorld, "Tactics", PieAction::Chat, false,
			   {"Attack!", "Get the Intel!", "Enemy Has the Intel!",
				"Fall Back!", "Regroup on Me", "Defend the Intel!"},
			   "{}"},
			  // Offered while the crosshair is on a teammate, and sent to them alone.
			  // A person is not a place, so their own rings do not point; the rings
			  // keep the order they have on terrain, and Social keeps the same six
			  // words, so the gesture is the same whoever it is aimed at.
			  //
			  // Directions are relative to the teammate under the crosshair, which
			  // makes them exact in a way a broadcast "our right" cannot be. Up, down,
			  // left and right sit where they point; the two threats that have no
			  // direction take the lower corners.
			  {PieContextTeammate, "Warn", PieAction::Command, false,
			   {"Above You!", "On Your Right!", "Behind You!",
				"Below You!", "Sniper on You!", "On Your Left!"},
			   "/pm %p {}"},
			  {PieContextTeammate, "Social", PieAction::Command, false,
			   {"Affirmative", "Thank You", "Hi!",
				"Negative", "Sorry!", "Help Me"},
			   "/pm %p {}"},
			  {PieContextTeammate, "Cooperate", PieAction::Command, false,
			   {"Follow Me", "Cover Me", "Let Me Through",
				"Stay Here", "Boost Me Up", "Help Me Build"},
			   "/pm %p {}"},
			  // A taunt goes to the whole server: servers do not carry private
			  // messages across teams.
			  {PieContextEnemy, "Taunt", PieAction::Chat, true,
			   {"I See You", "Nice Try", "Miss Me?",
				"Too Easy", "Behind You...", "Say Goodbye"},
			   "{}"},
			  {kPlayerContexts, "Point", PieAction::Ping, false,
			   {"Enemy Here!", "Tear It Down!", "Watch This Spot",
				"Go Here!", "Let's Dig Here", "Help Me Build"},
			   "{}"},
			};

			constexpr size_t kMaxPies = 10;
			constexpr size_t kMaxThemeBytes = 32;
			constexpr size_t kMaxLabelBytes = 48;
			constexpr size_t kMaxTextBytes = 255;
			constexpr size_t kMaxTooltipBytes = 128;

			constexpr std::uint8_t kKnownContexts =
			  PieContextWorld | PieContextTeammate | PieContextEnemy | PieContextSpectator;
			constexpr std::uint8_t kSliceFlagGlobal = 1 << 0;

			/** Walks the packet, failing rather than reading past its end. */
			class MenuReader {
				const std::string& data;
				size_t pos = 0;

			public:
				std::string error;

				explicit MenuReader(const std::string& d) : data(d) {}

				bool AtEnd() const { return pos == data.size(); }

				bool Byte(std::uint8_t& out) {
					if (pos >= data.size()) {
						error = "a record runs past the end of the packet";
						return false;
					}
					out = static_cast<std::uint8_t>(data[pos++]);
					return true;
				}

				/** A NUL-terminated string, at most `maxBytes` before its NUL, a
				 * `0xff` prefix included. It is Code Page 437, or UTF-8 behind the
				 * prefix, and comes out as UTF-8. */
				bool String(std::string& out, size_t maxBytes, const char* what) {
					auto end = data.find('\0', pos);
					if (end == std::string::npos) {
						error = Format("a {0} runs past the end of the packet", what);
						return false;
					}
					std::string raw = data.substr(pos, end - pos);
					pos = end + 1;
					if (raw.size() > maxBytes) {
						error = Format("a {0} of {1} bytes is longer than {2}", what,
									   static_cast<int>(raw.size()), static_cast<int>(maxBytes));
						return false;
					}
					// A control character would be sent in a chat line that goes on
					// past a newline, or drawn as nothing; the protocol lets a client
					// refuse a menu outright, so it does.
					for (char c : raw) {
						auto u = static_cast<unsigned char>(c);
						if (u < 0x20 || u == 0x7F) {
							error = Format("a {0} holds a control character", what);
							return false;
						}
					}
					if (!raw.empty() && static_cast<unsigned char>(raw[0]) == 0xFF) {
						out = raw.substr(1);
						if (!IsWellFormedUTF8(out)) {
							error = Format("a {0} is not well-formed UTF-8", what);
							return false;
						}
					} else {
						out = CP437::Decode(raw);
					}
					return true;
				}
			};

			// Labels never hold a `%`, so they can go into the Text as they are.
			std::string FormatText(const char* format, const std::string& label) {
				std::string out = format;
				auto pos = out.find("{}");
				if (pos != std::string::npos)
					out.replace(pos, 2, label);
				return out;
			}
		} // namespace

		bool operator==(const PieSlice& a, const PieSlice& b) {
			return a.action == b.action && a.global == b.global && a.text == b.text &&
				   a.label == b.label && a.tooltip == b.tooltip;
		}

		bool operator==(const Pie& a, const Pie& b) {
			return a.contexts == b.contexts && a.theme == b.theme && a.slices == b.slices;
		}

		std::vector<Pie> PieMenu::PiesFor(PieContext context) const {
			std::vector<Pie> out;
			for (const Pie& pie : pies)
				if (pie.contexts & context)
					out.push_back(pie);
			return out;
		}

		namespace {
			// A number with a fixed count of decimals and a `.` whatever the locale,
			// which printf would follow.
			std::string Fixed(float value, int decimals) {
				long scale = 1;
				for (int i = 0; i < decimals; i++)
					scale *= 10;
				long n = std::lround(static_cast<double>(value) * static_cast<double>(scale));
				std::string out = n < 0 ? "-" : "";
				n = std::labs(n);
				std::string frac = std::to_string(n % scale);
				frac.insert(0, static_cast<size_t>(decimals) - frac.size(), '0');
				return out + std::to_string(n / scale) + "." + frac;
			}
		} // namespace

		std::string ExpandPieText(const std::string& text, int targetPlayerId,
								  const PieAim& aim) {
			std::string out;
			out.reserve(text.size());

			for (size_t i = 0; i < text.size(); i++) {
				if (text[i] != '%' || i + 1 >= text.size()) {
					out += text[i];
					continue;
				}

				char token = text[i + 1];
				if (token == 'p') {
					if (targetPlayerId >= 0)
						out += "#" + std::to_string(targetPlayerId);
				} else if (token == 'b') {
					if (aim.block)
						out += std::to_string(aim.block->x) + " " + std::to_string(aim.block->y) +
							   " " + std::to_string(aim.block->z);
				} else if (token == 'r') {
					out += Fixed(aim.origin.x, 2) + " " + Fixed(aim.origin.y, 2) + " " +
						   Fixed(aim.origin.z, 2) + " " + Fixed(aim.direction.x, 4) + " " +
						   Fixed(aim.direction.y, 4) + " " + Fixed(aim.direction.z, 4);
				} else if (token == '%') {
					out += '%';
				} else {
					// Reserved: both characters stay as they are.
					out += '%';
					continue;
				}
				i++;
			}

			return out;
		}

		stmp::optional<PieMenu> ParsePieMenu(const std::string& data, std::string& error) {
			MenuReader r(data);
			PieMenu menu;

			auto fail = [&]() -> stmp::optional<PieMenu> {
				error = r.error;
				return {};
			};

			std::uint8_t pieCount;
			if (!r.Byte(pieCount))
				return fail();
			if (pieCount > kMaxPies) {
				r.error = Format("a Pie Count of {0} is more than {1}",
								 static_cast<int>(pieCount), static_cast<int>(kMaxPies));
				return fail();
			}

			menu.pies.resize(pieCount);
			for (Pie& pie : menu.pies) {
				std::uint8_t contexts, themeMessageId;
				if (!r.Byte(contexts) || !r.Byte(themeMessageId) ||
					!r.String(pie.theme, kMaxThemeBytes, "Theme"))
					return fail();

				// Reserved bits are ignored, never a reason to refuse.
				pie.contexts = contexts & kKnownContexts;

				for (PieSlice& slice : pie.slices) {
					std::uint8_t action, messageId, flags;
					if (!r.Byte(action) || !r.Byte(messageId) || !r.Byte(flags) ||
						!r.String(slice.text, kMaxTextBytes, "Text") ||
						!r.String(slice.label, kMaxLabelBytes, "Label") ||
						!r.String(slice.tooltip, kMaxTooltipBytes, "Tooltip"))
						return fail();

					// An unknown action is read as one that does nothing.
					slice.action = action <= static_cast<std::uint8_t>(PieAction::Command)
									 ? static_cast<PieAction>(action)
									 : PieAction::None;
					slice.global = (flags & kSliceFlagGlobal) != 0;
				}
			}

			if (!r.AtEnd()) {
				r.error = "bytes remain after the last pie";
				return fail();
			}

			return menu;
		}

		PieMenu BuiltInPieMenu() {
			PieMenu menu;
			menu.pies.reserve(std::size(kPieDefs));

			for (const PieDef& def : kPieDefs) {
				Pie pie;
				pie.contexts = def.contexts;
				pie.theme = def.theme;
				for (int s = 0; s < kPieSliceCount; s++) {
					PieSlice& slice = pie.slices[static_cast<size_t>(s)];
					slice.action = def.action;
					slice.global = def.global;
					slice.text = FormatText(def.textFormat, def.labels[s]);
					// A label is only worth carrying where the Text says more than it.
					if (slice.text != def.labels[s])
						slice.label = def.labels[s];
				}
				menu.pies.push_back(std::move(pie));
			}

			return menu;
		}
	} // namespace client
} // namespace spades
