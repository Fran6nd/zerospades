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
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include "Client.h"
#include "Fonts.h"
#include "IAudioChunk.h"
#include "IAudioDevice.h"
#include "IFont.h"
#include "IImage.h"
#include "IRenderer.h"
#include "LimboView.h"
#include "World.h"
#include <Core/Strings.h>

namespace spades {
	namespace client {

		// TODO: make limbo view scriptable using the existing UI framework.

		LimboView::LimboView(Client* client) : client(client), renderer(client->GetRenderer()) {
			// layout now!
			float menuWidth = 200.0F;
			float menuHeight = menuWidth / 8.0F;
			float rowHeight = menuHeight + 3.0F;

			float sw = renderer.ScreenWidth();
			float sh = renderer.ScreenHeight();

			float contentsWidth = sw - 8.0F;
			float maxContentsWidth = 800.0F;
			if (contentsWidth > maxContentsWidth)
				contentsWidth = maxContentsWidth;

			float left = (sw - contentsWidth) * 0.5F;
			float top = sh - 150.0F;

			float teamX = left + 10.0F;
			float firstY = top + 35.0F;

			World* w = client->GetWorld();

			items.push_back(MenuItem(MenuTeam1,
				AABB2(teamX, firstY, menuWidth, menuHeight),
				w ? w->GetTeamName(0) : "Team 1"));
			items.push_back(MenuItem(MenuTeam2,
				AABB2(teamX, firstY + rowHeight, menuWidth, menuHeight),
				w ? w->GetTeamName(1) : "Team 2"));
			items.push_back(MenuItem(MenuTeamSpectator,
				AABB2(teamX, firstY + rowHeight * 2.0F, menuWidth, menuHeight),
				_Tr("Client", "Spectator")));

			float weapX = left + 260.0F;

			items.push_back(MenuItem(MenuWeaponRifle,
				AABB2(weapX, firstY, menuWidth, menuHeight),
				_Tr("Client", "Rifle")));
			items.push_back(MenuItem(MenuWeaponSMG,
				AABB2(weapX, firstY + rowHeight, menuWidth, menuHeight),
				_Tr("Client", "SMG")));
			items.push_back(MenuItem(MenuWeaponShotgun,
				AABB2(weapX, firstY + rowHeight * 2.0F, menuWidth, menuHeight),
				_Tr("Client", "Shotgun")));

			//! The "Spawn" button that you press when you're ready to "spawn".
			items.push_back(MenuItem(MenuSpawn,
				AABB2(left + contentsWidth - 166.0F, firstY + 4.0F, 156.0F, 64.0F),
				_Tr("Client", "Spawn")));

			items.push_back(MenuItem(MenuClose,
				AABB2(left + contentsWidth - 24.0F, top, 24.0F, 24.0F), "X"));

			selectedTeam = 2;
			selectedWeapon = RIFLE_WEAPON;
		}
		LimboView::~LimboView() {}

		void LimboView::Update(float dt) {
			// spectator team was actually 255
			if (selectedTeam > 2)
				selectedTeam = 2;

			Vector2 cursorPos = (client && client->GetCursor()) ? client->GetCursor()->GetPosition() : MakeVector2(0, 0);

			for (size_t i = 0; i < items.size(); i++) {
				MenuItem& item = items[i];
				item.visible = true;

				switch (item.type) {
					case MenuWeaponRifle:
					case MenuWeaponShotgun:
					case MenuWeaponSMG:
						if (selectedTeam >= 2)
							item.visible = false;
						break;
					case MenuClose:
						item.visible = client->HasLocalPlayer();
						break;
					default:;
				}

				bool newHover = item.rect && cursorPos;
				if (!item.visible)
					newHover = false;
				if (newHover && !item.hover) {
					IAudioDevice& dev = *client->audioDevice;
					Handle<IAudioChunk> c = dev.RegisterSound("Sounds/Feedback/Limbo/Hover.opus");
					dev.PlayLocal(c.GetPointerOrNull(), AudioParam());
				}
				item.hover = newHover;
			}
		}
	} // namespace client
} // namespace spades