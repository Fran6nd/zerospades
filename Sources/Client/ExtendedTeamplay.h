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

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include <Core/Math.h>
#include <Core/TMPUtils.h>

namespace spades {
	namespace client {

		/**
		 * Client-side state of the *Extended Teamplay* protocol extension
		 * (extension id 2, packet id 66).
		 *
		 * The extension lets a server permit a set of optional teamplay features, relays
		 * in-world team pings, and lets the server reveal a chosen player through walls.
		 * This class owns everything the extension makes the client remember: the feature
		 * bitmask the server announced, the pings currently on screen, and the ESP marks
		 * currently in force. It holds no rendering or networking concerns — `NetClient`
		 * feeds it, `Client` ticks it, and the draw code reads it.
		 *
		 * Both the ping lifetime and the mark lifetime are expired here rather than by the
		 * server: the protocol has no removal sub-packet other than a Duration `0` mark.
		 */
		class ExtendedTeamplay {
		public:
			/** Feature bits carried by the Config sub-packet. Bits 3-7 are reserved and
			 * must be ignored, so an unknown bit never enables anything. */
			enum Feature : uint8_t {
				/** The client may render teammates through walls. */
				FeatureTeamESP = 1 << 0,
				/** The client may render received pings as 3D markers in the world. */
				FeaturePingWorld = 1 << 1,
				/** The client may render received pings on the minimap. */
				FeaturePingMinimap = 1 << 2,
			};

			/** Flag bits carried by the ESP Mark sub-packet. */
			enum MarkFlag : uint8_t {
				/** The mark ends when the marked player dies. */
				MarkFlagClearOnDeath = 1 << 0,
			};

			/** Player ID a Ping carries when the server originated it itself. */
			static constexpr int kServerPlayerId = 255;

			/** How long a received Ping stays on screen. Fixed at 5 seconds by the
			 * specification, so it is deliberately not a setting. */
			static constexpr float kPingLifetime = 5.0F;

			/** Duration value that marks a player until the server clears the mark. */
			static constexpr uint8_t kMarkDurationUntilCleared = 255;

			/** The reason strings are free-form UTF-8 and the protocol assigns no fixed
			 * values, so a server may send anything. Both caps only bound what this
			 * client is willing to keep; longer strings are truncated on a codepoint
			 * boundary rather than rejected. */
			static constexpr size_t kMaxReasonBytes = 64;

			struct Ping {
				/** The player who pinged, or `kServerPlayerId` for a server-origin ping. */
				int playerId;
				Vector3 position;
				std::string reason;
				/** Seconds until the ping disappears. */
				float timeLeft;
			};

			struct Mark {
				std::string reason;
				/** Seconds until the mark expires. Meaningless when `untilCleared`. */
				float timeLeft;
				/** The mark lasts until the server clears it (Duration `255`). */
				bool untilCleared;
				/** The mark ends when the marked player dies. */
				bool clearOnDeath;
			};

			/** Pings currently on screen, keyed by the player who originated them: the
			 * specification allows one live ping per player, a newer one replacing it and
			 * restarting its timer. */
			using PingMap = std::unordered_map<int, Ping>;

			ExtendedTeamplay() = default;

			/** Applies a Config sub-packet. Reserved bits are dropped here so no other
			 * code has to know which bits are defined. */
			void SetFeatures(uint8_t features);
			uint8_t GetFeatures() const { return features; }

			bool IsTeamESPEnabled() const { return (features & FeatureTeamESP) != 0; }
			bool IsWorldPingEnabled() const { return (features & FeaturePingWorld) != 0; }
			bool IsMinimapPingEnabled() const { return (features & FeaturePingMinimap) != 0; }

			/** Whether the client may send a Ping at all. With both ping bits clear the
			 * client must not send any, and the server would drop them anyway. */
			bool CanSendPing() const { return IsWorldPingEnabled() || IsMinimapPingEnabled(); }

			/** Records a relayed Ping, replacing any live ping from the same player and
			 * restarting its timer. */
			void AddPing(int playerId, const Vector3& position, std::string reason);

			/** Applies an ESP Mark sub-packet. A Duration of `0` clears the player's mark;
			 * any other value replaces the previous mark and restarts its timer. */
			void SetMark(int playerId, uint8_t duration, uint8_t flags, std::string reason);

			/** The mark in force for `playerId`, or empty when the player is not marked. */
			stmp::optional<const Mark&> GetMark(int playerId) const;

			const PingMap& GetPings() const { return pings; }
			bool HasPings() const { return !pings.empty(); }
			bool HasMarks() const { return !marks.empty(); }

			/** Drops the marks of a player who just died and had `CLEAR_ON_DEATH` set.
			 * A mark without that flag survives death and respawn. */
			void PlayerDied(int playerId);

			/** Drops everything belonging to a player who left the server. */
			void PlayerLeft(int playerId);

			/** Expires pings and marks. `dt` is real time, so a paused demo does not
			 * expire anything. */
			void Update(float dt);

			/** Clears all state without touching the feature bitmask. Used on a map
			 * change, where marks and pings are dropped but the server's policy stands
			 * until it sends a new Config. */
			void ClearTransientState();

			/** Clears all state including the feature bitmask. Used when the connection
			 * itself goes away. */
			void Reset();

			/** Truncates a reason string to `kMaxReasonBytes` on a UTF-8 codepoint
			 * boundary. Exposed for the packet parser, which applies it on receipt. */
			static std::string TruncateReason(std::string reason);

		private:
			uint8_t features = 0;
			PingMap pings;
			std::unordered_map<int, Mark> marks;
		};

	} // namespace client
} // namespace spades
