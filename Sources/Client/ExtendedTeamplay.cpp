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

#include <utility>

#include "ExtendedTeamplay.h"
#include <Core/Debug.h>

namespace spades {
	namespace client {
		namespace {
			/** Every feature bit this client knows about. The specification reserves the
			 * remaining bits and requires unknown ones to be ignored, so masking here
			 * means a future server can set them without changing behaviour. */
			constexpr uint8_t kKnownFeatures =
			  ExtendedTeamplay::FeatureTeamESP | ExtendedTeamplay::FeaturePingWorld |
			  ExtendedTeamplay::FeaturePingMinimap;

			/** Likewise for the ESP Mark flags. */
			constexpr uint8_t kKnownMarkFlags = ExtendedTeamplay::MarkFlagClearOnDeath;

			bool IsUTF8Continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }
		} // namespace

		std::string ExtendedTeamplay::SanitizeReason(std::string reason) {
			reason = TrimSpaces(StripNewlines(reason));

			if (reason.size() > kMaxReasonBytes) {
				// Back off to the start of the codepoint that straddles the cap, so the
				// result is never a half-encoded character. A codepoint is at most 4
				// bytes, so this walks back 3 bytes at the very most.
				size_t end = kMaxReasonBytes;
				while (end > 0 && IsUTF8Continuation(reason[end]))
					end--;

				reason.resize(end);

				// Cutting mid-phrase can leave the space that preceded the dropped word.
				reason = TrimSpaces(reason);
			}

			return reason;
		}

		void ExtendedTeamplay::SetFeatures(uint8_t newFeatures) {
			SPADES_MARK_FUNCTION();

			features = newFeatures & kKnownFeatures;

			// A ping the client sent before this arrived may still be in flight; the
			// server drops it, which is not an error. Pings already on screen are kept:
			// the specification only gates whether new ones may be sent and drawn, and
			// dropping live ones would make a policy change look like a glitch.
		}

		void ExtendedTeamplay::AddPing(int playerId, const Vector3& position,
		                               std::string reason) {
			SPADES_MARK_FUNCTION();

			Ping& ping = pings[playerId];
			ping.playerId = playerId;
			ping.position = position;
			ping.reason = std::move(reason);
			ping.timeLeft = kPingLifetime;
		}

		void ExtendedTeamplay::SetMark(int playerId, uint8_t duration, uint8_t flags,
		                               std::string reason) {
			SPADES_MARK_FUNCTION();

			if (duration == 0) { // clears the mark
				marks.erase(playerId);
				return;
			}

			Mark& mark = marks[playerId];
			mark.reason = std::move(reason);
			mark.untilCleared = (duration == kMarkDurationUntilCleared);
			mark.timeLeft = mark.untilCleared ? 0.0F : static_cast<float>(duration);
			mark.clearOnDeath = ((flags & kKnownMarkFlags) & MarkFlagClearOnDeath) != 0;
		}

		stmp::optional<const ExtendedTeamplay::Mark&>
		ExtendedTeamplay::GetMark(int playerId) const {
			auto it = marks.find(playerId);
			if (it == marks.end())
				return {};
			return it->second;
		}

		void ExtendedTeamplay::PlayerDied(int playerId) {
			auto it = marks.find(playerId);
			if (it != marks.end() && it->second.clearOnDeath)
				marks.erase(it);
		}

		void ExtendedTeamplay::PlayerLeft(int playerId) {
			marks.erase(playerId);
			pings.erase(playerId);
		}

		void ExtendedTeamplay::Update(float dt) {
			SPADES_MARK_FUNCTION();

			for (auto it = pings.begin(); it != pings.end();) {
				it->second.timeLeft -= dt;
				if (it->second.timeLeft <= 0.0F)
					it = pings.erase(it);
				else
					++it;
			}

			for (auto it = marks.begin(); it != marks.end();) {
				if (it->second.untilCleared) {
					++it;
					continue;
				}

				it->second.timeLeft -= dt;
				if (it->second.timeLeft <= 0.0F)
					it = marks.erase(it);
				else
					++it;
			}
		}

		void ExtendedTeamplay::ClearTransientState() {
			pings.clear();
			marks.clear();
		}

		void ExtendedTeamplay::Reset() {
			ClearTransientState();
			features = 0;
		}
	} // namespace client
} // namespace spades
