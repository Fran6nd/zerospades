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

#include <iterator>

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
			  // A taunt goes to the whole server, at the player it was aimed at:
			  // servers do not carry private messages across teams, so it goes out
			  // on global chat with the target's name on it.
			  {PieContextEnemy, "Taunt", PieAction::Chat, true,
			   {"I See You", "Nice Try", "Miss Me?",
				"Too Easy", "Behind You...", "Say Goodbye"},
			   "%n, {}"},
			  {kPlayerContexts, "Point", PieAction::Ping, false,
			   {"Enemy Here!", "Tear It Down!", "Watch This Spot",
				"Go Here!", "Let's Dig Here", "Help Me Build"},
			   "{}"},
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

		std::vector<Pie> PieMenu::PiesFor(PieContext context) const {
			std::vector<Pie> out;
			for (const Pie& pie : pies)
				if (pie.contexts & context)
					out.push_back(pie);
			return out;
		}

		std::string ExpandPieText(const std::string& text, int targetPlayerId,
								  const std::string& targetName, bool clientTokens) {
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
				} else if (token == '%') {
					out += '%';
				} else if (token == 'n' && clientTokens) {
					out += targetName;
				} else {
					// Reserved: both characters stay as they are.
					out += '%';
					continue;
				}
				i++;
			}

			return out;
		}

		PieMenu BuiltInPieMenu() {
			PieMenu menu;
			menu.clientTokens = true;
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
