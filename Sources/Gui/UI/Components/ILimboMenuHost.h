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

#include <Client/GameConstants.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace client {
		class World;
		class IAudioDevice;
	} // namespace client
	namespace gui {
		class ILimboMenuHost : public RefCountedObject {
		public:
			virtual ~ILimboMenuHost() = default;
			virtual client::IAudioDevice* GetAudioDevice() = 0;
			virtual void PlayHoverSound() = 0;
			virtual void PlaySelectSound() = 0;
			virtual void OnTeamSelected(int team) = 0;
			virtual void OnWeaponSelected(WeaponType weapon) = 0;
			virtual void OnSpawnPressed() = 0;
			virtual void OnClosePressed() = 0;
			virtual bool HasLocalPlayer() = 0;
			virtual int GetLocalPlayerTeam() = 0;
			virtual client::World* GetWorld() = 0;
			virtual int GetSelectedTeam() = 0;
			virtual WeaponType GetSelectedWeapon() = 0;
		};
	} // namespace gui
} // namespace spades
