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

#include <vector>

#include <Core/Math.h>
#include <Core/TMPUtils.h>

namespace spades {
	namespace client {
		class NetPacketReader;

		/** The length of a day in the *Daytime and Weather* extension, in minutes. */
		constexpr int kMinutesPerDay = 1440;

		/**
		 * The time of day the server set with the *Daytime and Weather* extension's Sky,
		 * advanced at its Speed from the moment the Sky arrived.
		 *
		 * The clock it is read with belongs to its owner: real seconds on a live
		 * connection, the playback time in a demo. It belongs to the connection, not to
		 * a world, so it lasts through Map Start.
		 */
		class DaytimeClock {
			struct Sky {
				float minutes; // since midnight, when the Sky arrived
				int speed;     // game minutes per real minute
				double setAt;  // the owner's clock when the Sky arrived
			};
			stmp::optional<Sky> sky;

		public:
			/** Takes a Sky's Time and Speed, as of `now`. */
			void Set(int minutes, int speed, double now);
			void Clear() { sky.reset(); }

			/** Whether a Sky arrived since the clock was created or cleared. */
			bool IsSet() const { return static_cast<bool>(sky); }

			/** Minutes since midnight at `now`, in `[0, kMinutesPerDay)`. Only with `IsSet`. */
			float GetMinutes(double now) const;

			/** The Speed of the last Sky. Only with `IsSet`. */
			int GetSpeed() const { return sky->speed; }
		};

		/**
		 * Applies one *Daytime and Weather* packet, received or replayed from a demo, to
		 * `clock` as of `now`. A truncated or unknown sub packet is ignored.
		 */
		void ApplyDaytimeWeatherPacket(NetPacketReader&, DaytimeClock& clock, double now);

		/** A whole Sky packet, packet id included, laid out as
		 * `ApplyDaytimeWeatherPacket` reads it. */
		std::vector<char> EncodeDaytimeWeatherSky(int minutes, int speed);

		/** The unit vector toward the sun at `minutes` since midnight, in map axes. */
		Vector3 GetSunDirection(float minutes);

		/**
		 * The daylight at `minutes` since midnight, in `[0.1, 1]`: the factor the world's
		 * lighting, the fog and the sky are drawn with. `1` is full daylight, and how
		 * the world looks without the extension; `0.1` is the night.
		 */
		float GetDaylight(float minutes);
	} // namespace client
} // namespace spades
