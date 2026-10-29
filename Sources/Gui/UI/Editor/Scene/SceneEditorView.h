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

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Client/IAudioDevice.h>
#include <Client/IRenderer.h>
#include <Client/SceneDefinition.h>
#include <Core/CopyOnWrite.h>
#include <Core/Math.h>
#include <Core/RefCountedObject.h>
#include <Core/VoxelModel2KV6.h>
#include <Gui/DocumentTypes.h>
#include <Gui/UI/Components/EditorMenu.h>
#include <Gui/UI/Components/FileBrowser/FileBrowserTypes.h>
#include <Gui/UI/Components/SoftwareCursor.h>
#include <Gui/UI/Editor/KV6/KV6Document.h>
#include <Gui/UI/Editor/Shell/EditorCamera.h>
#include <Gui/UI/Editor/Shell/EditorHandOff.h>
#include <Gui/UI/Editor/Shell/NavigationCube.h>
#include <Gui/UI/Editor/Voxel/VoxelEditorHost.h>
#include <Gui/View.h>

#include "SceneEditContext.h"

namespace spades {
	class VoxelModel;
	namespace client {
		class FontManager;
		class IModel;
	} // namespace client
	namespace gui {
		class EditorUI;
		class ScenePanel;
		class VoxelEditor;

		/**
		 * In-app editor of .2kv6 scenes: named voxel objects, each with its own
		 * place in the world, nested under one another.
		 *
		 * Hosted by `MainScreen` as a `subview`, as the model editor is, and built
		 * the same way: a VoxelEditor does the editing, and the view adds the
		 * camera, the render and the files. It works as Blender does: in Object
		 * mode whole objects are picked and placed, with the outliner and the
		 * active object's properties down the left, and in Edit mode the
		 * VoxelEditor changes the voxels of the active object, its document,
		 * while the rest of the scene stands around it.
		 *
		 * The scene's shape (which objects there are, their names, where they
		 * sit, which are picked) is journaled through the VoxelEditor's
		 * history, so it undoes with the voxels. The voxels of every object
		 * stay here, keyed by object id, as a model's single one does, so the
		 * history stays small and a replay never rebuilds them.
		 */
		class SceneEditorView : public View,
		                        public IVoxelEditorHost,
		                        public ISceneEditContext,
		                        public IEditorMenuHost,
		                        public IEditorHandOff {
		public:
			SceneEditorView(client::IRenderer* renderer, client::IAudioDevice* audioDevice,
			                client::FontManager* fontManager, SoftwareCursor* cursor,
			                const std::string& path, bool isNew);

			void MouseEvent(float x, float y) override;
			void WheelEvent(float x, float y) override;
			void KeyEvent(const std::string&, bool down) override;
			void TextInputEvent(const std::string&) override;
			void TextEditingEvent(const std::string&, int start, int len) override;
			bool AcceptsTextInput() override;
			AABB2 GetTextInputRect() override;
			bool NeedsAbsoluteMouseCoordinate() override { return false; }

			void RunFrame(float dt) override;
			void Closing() override {}
			bool WantsToBeClosed() override { return wantsClose; }

			// --- IEditorHandOff -----------------------------------------------
			std::string DocumentToOpenNext() const override { return handOffPath; }

			// --- IVoxelEditorHost ---------------------------------------------
			const GizmoView& CurrentView() const override { return cam.View(); }
			const Vector2& AimPosition() const override { return softwareCursor->GetPosition(); }
			Matrix4 DocumentToToolSpace() const override;
			float ViewportLeftInset() const override;
			bool KeyIsReserved(const std::string& key) const override;
			bool ClickViewportWidget(const Vector2& cursor) override;
			void DocumentReframed(const Vector3& shift) override;
			void ReleaseHeldKeys() override { cam.ReleaseHeld(); }

			// --- IVoxelSceneHost ----------------------------------------------
			bool CanEditActiveObject() const override;
			std::shared_ptr<const JournaledHostState> SnapshotJournal() const override;
			void RestoreJournal(const std::shared_ptr<const JournaledHostState>& state,
			                    EditorMode previousMode) override;
			void ModeChanging() override;
			void ModeChanged(EditorMode previous) override;
			void EndPreviews() noexcept override;
			int ObjectCount() const override { return int(state.scene->ObjectIds().size()); }
			std::vector<SceneObjectId> SelectedObjects() const override {
				return *state.selectedObjects;
			}
			SceneObjectId ObjectAtCursor() override;
			void SelectObject(SceneObjectId id) override;
			void ToggleObjectSelected(SceneObjectId id) override;
			SceneObjectId CreateObject() override;
			bool DeleteSelectedObjects() override;
			bool GetSelectionPose(Vector3& position, Quaternion& rotation) const override;
			void BeginObjectDrag() override;
			void PreviewObjectDrag(const GizmoTransform& change) override;
			void CommitObjectDrag(const std::string& label) override;
			void CancelObjectDrag() override;

			// --- ISceneEditContext --------------------------------------------
			const Scene& GetScene() const override { return *state.scene; }
			SceneObjectId ActiveObject() const override { return state.activeObject; }
			bool IsObjectSelected(SceneObjectId id) const override;
			void SelectObjects(const std::vector<SceneObjectId>& ids) override {
				SetSelectedObjects(ids, "Select Objects");
			}
			void SelectAllObjects() override;
			bool RenameObject(SceneObjectId id, const std::string& name) override;
			bool MoveObjects(const std::vector<SceneObjectId>& ids, SceneObjectId parent,
			                 SceneObjectId before) override;
			void PreviewObjectTransform(SceneObjectId id, const ObjectTransform& transform) override;
			void SetObjectTransform(SceneObjectId id, const ObjectTransform& transform,
			                        const std::string& label) override;
			SceneObjectId GroupObjects(const std::vector<SceneObjectId>& ids) override;
			bool UngroupObjects(const std::vector<SceneObjectId>& ids) override;
			void AskToSetObjectLocked(SceneObjectId id, bool locked) override;
			void SetObjectsHidden(const std::vector<SceneObjectId>& ids, bool hidden) override;
			CharacterSkinCheck CheckCharacterSkin() override;

		protected:
			~SceneEditorView();

		private:
			Handle<client::IRenderer> renderer;
			Handle<client::IAudioDevice> audioDevice;
			Handle<client::FontManager> fontManager;

			SoftwareCursor* softwareCursor = nullptr;
			std::unique_ptr<SoftwareCursor> ownedCursor; // if no cursor was provided
			Handle<EditorUI> ui;
			std::unique_ptr<ScenePanel> scenePanel;

			// --- Scene --------------------------------------------------------
			// What is journaled of the scene: its objects and their transforms,
			// and the ones picked in Object mode, in the order they were picked.
			// `activeObject` is the last of them: the one Edit mode works on, and
			// the one whose axes the gizmo takes.
			struct SceneState {
				CopyOnWrite<Scene> scene;
				CopyOnWrite<std::vector<SceneObjectId>> selectedObjects;
				SceneObjectId activeObject = kNoSceneObject;

				bool IsSelected(SceneObjectId id) const;
				bool operator==(const SceneState& o) const {
					return scene == o.scene && selectedObjects == o.selectedObjects &&
					       activeObject == o.activeObject;
				}
			};
			class Journal; // a SceneState as the history keeps it
			SceneState state;

			// The voxels of every object holding some, by id. They stay here when
			// the object is deleted, which is how the history brings it back.
			std::map<SceneObjectId, Handle<VoxelModel>> models;
			// Which object's voxels the document holds, so a change to them (a
			// reframe replaces the model) is stored back under the right object
			// even while the active one is being changed.
			SceneObjectId documentOwner = kNoSceneObject;
			// Stands in for the voxels when there is no object to edit, so the
			// document always holds a model.
			Handle<VoxelModel> emptyModel;
			// Bumped whenever an object's voxels change, so what is derived from
			// them (its render model) knows to be rebuilt.
			std::map<SceneObjectId, std::uint64_t> modelRevision;
			// The document's version when its owner's revision was last bumped.
			std::uint64_t documentVersionSeen = 0;

			/** The voxels of `id`, or null when it holds none. */
			VoxelModel* ModelOf(SceneObjectId id) const;
			/** Bumps the revision of the object the document holds, if its voxels
			 *  changed since it was last bumped. */
			void NoteDocumentEdits();
			// Points the document at the active object's voxels, after anything
			// that changed which object is active or what it holds, and lets the
			// editor settle its mode on what that leaves to edit.
			void RefreshActiveModel();
			// Adds `voxels` to the scene as an object named `name` (numbered if
			// that name is taken) and makes it active, as one undo step called
			// `label`. Returns its id.
			SceneObjectId AddSceneObject(const std::string& name, Handle<VoxelModel> voxels,
			                             const std::string& label);
			// Brings a model file into the scene: a .kv6 as one object, a .2kv6
			// as its own objects, each named apart from what is already there.
			// One undo step; false (with a status line) when it cannot be read.
			bool AddModelFileAsObjects(const std::string& path);
			// Replaces the picked objects with `ids`, the last being the active
			// one, as one journaled step named `label`. Does nothing when those
			// objects are already the picked ones.
			void SetSelectedObjects(std::vector<SceneObjectId> ids, const std::string& label);
			// Picks `ids` (the last being active) and points the document at what
			// that makes editable, journaling nothing of itself: for the document
			// paths, where the pick is part of the state being set up, and for the
			// commands, inside their step.
			void PickObjectsRaw(std::vector<SceneObjectId> ids);
			/** The object under the cursor, and where on it the cursor is (in the
			 *  tools' space) when `hitPoint` is given; `kNoSceneObject` for none. */
			SceneObjectId PickObject(Vector3* hitPoint);
			/** The box `ids` take up in the tools' space; false when none of them
			 *  holds a voxel. */
			bool ObjectsBounds(const std::vector<SceneObjectId>& ids, Vector3& lo,
			                   Vector3& hi) const;
			/** The colour `id`'s silhouette is drawn in: black unless it is picked. */
			Vector3 OutlineColorOf(SceneObjectId id) const;
			/** `wanted`, less what `id` may not change: a locked object only moves. */
			ObjectTransform AllowedTransform(SceneObjectId id, const ObjectTransform& wanted) const;
			/** Locks or unlocks `id` as one undo step. */
			void SetObjectLocked(SceneObjectId id, bool locked);
			/** The whole scene as the file holds it: objects with their voxels. */
			std::vector<VoxelObject> SceneAsObjects() const;

			// --- Spaces -------------------------------------------------------
			/**
			 * What turns the space the camera and the tools work in into the
			 * scene's.
			 *
			 * Every tool, every outline and every gizmo speaks the voxel
			 * coordinates of the document, the active object's own grid. In Edit
			 * mode the scene is shown through that object's transform, so the
			 * rest of it stands around at the right place; Object mode works on
			 * the scene itself, where the two are the same.
			 */
			Matrix4 ToolsToScene() const { return ToolsToScene(CurrentMode(), state); }
			Matrix4 ToolsToScene(EditorMode mode, const SceneState& in) const;
			/** Turns object `id`'s voxel coordinates into the tools' space. */
			Matrix4 ObjectVoxelMatrix(SceneObjectId id) const;
			/** Where object `id` is held: its model's pivot, in that same space. */
			Vector3 ObjectPivot(SceneObjectId id) const;
			EditorMode CurrentMode() const;

			// --- Object drags -------------------------------------------------
			// The objects being dragged in Object mode: where each sat before the
			// drag (so ending the preview puts them back), and the frame the drag
			// is measured in. A value being scrubbed or typed in the scene panel
			// previews through this too, as a drag of its one object.
			struct PreviewedDrag {
				std::vector<SceneObjectId> ids;
				std::vector<ObjectTransform> before; // parallel to `ids`
				// What each one's parents imposed when the drag began. Held from
				// then on: the drag moves the objects, and reading their parents
				// back out mid-drag would carry a child by its parent's move as
				// well as its own.
				std::vector<Matrix4> parent; // parallel to `ids`
				Vector3 pivot = MakeVector3(0.0F, 0.0F, 0.0F);
				Vector3 axes[3] = {MakeVector3(1.0F, 0.0F, 0.0F), MakeVector3(0.0F, 1.0F, 0.0F),
				                   MakeVector3(0.0F, 0.0F, 1.0F)};
			};
			std::optional<PreviewedDrag> previewedDrag;
			/** Puts every object of `drag` back where it was. */
			void RestoreDragged(const PreviewedDrag& drag);

			// --- Document -----------------------------------------------------
			KV6Document document;
			std::unique_ptr<VoxelEditor> editor;
			std::string filePath;
			// The document is dirty while its state differs from the one captured
			// at the last save (-1 = never saved).
			long savedDocumentId = -1;
			bool IsDirty() const;
			// What saving would change: the journaled edits, plus voxels still
			// waiting to be placed.
			bool HasUnsavedChanges() const;
			// Set when a document of another kind was opened from here; the main
			// screen opens it in its own editor once this one has closed.
			std::string handOffPath;

			// One render model per object, rebuilt when its voxels change; the
			// active one is drawn from the document's, `renderModel`.
			struct ObjectRenderModel {
				Handle<client::IModel> model;
				std::uint64_t revision = 0; // of the voxels it was built from
			};
			std::map<SceneObjectId, ObjectRenderModel> objectRenderModels;
			Handle<client::IModel> renderModel;
			std::uint64_t renderModelVersion = ~std::uint64_t(0); // document it shows
			void RefreshRenderModels();

			// CheckCharacterSkin's last answer, and the scene it was for.
			struct SkinCheckCache {
				bool valid = false;
				std::uint64_t sceneVersion = 0;
				std::uint64_t voxelsVersion = 0;
				CharacterSkinCheck result;
			} skinCheckCache;
			// Advances with every change to any object's voxels.
			std::uint64_t VoxelsVersion() const;

			float globalTime = 0.0F;
			bool wantsClose = false;
			bool wantScreenShot = false; // set by the screenshot key, served at end of frame

			// --- Camera -------------------------------------------------------
			EditorCamera cam;
			NavigationCube naviCube;
			void FrameCamera();
			// Flies back to the view the scene opened with.
			void ResetView();
			// Home flies back to the view the scene opened with, F frames what is
			// picked; true when `key` was one of them.
			bool ViewKey(const std::string& key);
			// The camera's scene for the full screen, lit and outlined for editing.
			client::SceneDefinition SetupScene(float width, float height);

			/** Starts a new scene: a root holding one object, as `path`. */
			void NewScene(int n, const std::string& path);
			/** Replaces the document with the scene at `path`. False leaves the
			 *  document untouched: the file could not be read. */
			bool LoadScene(const std::string& path);
			/**
			 * Replaces the document with a scene of `objects`, as a loaded one is
			 * opened; `path` empty makes it a new document, not yet saved.
			 */
			void OpenSceneObjects(const std::vector<VoxelObject>& objects, const std::string& path);
			/** Writes the document to its path; false if there is none, or on error. */
			bool Save();

			// --- Character skins ----------------------------------------------
			/** Starts a new skin from the game's own classic body parts. */
			void NewCharacterSkin();
			/**
			 * Starts a new skin from a folder of classic parts (an old player
			 * model mod, say), picked by any one of its .kv6 files.
			 */
			void OpenImportClassicPartsDialog();
			/** Builds the template from `part`, and opens it as a new document. */
			void OpenCharacterTemplate(
			  const std::function<Handle<VoxelModel>(const std::string&)>& part,
			  const std::string& from);

			// Drawing
			void DrawGround();
			void DrawVolumeBox();
			void DrawOriginAxes();
			void DrawRibbon(float sw); // full-width title/filename bar above the toolbar
			// The keys the view handles itself, for the help line.
			std::string HelpLine() const;
			/**
			 * The scene panel (outliner and properties) down the left side, when
			 * it is showing: in Object mode, which is about the scene's objects.
			 * Null otherwise, so every caller that routes input to it reads as
			 * "if it is there".
			 */
			ScenePanel* ShownScenePanel();
			/** Where the scene panel sits on a screen `sh` high. */
			AABB2 ScenePanelBounds(float sh) const;

			// --- IEditorMenuHost ---
			std::string GetMenuTitle() override;
			std::vector<EditorMenuItem> GetMenuItems() override;
			bool OnMenuEscape() override;

			// --- Document commands behind the menu items ---
			bool SaveDocument(const std::string& path);
			/** Asks for a path with the shared file browser, then saves to it.
			 *  `after` runs only once the document has actually been written. */
			void OpenSaveAsDialog(std::function<void()> after = std::function<void()>());
			/** Opens another document in place of this one, guarding unsaved
			 *  changes; a model opens in the model editor. */
			void OpenDocument();
			/** Brings `path` in: whole models as objects in Object mode, voxels
			 *  for the object being edited in Edit mode. */
			void InsertModel(const std::string& path);
			/** Asks for a model with the shared file browser, then inserts it. */
			void OpenInsertDialog();
			/** Starts an empty scene, once it is safe to lose this one. */
			void NewDocument();
			/**
			 * Shows the shared file browser over the editor, starting in the
			 * document's folder; `picked` gets the chosen path. `extensions` are
			 * what it lists and completes names with, the first being the one a
			 * typed name takes when it carries none.
			 */
			void ShowFileDialog(const std::string& title, FileBrowserPurpose purpose,
			                    const std::string& initialName,
			                    const std::vector<std::string>& extensions,
			                    std::function<void(const std::string&)> picked);
			/** A plain message over the editor with a single way out. */
			void ShowMessage(const std::string& text);
			/**
			 * Runs `proceed` once it is safe to lose the current document: right
			 * away when it is clean, otherwise after the user picks Save or Discard.
			 */
			void ConfirmDiscardChanges(std::function<void()> proceed);
		};
	} // namespace gui
} // namespace spades
