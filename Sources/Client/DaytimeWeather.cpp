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

#include "DaytimeWeather.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <Core/Debug.h>

#include "NetProtocol.h"

namespace spades {
	namespace client {
		namespace {
			/** The time of day at which the sun is highest: noon. */
			constexpr float kNoonMinutes = 720.0F;

			/** How many degrees the sun turns per minute: one turn a day. */
			constexpr float kSunDegreesPerMinute = 360.0F / kMinutesPerDay;

			/** The daylight of the night, while the sun is below the horizon. */
			constexpr float kNightDaylight = 0.1F;

			void WriteUShort(std::vector<char>& data, int value) {
				data.push_back(static_cast<char>(value & 0xff));
				data.push_back(static_cast<char>((value >> 8) & 0xff));
			}

			/** The angle the sun has turned since noon at `minutes`, in radians. */
			float SunAngleSinceNoon(float minutes) {
				return DEG2RAD((minutes - kNoonMinutes) * kSunDegreesPerMinute);
			}
		} // namespace

		void DaytimeClock::Set(int minutes, int speed, double now) {
			// The extension allows `0`-`1439`; wrap anything else rather than trust it.
			sky = Sky{static_cast<float>(minutes % kMinutesPerDay), speed, now};
		}

		float DaytimeClock::GetMinutes(double now) const {
			SPAssert(sky);

			// Speed is in game minutes per real minute, and the clock is in seconds.
			const double elapsed = std::max(now - sky->setAt, 0.0) / 60.0;
			const double minutes = sky->minutes + elapsed * sky->speed;
			return static_cast<float>(std::fmod(minutes, static_cast<double>(kMinutesPerDay)));
		}

		void ApplyDaytimeWeatherPacket(NetPacketReader& r, DaytimeClock& clock, double now) {
			SPADES_MARK_FUNCTION();

			if (!HasSubPacketBytes(r, 1, "Daytime and Weather"))
				return;

			switch (r.ReadByte()) { // sub packet id
				case DaytimeWeatherSubSky: {
					if (!HasSubPacketBytes(r, kDaytimeWeatherSkyBytes, "Daytime and Weather"))
						break;

					int minutes = r.ReadShort();
					int speed = r.ReadShort();
					// Weather is unimplemented in version 1 of the extension.
					r.ReadShort();

					clock.Set(minutes, speed, now);
				} break;
				default:
					// An unknown sub packet belongs to a newer version of the extension.
					SPLog("Ignoring an unknown Daytime and Weather sub packet");
					break;
			}
		}

		std::vector<char> EncodeDaytimeWeatherSky(int minutes, int speed) {
			std::vector<char> data{static_cast<char>(PacketTypeDaytimeWeather),
			                       static_cast<char>(DaytimeWeatherSubSky)};
			WriteUShort(data, minutes);
			WriteUShort(data, speed);
			WriteUShort(data, 0); // weather
			return data;
		}

		Vector3 GetSunDirection(float minutes) {
			// The sun turns around the axis `(0, 1, -1)` and is at `(0, -1, -1)` at noon.
			// The axis is perpendicular to the noon direction, so the sun follows the
			// great circle through it and `(-1, 0, 0)` (west, where it sets).
			const float angle = SunAngleSinceNoon(minutes);
			const float upAndNorth = -cosf(angle) * kInvSqrt2;
			return MakeVector3(-sinf(angle), upAndNorth, upAndNorth);
		}

		float GetDaylight(float minutes) {
			return std::max(kNightDaylight, Clamp(2.0F * cosf(SunAngleSinceNoon(minutes)), 0.0F, 1.0F));
		}
	} // namespace client
} // namespace spades
