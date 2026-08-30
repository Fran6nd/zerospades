/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

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

#include "MapEditorView.h"
#include "MapEditorUI.h"

#include <Client/Client.h>
#include <Client/EditorNetClient.h>
#include <Client/Fonts.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/ServerAddress.h>
#include <Core/Settings.h>
#include <Gui/UI/Components/SoftwareCursor.h>
#include <Gui/UI/Components/EditorMenu.h>

namespace spades {
	namespace gui {
		MapEditorView::MapEditorView(client::IRenderer* r, client::IAudioDevice* dev,
		                             client::FontManager* fm, SoftwareCursor* cursor,
		                             const std::string& path)
		    : renderer(r), audioDevice(dev), fontManager(fm), softwareCursor(cursor),
		      filePath(path) {
			ui = Handle<MapEditorUI>::New(r, dev, fm, this, cursor);
		}

		MapEditorView::~MapEditorView() {
		}

		void MapEditorView::MouseEvent(float x, float y) {
			EditorMenu* menu = ui->GetEditorMenu();
			if (menu->IsActive())
				return;
			if (client) {
				client->MouseEvent(x, y);
			}
		}

		void MapEditorView::WheelEvent(float x, float y) {
			EditorMenu* menu = ui->GetEditorMenu();
			if (menu->IsActive())
				return;
			if (client) {
				client->WheelEvent(x, y);
			}
		}

		void MapEditorView::KeyEvent(const std::string& key, bool down) {
			EditorMenu* menu = ui->GetEditorMenu();
			if (menu->KeyEvent(key, down))
				return;

			if (down && key == "Escape") {
				menu->Open();
				return;
			}

			if (client) {
				client->KeyEvent(key, down);
			}
		}

		void MapEditorView::TextInputEvent(const std::string& text) {
			EditorMenu* menu = ui->GetEditorMenu();
			if (menu->AcceptsTextInput()) {
				menu->TextInputEvent(text);
			} else if (client) {
				client->TextInputEvent(text);
			}
		}

		bool MapEditorView::AcceptsTextInput() {
			EditorMenu* menu = ui->GetEditorMenu();
			return menu->AcceptsTextInput() || (client && client->AcceptsTextInput());
		}

		AABB2 MapEditorView::GetTextInputRect() {
			EditorMenu* menu = ui->GetEditorMenu();
			if (menu->AcceptsTextInput())
				return menu->GetTextInputRect();
			if (client)
				return client->GetTextInputRect();
			return AABB2();
		}

		void MapEditorView::RunFrameLate(float dt) {
			if (client)
				client->RunFrameLate(dt);
		}

		void MapEditorView::Closing() {
			if (client)
				client->Closing();
		}

		void MapEditorView::RunFrame(float dt) {
			// Create client on first frame
			if (!clientCreated) {
				try {
					SPLog("MapEditor: Creating game client for map: '%s'", filePath.c_str());
					ServerAddress addr;
					client = Handle<client::Client>::New(renderer, audioDevice, addr, fontManager);

					auto editorNet = std::make_unique<client::EditorNetClient>(client.GetPointerOrNull());
					if (!editorNet->LoadMap(filePath)) {
						SPLog("Failed to load map: %s", editorNet->GetStatusString().c_str());
						wantsClose = true;
						return;
					}

					client->EnableEditorMode(std::move(editorNet));
					clientCreated = true;
					SPLog("Map loaded successfully, starting game client");
					return;
				} catch (const std::exception& ex) {
					SPLog("Error creating map editor client: %s", ex.what());
					wantsClose = true;
					return;
				}
			}

			// Run game client frame
			if (client) {
				try {
					client->RunFrame(dt);
				} catch (const std::exception& ex) {
					SPLog("Error in client frame: %s", ex.what());
				}
			}

			// Draw menu overlay
			EditorMenu* menu = ui->GetEditorMenu();
			menu->Draw();
		}

		void MapEditorView::SaveDocument(const std::string& path) {
			if (path.empty()) {
				SPLog("No file to save to");
				return;
			}

			try {
				auto stream = FileManager::OpenForWriting(path.c_str());
				// TODO: Save map data from client->GetWorld()->GetMap()
				SPLog("Saved map to %s", path.c_str());
			} catch (const Exception& ex) {
				SPLog("Save failed: %s", ex.GetShortMessage().c_str());
			} catch (const std::exception& ex) {
				SPLog("Save failed: %s", ex.what());
			}
		}
	} // namespace gui
} // namespace spades
