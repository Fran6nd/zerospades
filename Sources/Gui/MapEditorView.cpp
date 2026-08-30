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

#include <fstream>

#include <Client/Client.h>
#include <Client/EditorNetClient.h>
#include <Client/Fonts.h>
#include <Client/GameMap.h>
#include <Client/World.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/ServerAddress.h>
#include <Core/Settings.h>

namespace spades {
	namespace gui {
		MapEditorView::MapEditorView(client::IRenderer* r, client::IAudioDevice* dev,
		                             client::FontManager* fm, const std::string& path)
		    : renderer(r), audioDevice(dev), fontManager(fm), filePath(path) {
		}

		MapEditorView::~MapEditorView() {}

		void MapEditorView::LoadMapDeferred() {
			LoadMap();
		}

		void MapEditorView::LoadMap() {
			try {
				SPLog("MapEditor: Loading map from path: '%s'", filePath.c_str());

				// Use std::ifstream for absolute paths instead of FileManager
				// (FileManager is for virtual filesystem paths)
				std::ifstream file(filePath, std::ios::binary);
				if (!file.is_open()) {
					SPRaise("Failed to open map file: %s", filePath.c_str());
				}

				// Read entire file into memory to avoid lifetime issues
				file.seekg(0, std::ios::end);
				std::streamsize size = file.tellg();
				file.seekg(0, std::ios::beg);

				std::vector<char> buffer(size);
				if (!file.read(buffer.data(), size)) {
					SPRaise("Failed to read map file: %s", filePath.c_str());
				}

				class MemoryStream : public IStream {
					std::vector<char> data;
					size_t pos = 0;
				public:
					MemoryStream(std::vector<char> d) : data(std::move(d)) {}
					size_t Read(void* buf, size_t bytes) override {
						size_t toRead = std::min(bytes, data.size() - pos);
						if (toRead > 0) {
							std::memcpy(buf, data.data() + pos, toRead);
							pos += toRead;
						}
						return toRead;
					}
				};

				MemoryStream stream(std::move(buffer));
				map = Handle<client::GameMap>{client::GameMap::Load(&stream)};
				if (!map) {
					SPRaise("Failed to load map data from: %s", filePath.c_str());
				}

				SPLog("Loaded map successfully: %s", filePath.c_str());

			} catch (const std::exception& ex) {
				SPRaise("Error loading map: %s", ex.what());
			}
		}

		void MapEditorView::MouseEvent(float x, float y) {
			if (menuOpen || promptOpen)
				return;
			// TODO: Handle mouse for map editor
		}

		void MapEditorView::WheelEvent(float x, float y) {
			// TODO: Handle wheel for map editor (zoom, etc)
		}

		void MapEditorView::KeyEvent(const std::string& key, bool down) {
			// Handle Save As prompt
			if (promptOpen) {
				if (!down)
					return;
				if (key == "Escape") {
					promptOpen = false;
				} else if (key == "Return") {
					SubmitPrompt();
				} else if (key == "BackSpace") {
					if (!promptText.empty())
						promptText.pop_back();
				}
				return;
			}

			// Handle menu
			if (menuOpen) {
				if (!down)
					return;
				if (key == "Escape") {
					menuOpen = false;
					selectedMenuItem = 0;
					return;
				}
				if (key == "Up") {
					selectedMenuItem = (selectedMenuItem - 1 + 3) % 3;
					return;
				}
				if (key == "Down") {
					selectedMenuItem = (selectedMenuItem + 1) % 3;
					return;
				}
				if (key == "Return" || key == "LeftMouseButton") {
					if (selectedMenuItem == 0) {
						menuOpen = false;
						selectedMenuItem = 0;
					} else if (selectedMenuItem == 1) {
						Save();
						menuOpen = false;
						selectedMenuItem = 0;
					} else if (selectedMenuItem == 2) {
						SaveAs();
						menuOpen = false;
						selectedMenuItem = 0;
					}
					return;
				}
				return;
			}

			// Esc opens menu
			if (down && key == "Escape") {
				menuOpen = true;
				selectedMenuItem = 0;
				return;
			}

			// TODO: Forward keys to map editor
		}

		void MapEditorView::TextInputEvent(const std::string& text) {
			if (promptOpen)
				promptText += text;
		}

		bool MapEditorView::AcceptsTextInput() {
			return promptOpen;
		}

		AABB2 MapEditorView::GetTextInputRect() {
			return AABB2();
		}

		void MapEditorView::RunFrameLate(float dt) {
		}

		void MapEditorView::Closing() {
		}

		void MapEditorView::RunFrame(float dt) {
			if (!mapLoaded) {
				try {
					LoadMapDeferred();
					mapLoaded = true;
				} catch (const std::exception& ex) {
					SPLog("Failed to load map: %s", ex.what());
				}
			}

			// Draw map viewport or placeholder
			if (map) {
				// TODO: Render map in viewport
			}

			// Draw menu/prompt overlay if open
			if (menuOpen || promptOpen) {
				float sw = renderer->ScreenWidth();
				float sh = renderer->ScreenHeight();
				if (promptOpen)
					DrawPrompt(sw, sh);
				else
					DrawMenu(sw, sh);
			}
		}

		// --- Rendering Helpers (similar to KV6EditorView) ---

		void MapEditorView::ColorNP(const Vector4& c) {
			renderer->SetColorAlphaPremultiplied(MakeVector4(c.x * c.w, c.y * c.w, c.z * c.w, c.w));
		}

		void MapEditorView::FillRect(float x, float y, float w, float h) {
			renderer->DrawImage((client::IImage*)NULL, AABB2(x, y, w, h));
		}

		void MapEditorView::StrokeRect(float x, float y, float w, float h, float t,
		                               const Vector4& c) {
			ColorNP(c);
			FillRect(x, y, w, t);        // top
			FillRect(x, y + h - t, w, t); // bottom
			FillRect(x, y, t, h);        // left
			FillRect(x + w - t, y, t, h); // right
		}

		// --- Menu Rendering ---

		int MapEditorView::MenuButtonAt(const Vector2& p) {
			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();
			float w = 260.0F;
			float x = (sw - w) * 0.5F;
			float y = sh * 0.5F - 110.0F + 44.0F;
			for (int i = 0; i < 3; i++) {
				if (p.x >= x && p.x < x + w && p.y >= y && p.y < y + 36.0F)
					return i;
				y += 44.0F;
			}
			return -1;
		}

		void MapEditorView::DrawMenu(float sw, float sh) {
			client::IFont& font = fontManager->GetSmallGuiFont();
			static const char* kMenuItems[3] = {"Resume", "Save", "Save As..."};

			// Darken background
			ColorNP(MakeVector4(0.0F, 0.0F, 0.0F, 0.7F));
			FillRect(0, 0, sw, sh);

			float w = 260.0F;
			float x = (sw - w) * 0.5F;
			float y = sh * 0.5F - 110.0F;

			// Title
			Vector2 sz = font.Measure("Map Editor");
			font.Draw("Map Editor", MakeVector2(x + (w - sz.x) * 0.5F, y), 1.0F,
			          MakeVector4(1, 1, 1, 1));
			y += 44.0F;

			// Menu buttons - draw 3 buttons
			for (int i = 0; i < 3; i++) {
				if (i == selectedMenuItem) {
					// Highlighted button
					ColorNP(MakeVector4(0.3F, 0.5F, 0.3F, 0.9F)); // Green highlight
				} else {
					// Normal button
					ColorNP(MakeVector4(0.2F, 0.2F, 0.2F, 0.8F));
				}
				FillRect(x, y, w, 36.0F);
				StrokeRect(x, y, w, 36.0F, 1.0F, MakeVector4(0.5F, 0.5F, 0.5F, 0.5F));

				Vector2 sz = font.Measure(kMenuItems[i]);
				font.Draw(kMenuItems[i], MakeVector2(x + (w - sz.x) * 0.5F, y + 6.0F), 1.0F,
				          MakeVector4(1, 1, 1, 1));
				y += 44.0F;
			}
		}

		void MapEditorView::DrawPrompt(float sw, float sh) {
			client::IFont& font = fontManager->GetSmallGuiFont();

			// Darken background
			ColorNP(MakeVector4(0.0F, 0.0F, 0.0F, 0.7F));
			FillRect(0, 0, sw, sh);

			float w = 460.0F, h = 116.0F;
			float x = (sw - w) * 0.5F, y = (sh - h) * 0.5F;

			// Dialog box
			ColorNP(MakeVector4(0.16F, 0.16F, 0.18F, 1.0F));
			FillRect(x, y, w, h);
			StrokeRect(x, y, w, h, 1.0F, MakeVector4(0.5F, 0.5F, 0.5F, 0.7F));

			// Title
			font.Draw("Save As (full path)", MakeVector2(x + 16.0F, y + 12.0F), 1.0F,
			          MakeVector4(0.8F, 0.8F, 0.8F, 1.0F));

			// Text input field
			float fx = x + 16.0F, fy = y + 44.0F;
			std::string shown = promptText + "_";
			font.Draw(shown, MakeVector2(fx + 6.0F, fy + 6.0F), 1.0F, MakeVector4(1, 1, 1, 1));

			// Help text
			font.Draw("[Enter] OK    [Esc] cancel", MakeVector2(x + 16.0F, y + h - 24.0F), 0.9F,
			          MakeVector4(0.7F, 0.7F, 0.7F, 1.0F));
		}

		// --- File Operations ---

		void MapEditorView::Save() {
			if (filePath.empty()) {
				SPLog("No file to save to");
				return;
			}
			if (!map)
				return;

			try {
				auto stream = FileManager::OpenForWriting(filePath.c_str());
				map->Save(stream.get());
				SPLog("Saved map to %s", filePath.c_str());
			} catch (const Exception& ex) {
				SPLog("Save failed: %s", ex.GetShortMessage().c_str());
			} catch (const std::exception& ex) {
				SPLog("Save failed: %s", ex.what());
			}
		}

		void MapEditorView::SaveAs() {
			promptText = filePath;
			promptOpen = true;
		}

		void MapEditorView::SubmitPrompt() {
			std::string p = promptText;
			if (!p.empty()) {
				// Add .vxl extension if not present
				if (p.size() < 4 || p.substr(p.size() - 4) != ".vxl")
					p += ".vxl";
				filePath = p;
				Save();
			}
			promptOpen = false;
			menuOpen = false;
		}
	} // namespace gui
} // namespace spades
