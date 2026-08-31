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

#include "LimboMenu.h"
#include "SoftwareCursor.h"
#include <Gui/OverlayPaint.h>
#include <Gui/UIWidgetPainter.h>
#include <Client/IRenderer.h>
#include <Client/Fonts.h>
#include <Client/IAudioDevice.h>
#include <Client/IFont.h>
#include <Client/World.h>
#include <Core/Strings.h>

namespace spades {
	namespace gui {

		LimboMenu::LimboMenu(Handle<ILimboMenuHost> host, client::IRenderer& renderer,
							 client::FontManager& fontManager, SoftwareCursor& cursor,
							 client::IAudioDevice* audioDevice)
		    : host(host), renderer(&renderer), fontManager(&fontManager), cursor(cursor),
		      audioDevice(audioDevice) {}

		LimboMenu::~LimboMenu() {}

		void LimboMenu::Open() {
			if (!renderer || !fontManager || !host)
				return;

			prevSelectedItem = -1;
			items.clear();

			cachedLayout.isValid = false;
			RecalculateLayout();

			if (!cachedLayout.isValid)
				return;

			isActive = true;

			int preselectedTeam = host->GetPreselectedTeam();
			bool shouldPreselect = preselectedTeam >= 0;

			items.push_back(MenuItem(MenuItemType::Team1,
				AABB2(cachedLayout.teamX, cachedLayout.firstY, cachedLayout.menuWidth, cachedLayout.menuHeight),
				host->GetTeamName(0)));
			items.back().visible = !shouldPreselect;

			items.push_back(MenuItem(MenuItemType::Team2,
				AABB2(cachedLayout.teamX, cachedLayout.firstY + cachedLayout.rowHeight, cachedLayout.menuWidth, cachedLayout.menuHeight),
				host->GetTeamName(1)));
			items.back().visible = !shouldPreselect;

			items.push_back(MenuItem(MenuItemType::TeamSpectator,
				AABB2(cachedLayout.teamX, cachedLayout.firstY + cachedLayout.rowHeight * 2.0F, cachedLayout.menuWidth, cachedLayout.menuHeight),
				_Tr("Client", "Spectator")));
			items.back().visible = !shouldPreselect;

			items.push_back(MenuItem(MenuItemType::WeaponRifle,
				AABB2(cachedLayout.weapX, cachedLayout.firstY, cachedLayout.menuWidth, cachedLayout.menuHeight),
				_Tr("Client", "Rifle")));
			items.push_back(MenuItem(MenuItemType::WeaponSMG,
				AABB2(cachedLayout.weapX, cachedLayout.firstY + cachedLayout.rowHeight, cachedLayout.menuWidth, cachedLayout.menuHeight),
				_Tr("Client", "SMG")));
			items.push_back(MenuItem(MenuItemType::WeaponShotgun,
				AABB2(cachedLayout.weapX, cachedLayout.firstY + cachedLayout.rowHeight * 2.0F, cachedLayout.menuWidth, cachedLayout.menuHeight),
				_Tr("Client", "Shotgun")));

			items.push_back(MenuItem(MenuItemType::Spawn,
				AABB2(cachedLayout.left + cachedLayout.contentsWidth - 166.0F, cachedLayout.firstY + 4.0F, 156.0F, 64.0F),
				_Tr("Client", "Spawn")));

			items.push_back(MenuItem(MenuItemType::Close,
				AABB2(cachedLayout.left + cachedLayout.contentsWidth - 24.0F, cachedLayout.top, 24.0F, 24.0F), "X"));
		}

		void LimboMenu::Close() {
			isActive = false;
			items.clear();
		}

		void LimboMenu::RecalculateLayout() {
			if (!renderer || !fontManager)
				return;

			cachedLayout.screenWidth = renderer->ScreenWidth();
			cachedLayout.screenHeight = renderer->ScreenHeight();

			cachedLayout.menuWidth = 200.0F;
			cachedLayout.menuHeight = cachedLayout.menuWidth / 8.0F;
			cachedLayout.rowHeight = cachedLayout.menuHeight + 3.0F;

			cachedLayout.contentsWidth = cachedLayout.screenWidth - 8.0F;
			float maxContentsWidth = 800.0F;
			if (cachedLayout.contentsWidth > maxContentsWidth)
				cachedLayout.contentsWidth = maxContentsWidth;

			cachedLayout.left = (cachedLayout.screenWidth - cachedLayout.contentsWidth) * 0.5F;
			cachedLayout.top = cachedLayout.screenHeight - 150.0F;

			cachedLayout.teamX = cachedLayout.left + 10.0F;
			cachedLayout.weapX = cachedLayout.left + 260.0F;
			cachedLayout.firstY = cachedLayout.top + 35.0F;

			cachedLayout.isValid = true;
		}

		void LimboMenu::RecalculateItemRects() {
			float yPos = cachedLayout.firstY;
			for (size_t i = 0; i < items.size(); i++) {
				MenuItem& item = items[i];
				switch (item.type) {
					case MenuItemType::Team1:
						item.rect = AABB2(cachedLayout.teamX, yPos, cachedLayout.menuWidth, cachedLayout.menuHeight);
						yPos += cachedLayout.rowHeight;
						break;
					case MenuItemType::Team2:
						item.rect = AABB2(cachedLayout.teamX, yPos, cachedLayout.menuWidth, cachedLayout.menuHeight);
						yPos += cachedLayout.rowHeight;
						break;
					case MenuItemType::TeamSpectator:
						item.rect = AABB2(cachedLayout.teamX, yPos, cachedLayout.menuWidth, cachedLayout.menuHeight);
						yPos += cachedLayout.rowHeight;
						break;
					case MenuItemType::WeaponRifle:
						item.rect = AABB2(cachedLayout.weapX, cachedLayout.firstY, cachedLayout.menuWidth, cachedLayout.menuHeight);
						break;
					case MenuItemType::WeaponSMG:
						item.rect = AABB2(cachedLayout.weapX, cachedLayout.firstY + cachedLayout.rowHeight, cachedLayout.menuWidth, cachedLayout.menuHeight);
						break;
					case MenuItemType::WeaponShotgun:
						item.rect = AABB2(cachedLayout.weapX, cachedLayout.firstY + cachedLayout.rowHeight * 2.0F, cachedLayout.menuWidth, cachedLayout.menuHeight);
						break;
					case MenuItemType::Spawn:
						item.rect = AABB2(cachedLayout.left + cachedLayout.contentsWidth - 166.0F, cachedLayout.firstY + 4.0F, 156.0F, 64.0F);
						break;
					case MenuItemType::Close:
						item.rect = AABB2(cachedLayout.left + cachedLayout.contentsWidth - 24.0F, cachedLayout.top, 24.0F, 24.0F);
						break;
				}
			}
		}

		bool LimboMenu::KeyEvent(const std::string& key, bool down) {
			if (!isActive)
				return false;

			if (!down)
				return false;

			if (key == "LeftMouseButton") {
				int idx = MenuButtonAt(cursor.GetPosition());
				if (idx >= 0 && idx < static_cast<int>(items.size())) {
					host->PlaySelectSound();
					HandleMenuItemSelection(items[idx].type);
				}
				return true;
			}

			// Keyboard shortcuts
			if (key == "1") {
				if (host->GetSelectedTeam() >= 2) {
					host->OnTeamSelected(0);
				} else {
					host->OnWeaponSelected(RIFLE_WEAPON);
					host->OnSpawnPressed();
				}
				host->PlaySelectSound();
				return true;
			} else if (key == "2") {
				if (host->GetSelectedTeam() >= 2) {
					host->OnTeamSelected(1);
				} else {
					host->OnWeaponSelected(SMG_WEAPON);
					host->OnSpawnPressed();
				}
				host->PlaySelectSound();
				return true;
			} else if (key == "3") {
				if (host->GetSelectedTeam() < 2) {
					host->OnWeaponSelected(SHOTGUN_WEAPON);
				}
				host->PlaySelectSound();
				host->OnSpawnPressed();
				return true;
			} else if (key == "Return" || key == "Enter") {
				host->PlaySelectSound();
				host->OnSpawnPressed();
				return true;
			} else if (key == "Escape") {
				host->PlaySelectSound();
				host->OnClosePressed();
				return true;
			}

			return false;
		}

		void LimboMenu::TextInputEvent(const std::string& text) {}

		void LimboMenu::Draw() {
			if (!isActive || !renderer || !fontManager)
				return;

			float currentScreenWidth = renderer->ScreenWidth();
			float currentScreenHeight = renderer->ScreenHeight();

			bool layoutChanged = !cachedLayout.isValid || cachedLayout.screenWidth != currentScreenWidth ||
				cachedLayout.screenHeight != currentScreenHeight;
			if (layoutChanged) {
				RecalculateLayout();
				RecalculateItemRects();
			}

			if (!cachedLayout.isValid)
				return;

			client::IFont& font = fontManager->GetGuiFont();

			// Draw semi-transparent background
			OverlayColorNP(*renderer, MakeVector4(0.0F, 0.0F, 0.0F, 0.5F));
			OverlayFillRect(*renderer, cachedLayout.left, cachedLayout.top,
				cachedLayout.left + cachedLayout.contentsWidth, cachedLayout.top + 140.0F);

			// Draw "Select Team:" label
			Vector4 color = MakeVector4(1, 1, 1, 1);
			Vector4 shadowColor = MakeVector4(0, 0, 0, 0.4F);
			{
				auto str = _Tr("Client", "Select Team:");
				Vector2 pos = {cachedLayout.left + 10.0F, cachedLayout.top + 10.0F};
				font.DrawShadow(str, pos, 1.0F, color, shadowColor);
			}

			// Draw "Select Weapon:" label (only for teams, not spectators)
			if (host->GetSelectedTeam() < 2) {
				auto str = _Tr("Client", "Select Weapon:");
				Vector2 pos = {cachedLayout.weapX, cachedLayout.top + 10.0F};
				font.DrawShadow(str, pos, 1.0F, color, shadowColor);
			}

			// Draw all buttons
			for (const auto& item : items) {
				if (!item.visible)
					continue;

				MenuItemType type = item.type;
				bool selected = false;
				int hotkey = 0;

				// Determine if button is selected and its hotkey
				int selectedTeam = host->GetSelectedTeam();
				WeaponType selectedWeapon = host->GetSelectedWeapon();
				switch (type) {
					case MenuItemType::Team1:
						selected = (selectedTeam == 0);
						hotkey = 1;
						break;
					case MenuItemType::Team2:
						selected = (selectedTeam == 1);
						hotkey = 2;
						break;
					case MenuItemType::TeamSpectator:
						selected = (selectedTeam == 2);
						hotkey = 3;
						break;
					case MenuItemType::WeaponRifle:
						selected = (selectedWeapon == RIFLE_WEAPON);
						hotkey = 1;
						break;
					case MenuItemType::WeaponSMG:
						selected = (selectedWeapon == SMG_WEAPON);
						hotkey = 2;
						break;
					case MenuItemType::WeaponShotgun:
						selected = (selectedWeapon == SHOTGUN_WEAPON);
						hotkey = 3;
						break;
					default:
						break;
				}

				// Draw button
				if (type == MenuItemType::Spawn) {
					widgets::PaintButton(*renderer, font, item.rect.min,
						MakeVector2(item.rect.GetWidth(), item.rect.GetHeight()),
						item.text, MakeVector2(0.5F, 0.5F), "", MakeVector2(1.0F, 0.5F),
						true, false, false, item.hover);

					// Draw spawn hotkey hint
					std::string hotkeyStr = "[1, 2, 3]";
					Vector2 hotkeySize = font.Measure(hotkeyStr);
					Vector2 hotkeyPos = {item.rect.GetMaxX() - hotkeySize.x - 8.0F,
										item.rect.min.y + (item.rect.GetHeight() - hotkeySize.y) * 0.5F};
					font.DrawShadow(hotkeyStr, hotkeyPos, 1.0F,
						MakeVector4(1, 1, 1, 0.6F), shadowColor);
				} else if (type == MenuItemType::Close) {
					widgets::PaintButton(*renderer, font, item.rect.min,
						MakeVector2(item.rect.GetWidth(), item.rect.GetHeight()),
						item.text, MakeVector2(0.5F, 0.5F), "", MakeVector2(1.0F, 0.5F),
						true, false, false, item.hover);

					// Draw close hotkey hint
					std::string hotkeyStr = "[Esc]";
					Vector2 hotkeySize = font.Measure(hotkeyStr);
					Vector2 hotkeyPos = {item.rect.GetMinX() - hotkeySize.x - 5.0F,
										item.rect.min.y + (item.rect.GetHeight() - hotkeySize.y) * 0.5F};
					font.DrawShadow(hotkeyStr, hotkeyPos, 1.0F,
						MakeVector4(1, 1, 1, 0.6F), shadowColor);
				} else {
					// Team and weapon buttons
					widgets::PaintButton(*renderer, font, item.rect.min,
						MakeVector2(item.rect.GetWidth(), item.rect.GetHeight()),
						item.text, MakeVector2(0.0F, 0.5F), "", MakeVector2(1.0F, 0.5F),
						true, selected, false, item.hover);

					// Draw hotkey hint
					if (hotkey > 0) {
						std::string hotkeyStr = Format("[{0}]", hotkey);
						Vector2 hotkeySize = font.Measure(hotkeyStr);
						Vector2 hotkeyPos = {item.rect.GetMaxX() - hotkeySize.x - 5.0F,
											item.rect.min.y + (item.rect.GetHeight() - hotkeySize.y) * 0.5F};
						font.DrawShadow(hotkeyStr, hotkeyPos, 1.0F,
							MakeVector4(1, 1, 1, 0.6F), shadowColor);
					}
				}
			}

			// Draw cursor
			cursor.Draw();
		}

		void LimboMenu::Update(float dt) {
			if (!isActive)
				return;

			Vector2 cursorPos = cursor.GetPosition();
			int selectedTeam = host->GetSelectedTeam();

			for (size_t i = 0; i < items.size(); i++) {
				MenuItem& item = items[i];
				item.visible = true;

				switch (item.type) {
					case MenuItemType::WeaponRifle:
					case MenuItemType::WeaponShotgun:
					case MenuItemType::WeaponSMG:
						if (selectedTeam >= 2)
							item.visible = false;
						break;
					default:;
				}

				item.hover = false;
			}

			// Check hover state based on cursor position
			int hoveredIndex = MenuButtonAt(cursorPos);
			if (hoveredIndex >= 0 && hoveredIndex < static_cast<int>(items.size())) {
				items[hoveredIndex].hover = true;
				if (hoveredIndex != prevSelectedItem) {
					host->PlayHoverSound();
					prevSelectedItem = hoveredIndex;
				}
			} else if (prevSelectedItem >= 0) {
				prevSelectedItem = -1;
			}
		}

		void LimboMenu::MouseEvent(float x, float y) {
			if (isActive)
				cursor.SetPosition(MakeVector2(x, y));
		}

		int LimboMenu::MenuButtonAt(const Vector2& p) const {
			for (size_t i = 0; i < items.size(); i++) {
				const MenuItem& item = items[i];
				if (!item.visible)
					continue;
				if (item.rect.min.x <= p.x && p.x <= item.rect.GetMaxX() &&
					item.rect.min.y <= p.y && p.y <= item.rect.GetMaxY())
					return static_cast<int>(i);
			}
			return -1;
		}

		void LimboMenu::HandleMenuItemSelection(MenuItemType type) {
			switch (type) {
				case MenuItemType::Team1:
					host->OnTeamSelected(0);
					break;
				case MenuItemType::Team2:
					host->OnTeamSelected(1);
					break;
				case MenuItemType::TeamSpectator:
					host->OnTeamSelected(2);
					break;
				case MenuItemType::WeaponRifle:
					host->OnWeaponSelected(RIFLE_WEAPON);
					break;
				case MenuItemType::WeaponSMG:
					host->OnWeaponSelected(SMG_WEAPON);
					break;
				case MenuItemType::WeaponShotgun:
					host->OnWeaponSelected(SHOTGUN_WEAPON);
					break;
				case MenuItemType::Spawn:
					host->OnSpawnPressed();
					break;
				case MenuItemType::Close:
					host->OnClosePressed();
					break;
			}
		}

	} // namespace gui
} // namespace spades
