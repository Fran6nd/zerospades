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

#include <optional>
#include <string>
#include <vector>

#include <Core/Math.h>
#include <Gui/UI/Components/ContextMenu.h>
#include <Gui/UI/Components/PropertyGrid.h>
#include <Gui/UI/Components/TabbedPanel.h>
#include <Gui/UI/Components/TreeView.h>

#include "SceneEditContext.h"

namespace spades {
	namespace client {
		class IRenderer;
		class FontManager;
		class IAudioDevice;
	} // namespace client
	namespace gui {
		/**
		 * The scene editor's panel: a tabbed side panel with an outliner
		 * of the scene's objects and the properties of the active one.
		 *
		 * It is the adapter between the generic components (TabbedPanel,
		 * TreeView, PropertyGrid), which know nothing of scenes, and the
		 * editor, which it only reaches through ISceneEditContext. Every frame it
		 * reads the scene back into them; every change they report goes
		 * through the context, so picking, renaming, reparenting and placing
		 * an object here are the same journaled steps as anywhere else, and
		 * undo takes them back.
		 *
		 * Owns no document state. What is its own is how it is shown: the
		 * tab, the tree's folds and scroll, and whether scale is locked.
		 *
		 * Its tabs: the outliner, the active object's properties (with its
		 * lock), and the scene checked as a character skin by the game's rules.
		 */
		class ScenePanel {
		public:
			ScenePanel(ISceneEditContext& editor, client::IAudioDevice* audioDevice);

			/** How wide the panel is. */
			static constexpr float kWidth = 250.0F;

			/**
			 * Catches up with the scene and lays the panel out in `bounds`.
			 * Called once a frame while it shows, before it is drawn and before
			 * the input of the next frame reaches it.
			 */
			void Update(const AABB2& bounds);
			void Draw(client::IRenderer& renderer, client::FontManager& fontManager,
			          const Vector2& cursorPos, bool menuActive);

			bool Contains(const Vector2& p) const { return frame.Contains(p); }

			// --- Pointer ------------------------------------------------------
			/**
			 * A left press at `p`; false when it is not on the panel. `extend`
			 * (Shift or Ctrl held) adds to the picked objects rather than
			 * replacing them. `time` tells a double click (rename) apart.
			 */
			bool MouseDown(const Vector2& p, bool extend, float time);
			/**
			 * A right press at `p`; false when it is not on the panel. Over the
			 * outliner it opens the object menu, picking the object under it
			 * first unless that one is picked already.
			 */
			bool RightMouseDown(const Vector2& p);
			void MouseMove(const Vector2& p);
			void MouseUp(const Vector2& p);
			/** Scrolls the outliner; false when `p` is not over it. */
			bool Wheel(const Vector2& p, float rows);
			/**
			 * A press on the panel is still held (an object being dragged in the
			 * outliner, a value being scrubbed), or its menu is open. The pointer
			 * is the panel's until that is over.
			 */
			bool IsCapturing() const;

			// --- Keyboard -----------------------------------------------------
			/** A name or a number is being typed: the keyboard is the panel's. */
			bool IsTyping() const;
			/** A key going down, taken while a gesture is on; false otherwise. */
			bool KeyEvent(const std::string& key);
			void TextInputEvent(const std::string& text);
			/** Where the text being typed is, for the IME; empty when none is. */
			AABB2 TextInputRect() const;

			/** Ends whatever gesture is on; `commit` keeps what it reached. */
			void EndInput(bool commit);
			/** Shows the outliner and starts renaming the active object. */
			void RenameActive();

		private:
			enum Tab { kOutlinerTab = 0, kPropertiesTab = 1, kSkinTab = 2 };

			// One transform field being scrubbed or typed: where the object was
			// when it began, which every value it reports is applied to, so the
			// fields that are not being changed keep exactly what they held.
			struct PendingEdit {
				SceneObjectId id = kNoSceneObject;
				ObjectTransform start;
				Vector3 startEuler = MakeVector3(0.0F, 0.0F, 0.0F);
				// The angles as typed so far. Shown instead of the ones read back
				// from the turn, which may name the same turn differently and
				// would make the other two fields jump under the cursor.
				Vector3 euler = MakeVector3(0.0F, 0.0F, 0.0F);
			};

			// Where a drop in the outliner lands, in scene terms.
			struct Destination {
				std::vector<SceneObjectId> ids;
				SceneObjectId parent = kNoSceneObject;
				SceneObjectId before = kNoSceneObject;
			};

			ISceneEditContext& editor;
			TabbedPanel frame;
			TreeView tree;
			PropertyGrid properties;
			ContextMenu menu;
			// Whether a change to one scale factor changes all three by the same
			// ratio. A setting of the panel, not of the document.
			bool scaleLocked = false;
			// The active object as of the last reveal, so the outliner follows a
			// pick made in the viewport without fighting the user's scrolling.
			SceneObjectId revealed = kNoSceneObject;
			std::optional<PendingEdit> pending;
			// The scene checked as a character skin, as of this frame.
			CharacterSkinCheck skinCheck;

			void RefreshTree();
			void RefreshProperties();
			Destination Resolve(TreeView::ItemId dragged, const TreeView::DropTarget& target) const;
			void OnNumberChanged(const std::string& fieldId, float value, bool committed);
			/** Opens the object menu at `at`, offering what applies to what is picked. */
			void OpenMenu(const Vector2& at);
			/** Carries out the menu command `id` on what is picked. */
			void RunCommand(const std::string& id);
			/** The Skin tab: whether the game would load this scene as a skin, and why not. */
			void DrawSkinTab(client::IRenderer& renderer, client::FontManager& fontManager);
		};
	} // namespace gui
} // namespace spades
