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

#pragma once

#include <string>

#include "View.h"
#include <Client/Client.h>
#include <Client/EditorNetClient.h>
#include <Client/GameMap.h>
#include <Client/IAudioDevice.h>
#include <Client/IRenderer.h>
#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace client {
		class FontManager;
		class Client;
		class EditorNetClient;
	}
	namespace gui {
		/**
		 * Map editor view for .vxl files.
		 *
		 * Loads a voxel terrain map and provides a local game client for viewing
		 * and editing. The player starts as a spectator in edit mode.
		 */
		class MapEditorView : public View {
		public:
			MapEditorView(client::IRenderer* r, client::IAudioDevice* dev,
			              client::FontManager* fm, const std::string& path);

			void MouseEvent(float x, float y) override;
			void WheelEvent(float x, float y) override;
			void KeyEvent(const std::string&, bool down) override;
			void TextInputEvent(const std::string&) override;
			bool AcceptsTextInput() override;
			AABB2 GetTextInputRect() override;
			bool NeedsAbsoluteMouseCoordinate() override { return false; }

			void RunFrame(float dt) override;
			void RunFrameLate(float dt) override;
			void Closing() override;
			bool WantsToBeClosed() override { return wantsClose; }

		protected:
			~MapEditorView();

		private:
			Handle<client::IRenderer> renderer;
			Handle<client::IAudioDevice> audioDevice;
			Handle<client::FontManager> fontManager;
			Handle<client::Client> client;
			std::string filePath;
			bool wantsClose = false;
			bool clientCreated = false;

			// Esc menu state
			bool menuOpen = false;
			int selectedMenuItem = 0; // 0=Resume, 1=Save, 2=Save As
			bool promptOpen = false;
			std::string promptText;

			void LoadMap();
			void Save();
			void SaveAs();
			int MenuButtonAt(const Vector2& p);
			void DrawMenu(float sw, float sh);
			void DrawPrompt(float sw, float sh);
			void SubmitPrompt();

			// Rendering helpers
			void ColorNP(const Vector4& c);
			void FillRect(float x, float y, float w, float h);
			void StrokeRect(float x, float y, float w, float h, float t, const Vector4& c);
		};
	} // namespace gui
} // namespace spades
