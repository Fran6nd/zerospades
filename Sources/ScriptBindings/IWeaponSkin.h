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

#pragma once

#include "ScriptFunction.h"
#include <Core/Math.h>

namespace spades {
	namespace client {

		class ScriptIWeaponSkin {
			asIScriptObject* obj;

		public:
			ScriptIWeaponSkin(asIScriptObject* obj);
			bool ImplementsInterface();
			void SetReadyState(float);
			void SetAimDownSightState(float);
			void SetReloading(bool);
			void SetReloadProgress(float);
			void SetAmmo(int);
			void SetClipSize(int);
			void WeaponFired();
			void ReloadingWeapon();
			void ReloadedWeapon();
		};

		class ScriptIWeaponSkin2 {
			asIScriptObject* obj;

		public:
			ScriptIWeaponSkin2(asIScriptObject* obj);
			bool ImplementsInterface();
			void SetSoundEnvironment(float room, float size, float distance);
			void SetSoundOrigin(Vector3);
		};

		class ScriptIWeaponSkin3 {
			asIScriptObject* obj;

		public:
			ScriptIWeaponSkin3(asIScriptObject* obj);
			bool ImplementsInterface();
			Vector3 GetMuzzlePosition();
			Vector3 GetCaseEjectPosition();
		};

		/** Optional, for a skin that reacts to a reload ending without completing:
		 * a death, a shot interrupting a slow reload, or the server ending it,
		 * e.g. on a tool change. The client never aborts a reload on a tool change
		 * by itself; a server that does so answers with a WeaponReload taking
		 * nothing from the reserve, its ammo is applied and the reload counts as
		 * aborted if its animation was still playing. Told how far through its
		 * animation the reload was; returns whether the engine should fade out the
		 * sounds the skin played from ReloadingWeapon(). Later
		 * reload-lifecycle callbacks belong here rather than in a new interface. */
		class ScriptIWeaponSkin4 {
			asIScriptObject* obj;

		public:
			ScriptIWeaponSkin4(asIScriptObject* obj);
			bool ImplementsInterface();
			bool ReloadAborted(float progress);
		};
	} // namespace client
} // namespace spades