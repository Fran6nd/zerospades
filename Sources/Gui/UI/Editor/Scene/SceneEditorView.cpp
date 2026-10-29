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

#include "SceneEditorView.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <initializer_list>
#include <iterator>
#include <limits>

#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IModel.h>
#include <Client/ScreenShot.h>
#include <Core/CharacterSkin.h>
#include <Core/Debug.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/LocalFileSystem.h>
#include <Core/Settings.h>
#include <Core/VoxelModel.h>
#include <Gui/OverlayPaint.h>
#include <Gui/UI/Components/ColorPicker.h>
#include <Gui/UI/Components/Gizmo/TransformGizmo.h>
#include <Gui/UI/Components/OptionBar.h>
#include <Gui/UI/Components/UIOverlayHost.h>
#include <Gui/UI/Editor/KV6/KV6ModelIO.h>
#include <Gui/UI/Editor/Shell/EditorFileDialog.h>
#include <Gui/UI/Editor/Shell/EditorKeys.h>
#include <Gui/UI/Editor/Shell/EditorPrompts.h>
#include <Gui/UI/Editor/Shell/EditorUI.h>
#include <Gui/UI/Editor/Voxel/VoxelEditor.h>
#include <Gui/UI/Editor/Voxel/VoxelOverlayLines.h>
#include <Gui/UI/Widgets/MessageBox.h>

#include "SceneIO.h"
#include "ScenePanel.h"

SPADES_SETTING(cg_keyMoveForward);
SPADES_SETTING(cg_keyMoveBackward);
SPADES_SETTING(cg_keyMoveLeft);
SPADES_SETTING(cg_keyMoveRight);
SPADES_SETTING(cg_keyJump);
SPADES_SETTING(cg_keyCrouch);
SPADES_SETTING(cg_keySprint);
SPADES_SETTING(cg_keyScreenshot);

namespace spades {
	namespace gui {
		namespace {
			// The view's own Ctrl shortcuts: the document's files, and in Object
			// mode picking and grouping whole objects. Ctrl is also the descend
			// key, so a chord costs the movement key bound to the same letter:
			// the object ones are only claimed where they mean something.
			bool IsViewShortcut(const std::string& key, bool objectMode) {
				for (const char* k : {"s", "n", "o"}) {
					if (EqualsIgnoringCase(key, k))
						return true;
				}
				return objectMode && (EqualsIgnoringCase(key, "a") || EqualsIgnoringCase(key, "g"));
			}

			// The editor's ground grid, as the model editor's: below the deepest
			// voxel any model may hold.
			constexpr float kGroundPlaneZ = float(KV6Document::kMaxDepth);
			constexpr int kGroundReach = 256;    // voxels from the origin, each way
			constexpr int kGroundStep = 4;       // voxels between lines
			constexpr int kGroundMajorEvery = 4; // every Nth line is brighter

			// Camera speed, in voxels per second per voxel of what is framed.
			constexpr float kCameraSpeedPerSize = 0.7F;

			// The navigation cube's box on screen, its distance from the right
			// edge and from the bars, and how far the cube sits inside the box.
			constexpr float kNaviCubeBox = 174.0F;
			constexpr float kNaviCubeMargin = 16.0F;
			constexpr float kNaviCubeTopGap = 8.0F;
			constexpr float kNaviCubeInset = 14.0F;

			// Edge length of the cube a new object's volume starts as.
			constexpr int kNewModelSize = 32;

			// The view keys: back to the opening view, and frame what is picked.
			const char* const kHomeViewKey = "Home";
			const char* const kFrameSelectionKey = "F";

			bool BoundToKeySetting(const std::string& key) {
				const std::string bound[] = {cg_keyMoveForward, cg_keyMoveBackward, cg_keyMoveLeft,
				                             cg_keyMoveRight,   cg_keyJump,         cg_keyCrouch,
				                             cg_keySprint,      cg_keyScreenshot};
				for (const std::string& b : bound) {
					if (EditorKeyMatches(b, key))
						return true;
				}
				return false;
			}

			Vector3 Apply(const Matrix4& m, const Vector3& p) {
				return (m * MakeVector4(p.x, p.y, p.z, 1.0F)).GetXYZ();
			}

			// The scene as the file format holds it, built from what the editor
			// keeps apart: the journaled nodes, and the voxels by object id.
			VoxelObject ToVoxelObject(const SceneNode& node,
			                          const std::map<SceneObjectId, Handle<VoxelModel>>& models) {
				VoxelObject object;
				object.name = node.name;
				object.position = node.position;
				object.rotation = node.rotation;
				object.scale = node.scale;
				object.locked = node.locked;
				object.hidden = node.hidden;
				object.keyframes = node.keyframes;
				if (node.hasModel) {
					auto it = models.find(node.id);
					if (it != models.end())
						object.model = it->second;
				}
				for (const SceneNode& child : node.children)
					object.children.push_back(ToVoxelObject(child, models));
				return object;
			}

			// The reverse: a loaded object becomes a node, and its voxels go to
			// `models` under the id this hands out.
			SceneNode ToSceneNode(const VoxelObject& object, Scene& scene,
			                      std::map<SceneObjectId, Handle<VoxelModel>>& models) {
				SceneNode node;
				node.id = scene.nextId++;
				node.name = object.name;
				node.position = object.position;
				node.rotation = object.rotation;
				node.scale = object.scale;
				node.locked = object.locked;
				node.hidden = object.hidden;
				node.keyframes = object.keyframes;
				node.hasModel = static_cast<bool>(object.model);
				if (node.hasModel)
					models[node.id] = object.model;
				for (const VoxelObject& child : object.children)
					node.children.push_back(ToSceneNode(child, scene, models));
				return node;
			}

			// The object `path` names from the top of `scene` down, or none.
			SceneObjectId FindByPath(const Scene& scene, const std::vector<std::string>& path) {
				const std::vector<SceneNode>* level = &scene.roots;
				const SceneNode* found = nullptr;
				for (const std::string& name : path) {
					auto it = std::find_if(level->begin(), level->end(),
					                       [&](const SceneNode& node) { return node.name == name; });
					if (it == level->end())
						return kNoSceneObject;
					found = &*it;
					level = &found->children;
				}
				return found ? found->id : kNoSceneObject;
			}

			// The first solid voxel of `voxels` a ray meets, in the model's own space
			// where voxel (x,y,z) is centred on (x,y,z); `distance` is how far along
			// `dir` it lies. False when the ray meets nothing.
			bool RayVoxelHit(const VoxelModel& voxels, const Vector3& rayStart, const Vector3& dir,
			                 float& distance) {
				// Start where the ray meets the model's box: from far away (or
				// close up on a small object) the voxels are many cells off, and
				// a walk that began at the eye would give up before reaching them.
				const float from[3] = {rayStart.x, rayStart.y, rayStart.z};
				const float along[3] = {dir.x, dir.y, dir.z};
				const float hi[3] = {float(voxels.GetWidth()) - 0.5F, float(voxels.GetHeight()) - 0.5F,
				                     float(voxels.GetDepth()) - 0.5F};
				float tEnter = 0.0F;
				float tLeave = std::numeric_limits<float>::infinity();
				for (int k = 0; k < 3; k++) {
					if (along[k] == 0.0F) {
						if (from[k] < -0.5F || from[k] > hi[k])
							return false; // parallel to the box and outside it
						continue;
					}
					float t0 = (-0.5F - from[k]) / along[k];
					float t1 = (hi[k] - from[k]) / along[k];
					if (t0 > t1)
						std::swap(t0, t1);
					tEnter = std::max(tEnter, t0);
					tLeave = std::min(tLeave, t1);
				}
				if (tEnter > tLeave)
					return false; // the ray misses the model altogether

				// Traverse in a +0.5-shifted space, where each integer cell maps to a
				// voxel index.
				const Vector3 o = rayStart + dir * tEnter + MakeVector3(0.5F, 0.5F, 0.5F);
				int cx = int(std::floor(o.x));
				int cy = int(std::floor(o.y));
				int cz = int(std::floor(o.z));
				const int stepX = (dir.x >= 0.0F) ? 1 : -1;
				const int stepY = (dir.y >= 0.0F) ? 1 : -1;
				const int stepZ = (dir.z >= 0.0F) ? 1 : -1;
				const float big = 1.0e30F;
				const float tDeltaX = (dir.x != 0.0F) ? std::fabs(1.0F / dir.x) : big;
				const float tDeltaY = (dir.y != 0.0F) ? std::fabs(1.0F / dir.y) : big;
				const float tDeltaZ = (dir.z != 0.0F) ? std::fabs(1.0F / dir.z) : big;
				float tMaxX =
				  (dir.x != 0.0F) ? ((float(cx) + (stepX > 0 ? 1.0F : 0.0F) - o.x) / dir.x) : big;
				float tMaxY =
				  (dir.y != 0.0F) ? ((float(cy) + (stepY > 0 ? 1.0F : 0.0F) - o.y) / dir.y) : big;
				float tMaxZ =
				  (dir.z != 0.0F) ? ((float(cz) + (stepZ > 0 ? 1.0F : 0.0F) - o.z) / dir.z) : big;

				float travelled = 0.0F;
				// Crossing the model takes at most one step per cell of each axis,
				// plus a few for the cells the entry point rounds into.
				const int limit = voxels.GetWidth() + voxels.GetHeight() + voxels.GetDepth() + 8;
				for (int i = 0; i < limit; i++) {
					if (voxels.IsSolid(cx, cy, cz)) { // bounds-checked
						distance = tEnter + travelled; // along the ray as it was given
						return true;
					}
					if (tMaxX <= tMaxY && tMaxX <= tMaxZ) {
						travelled = tMaxX;
						cx += stepX;
						tMaxX += tDeltaX;
					} else if (tMaxY <= tMaxZ) {
						travelled = tMaxY;
						cy += stepY;
						tMaxY += tDeltaY;
					} else {
						travelled = tMaxZ;
						cz += stepZ;
						tMaxZ += tDeltaZ;
					}
				}
				return false;
			}

			// "Object", "Object.001", ... : the first name of that shape the scene
			// does not already use, as Blender numbers duplicates.
			std::string UnusedObjectName(const Scene& scene, const std::string& base) {
				auto taken = [&](const std::string& name) {
					for (SceneObjectId id : scene.Ids()) {
						const SceneNode* node = scene.Find(id);
						if (node && node->name == name)
							return true;
					}
					return false;
				};
				if (!taken(base))
					return base;
				for (int n = 1; n < 1000; n++) {
					char suffix[8];
					std::snprintf(suffix, sizeof(suffix), ".%03d", n);
					if (!taken(base + suffix))
						return base + suffix;
				}
				return base;
			}

			// A fresh object's voxels: one seed voxel, with the pivot on it, as a
			// new model has.
			Handle<VoxelModel> NewObjectModel(int n, std::uint32_t color) {
				Handle<VoxelModel> fresh = Handle<VoxelModel>::New(n, n, n);
				fresh->SetSolid(n / 2, n / 2, n / 2, color);
				const float centre = float(n / 2);
				fresh->SetOrigin(MakeVector3(-centre, -centre, -centre));
				return fresh;
			}
		} // namespace

		// --- Journal ----------------------------------------------------------

		bool SceneEditorView::SceneState::IsSelected(SceneObjectId id) const {
			return std::find(selectedObjects->begin(), selectedObjects->end(), id) !=
			       selectedObjects->end();
		}

		class SceneEditorView::Journal final : public JournaledHostState {
		public:
			explicit Journal(const SceneState& state) : state(state) {}
			const SceneState state;

			bool Equals(const JournaledHostState& other) const override {
				const Journal* o = dynamic_cast<const Journal*>(&other);
				return o && state == o->state;
			}

			std::size_t UnsharedBytes(const JournaledHostState* other) const override {
				const Journal* o = other ? dynamic_cast<const Journal*>(other) : nullptr;
				std::size_t bytes = 0;
				if (!o || !state.scene.Shares(o->state.scene))
					bytes += state.scene->Bytes();
				if (!o || !state.selectedObjects.Shares(o->state.selectedObjects))
					bytes += state.selectedObjects->capacity() * sizeof(SceneObjectId);
				return bytes;
			}
		};

		// --- View -------------------------------------------------------------

		SceneEditorView::SceneEditorView(client::IRenderer* r, client::IAudioDevice* dev,
		                                 client::FontManager* fm, SoftwareCursor* c,
		                                 const std::string& path, bool isNew)
		    : renderer(r), audioDevice(dev), fontManager(fm) {
			SPADES_MARK_FUNCTION();
			if (c) {
				softwareCursor = c;
			} else {
				ownedCursor = std::make_unique<SoftwareCursor>(*renderer);
				softwareCursor = ownedCursor.get();
			}

			ui = Handle<EditorUI>::New(renderer.GetPointerOrNull(), audioDevice.GetPointerOrNull(),
			                           &*fontManager, *this, softwareCursor);
			scenePanel = std::make_unique<ScenePanel>(*this, audioDevice.GetPointerOrNull());
			emptyModel = Handle<VoxelModel>::New(1, 1, 1);
			// The document's model is replaced when another object is edited, and
			// the scene follows it there (see DocumentReframed for growth).
			VoxelEditor::Config config;
			config.scene = this;
			editor = std::make_unique<VoxelEditor>(document, *this, *renderer, *fontManager, *ui,
			                                       *softwareCursor, config);

			// The editor always opens with a document, as the model editor does: a
			// file that exists but cannot be read leaves an untitled scene instead,
			// so Save never overwrites what this editor never managed to read.
			if (isNew || path.empty())
				NewScene(kNewModelSize, path);
			else if (!LoadScene(path))
				NewScene(kNewModelSize, std::string());
		}

		SceneEditorView::~SceneEditorView() { SPADES_MARK_FUNCTION(); }

		EditorMode SceneEditorView::CurrentMode() const {
			// Read before the editor exists (it is built after the document): a
			// scene is set up in Edit mode's space, then opens on its objects.
			return editor ? editor->CurrentMode() : EditorMode::Edit;
		}

		bool SceneEditorView::IsDirty() const {
			return editor->DocumentStateId() != savedDocumentId;
		}

		bool SceneEditorView::HasUnsavedChanges() const {
			return IsDirty() || editor->HasPlacement();
		}

		// --- Objects and their voxels -----------------------------------------

		VoxelModel* SceneEditorView::ModelOf(SceneObjectId id) const {
			auto it = models.find(id);
			return (it == models.end()) ? nullptr : it->second.GetPointerOrNull();
		}

		void SceneEditorView::NoteDocumentEdits() {
			if (document.Version() == documentVersionSeen)
				return;
			documentVersionSeen = document.Version();
			if (documentOwner != kNoSceneObject)
				modelRevision[documentOwner]++;
		}

		void SceneEditorView::RefreshActiveModel() {
			// What the document held so far was its owner's voxels, edited.
			NoteDocumentEdits();
			Handle<VoxelModel> active;
			auto it = models.find(state.activeObject);
			if (it != models.end())
				active = it->second;
			const SceneObjectId owner = active ? state.activeObject : kNoSceneObject;
			// Nothing to edit: the same empty stand-in every time, so nothing
			// downstream sees the document change for no reason. No mode edits it.
			if (!active)
				active = emptyModel;
			documentOwner = owner;
			if (&document.Model() != active.GetPointerOrNull())
				document.Assign(active);
			documentVersionSeen = document.Version();
			// Losing the object being edited leaves Edit mode with nothing to work
			// on, so the mode (and the tool with it) follows.
			if (editor)
				editor->ReconcileMode();
		}

		// --- Spaces -----------------------------------------------------------

		Matrix4 SceneEditorView::ToolsToScene(EditorMode mode, const SceneState& in) const {
			if (mode != EditorMode::Edit || in.activeObject == kNoSceneObject ||
			    !in.scene->Find(in.activeObject))
				return Matrix4::Identity();
			return in.scene->WorldTransform(in.activeObject);
		}

		Matrix4 SceneEditorView::ObjectVoxelMatrix(SceneObjectId id) const {
			// The renderer bakes a model's pivot into its own vertices and the
			// matrix handed to it cancels that (see the render pass), so an
			// object's voxel v ends up at its scene transform times v, as the game
			// composes a scene: voxel coordinates and the scene meet with no pivot
			// in between.
			if (id == kNoSceneObject || !state.scene->Find(id))
				return Matrix4::Identity();
			return ToolsToScene().Inversed() * state.scene->WorldTransform(id);
		}

		Vector3 SceneEditorView::ObjectPivot(SceneObjectId id) const {
			// An object is held by its model's pivot, the point a .kv6 is built
			// about, so the gizmo sits on the object rather than at the corner of
			// its grid.
			const VoxelModel* voxels = ModelOf(id);
			const Vector3 pivot = voxels ? voxels->GetOrigin() * -1.0F : MakeVector3(0, 0, 0);
			return Apply(ObjectVoxelMatrix(id), pivot);
		}

		Matrix4 SceneEditorView::DocumentToToolSpace() const {
			// Edit mode works in the document's own voxels; Object mode shows them
			// at the active object's place in the scene.
			return ObjectVoxelMatrix(documentOwner);
		}

		float SceneEditorView::ViewportLeftInset() const {
			return CurrentMode() == EditorMode::Object ? ScenePanel::kWidth : 0.0F;
		}

		void SceneEditorView::DocumentReframed(const Vector3& shift) {
			// A reframe hands the document a new model holding the same voxels
			// relabelled by `shift`: the scene keeps it under its object.
			if (documentOwner == kNoSceneObject)
				return;
			models[documentOwner] = Handle<VoxelModel>(&document.Model());
			modelRevision[documentOwner]++;
			documentVersionSeen = document.Version();
			// The game places voxel v at the object's transform times v, so the
			// relabelled voxels would move in the scene: the object moves back by
			// as much, and stays where it is. The undo history restores the scene
			// with the step, so a replay lands on the same place.
			if (SceneNode* node = state.scene.Edit().Find(documentOwner)) {
				const Vector3 back = Quaternion(node->rotation).Apply(
				  MakeVector3(node->scale.x * shift.x, node->scale.y * shift.y,
				              node->scale.z * shift.z));
				node->position = node->position - back;
			}
			// The view works in the document's voxels in Edit mode: it follows them.
			if (CurrentMode() == EditorMode::Edit)
				cam.Shift(shift);
		}

		// --- IVoxelSceneHost: the journal -------------------------------------

		bool SceneEditorView::CanEditActiveObject() const {
			// One object at a time is edited, so a document with several picked
			// has no one object to open, as in Blender; and only one in sight,
			// since its voxels are what Edit mode shows.
			return state.selectedObjects->size() == 1 && state.activeObject != kNoSceneObject &&
			       ModelOf(state.activeObject) != nullptr &&
			       state.scene->IsShown(state.activeObject);
		}

		std::shared_ptr<const JournaledHostState> SceneEditorView::SnapshotJournal() const {
			return std::make_shared<Journal>(state);
		}

		void SceneEditorView::RestoreJournal(const std::shared_ptr<const JournaledHostState>& journal,
		                                     EditorMode previousMode) {
			const Journal* restored = dynamic_cast<const Journal*>(journal.get());
			if (!restored)
				return;
			const Matrix4 was = ToolsToScene(previousMode, state);
			state = restored->state;
			// The step may have been one that changed which object is edited, or
			// the mode, so what the tools work on follows the state back. The
			// camera goes with it, as it does when the mode is changed by hand.
			RefreshActiveModel();
			cam.Carry(ToolsToScene().Inversed() * was);
		}

		void SceneEditorView::ModeChanging() {
			// What is being typed into the scene panel belongs to Object mode: it
			// is settled while that mode is still the one in hand.
			if (ScenePanel* panel = ShownScenePanel())
				panel->EndInput(true);
		}

		void SceneEditorView::ModeChanged(EditorMode previous) {
			// Modes work in different spaces (see ToolsToScene); the camera moves
			// with the change, so what was on screen stays where it was.
			cam.Carry(ToolsToScene().Inversed() * ToolsToScene(previous, state));
		}

		void SceneEditorView::EndPreviews() noexcept {
			try {
				if (previewedDrag) {
					const PreviewedDrag drag = *previewedDrag;
					previewedDrag.reset();
					RestoreDragged(drag);
				}
			} catch (const std::exception& ex) {
				// Putting a preview back copies the scene, which can run out of
				// memory. This is called from destructors and while unwinding, so
				// nothing may escape: the preview is dropped and said so.
				SPLog("Could not end a preview: %s", ex.what());
			} catch (...) {
				SPLog("Could not end a preview");
			}
		}

		void SceneEditorView::RestoreDragged(const PreviewedDrag& drag) {
			if (drag.ids.empty())
				return;
			Scene& scene = state.scene.Edit();
			for (std::size_t i = 0; i < drag.ids.size(); i++) {
				SceneNode* node = scene.Find(drag.ids[i]);
				if (!node)
					continue;
				node->position = drag.before[i].position;
				node->rotation = drag.before[i].rotation;
				node->scale = drag.before[i].scale;
			}
		}

		// --- Picking objects --------------------------------------------------

		bool SceneEditorView::IsObjectSelected(SceneObjectId id) const {
			return state.IsSelected(id);
		}

		void SceneEditorView::SetSelectedObjects(std::vector<SceneObjectId> ids,
		                                         const std::string& label) {
			// Only objects the scene still holds, each once, in the order picked.
			std::vector<SceneObjectId> picked;
			for (SceneObjectId id : ids) {
				if (id == kNoSceneObject || !state.scene->Find(id))
					continue;
				if (std::find(picked.begin(), picked.end(), id) == picked.end())
					picked.push_back(id);
			}
			if (picked == *state.selectedObjects)
				return; // already what is picked

			VoxelEditor::DocumentCommand command(*editor);
			VoxelUndoStack::Step step(editor->History(), label);
			// A voxel selection names voxels of the object it was made in.
			if ((picked.empty() ? kNoSceneObject : picked.back()) != state.activeObject)
				editor->ClearSelection();
			PickObjectsRaw(std::move(picked));
		}

		void SceneEditorView::PickObjectsRaw(std::vector<SceneObjectId> ids) {
			const SceneObjectId active = ids.empty() ? kNoSceneObject : ids.back();
			state.selectedObjects.Edit() = std::move(ids);
			state.activeObject = active;
			// What Edit mode works on is the object picked last, so the document
			// follows the pick.
			RefreshActiveModel();
		}

		void SceneEditorView::SelectObject(SceneObjectId id) {
			if (id == kNoSceneObject) {
				SetSelectedObjects({}, "Deselect Objects");
				return;
			}
			SetSelectedObjects({id}, "Select Object");
		}

		void SceneEditorView::ToggleObjectSelected(SceneObjectId id) {
			if (id == kNoSceneObject)
				return;
			std::vector<SceneObjectId> picked = *state.selectedObjects;
			auto at = std::find(picked.begin(), picked.end(), id);
			if (at != picked.end()) {
				picked.erase(at);
				SetSelectedObjects(std::move(picked), "Deselect Object");
				return;
			}
			// The newly picked object becomes the active one, as the last picked.
			picked.push_back(id);
			SetSelectedObjects(std::move(picked), "Select Object");
		}

		void SceneEditorView::SelectAllObjects() {
			const Scene& scene = *state.scene;
			const SceneNode* root = scene.Root();
			std::vector<SceneObjectId> all;
			bool everyOne = true;
			for (SceneObjectId id : scene.Ids()) {
				if (root && root->id == id)
					continue;
				all.push_back(id);
				everyOne = everyOne && state.IsSelected(id);
			}
			if (everyOne) {
				SetSelectedObjects({}, "Deselect All");
				return;
			}
			// The active object stays active: it goes last, where that is read.
			auto active = std::find(all.begin(), all.end(), state.activeObject);
			if (active != all.end())
				std::rotate(active, active + 1, all.end());
			SetSelectedObjects(std::move(all), "Select All");
		}

		SceneObjectId SceneEditorView::ObjectAtCursor() { return PickObject(nullptr); }

		SceneObjectId SceneEditorView::PickObject(Vector3* hitPoint) {
			const GizmoView& view = cam.View();
			if (!view.IsValid())
				return kNoSceneObject;
			Vector3 eye, dir;
			view.Ray(softwareCursor->GetPosition(), eye, dir);

			// Objects are picked where they are drawn: each is traced in its own
			// voxel space, so its transform costs one matrix inverse rather than a
			// ray per voxel.
			SceneObjectId hit = kNoSceneObject;
			float nearest = std::numeric_limits<float>::infinity();
			for (SceneObjectId id : state.scene->ObjectIds()) {
				const VoxelModel* voxels = ModelOf(id);
				const SceneNode* node = state.scene->Find(id);
				if (!voxels || !node || !state.scene->IsShown(id))
					continue; // what is out of sight cannot be clicked on
				// A scale of zero flattens the object away: it has no inverse, and
				// nothing of it is on screen to be picked.
				if (node->scale.x == 0.0F || node->scale.y == 0.0F || node->scale.z == 0.0F)
					continue;
				const Matrix4 world = ObjectVoxelMatrix(id);
				const Matrix4 toLocal = world.Inversed();
				const Vector3 localEye = Apply(toLocal, eye);
				const Vector3 localDir = (toLocal * MakeVector4(dir.x, dir.y, dir.z, 0.0F)).GetXYZ();
				float localDistance = 0.0F;
				if (!RayVoxelHit(*voxels, localEye, localDir, localDistance))
					continue;
				// Back to the tools' space: objects may be scaled differently, so a
				// local distance says nothing about which one is in front.
				const Vector3 worldHit = Apply(world, localEye + localDir * localDistance);
				const float distance = Vector3::Dot(worldHit - eye, dir);
				if (distance >= 0.0F && distance < nearest) {
					nearest = distance;
					hit = id;
					if (hitPoint)
						*hitPoint = worldHit;
				}
			}
			return hit;
		}

		Vector3 SceneEditorView::OutlineColorOf(SceneObjectId id) const {
			// Every model is outlined (the scene asks for it); a picked object is
			// outlined in orange and the active one in a lighter shade, which is
			// how Blender tells the two apart.
			if (CurrentMode() != EditorMode::Object || !state.IsSelected(id))
				return MakeVector3(0.0F, 0.0F, 0.0F);
			return (id == state.activeObject) ? MakeVector3(1.0F, 0.66F, 0.25F)
			                                  : MakeVector3(0.92F, 0.38F, 0.05F);
		}

		// --- Adding and removing objects ----------------------------------------

		SceneObjectId SceneEditorView::AddSceneObject(const std::string& name,
		                                              Handle<VoxelModel> voxels,
		                                              const std::string& label) {
			if (!voxels)
				return kNoSceneObject;
			VoxelEditor::DocumentCommand command(*editor);
			VoxelUndoStack::Step step(editor->History(), label);
			editor->History().MarkDocumentChanged(); // the scene's objects are part of the file
			SceneNode node;
			{
				Scene& scene = state.scene.Edit();
				node.id = scene.nextId++;
				node.name = UnusedObjectName(scene, name.empty() ? "Object" : name);
				node.hasModel = true;
				// New objects land where the user is looking, on whole voxels, so
				// one never hides inside another at the origin.
				const Vector3 looked = Apply(scene.TopLevelTransform().Inversed() * ToolsToScene(),
				                             cam.Target());
				node.position = MakeVector3(std::round(looked.x), std::round(looked.y),
				                            std::round(looked.z));
				scene.ObjectHome().push_back(node);
			}
			models[node.id] = voxels;
			modelRevision[node.id]++;
			// The new object is the one being worked on, as it is in Blender, and
			// the only one picked.
			editor->ClearSelection();
			PickObjectsRaw({node.id});
			return node.id;
		}

		SceneObjectId SceneEditorView::CreateObject() {
			return AddSceneObject(std::string(),
			                      NewObjectModel(kNewModelSize, editor->CurrentColor()),
			                      "New Object");
		}

		bool SceneEditorView::DeleteSelectedObjects() {
			if (state.selectedObjects->empty())
				return false;
			// A locked object stays, and so does what holds it.
			if (state.scene->AnyLocked(*state.selectedObjects)) {
				editor->SetStatus("A locked object would go with it: unlock it in Properties first");
				return false;
			}
			VoxelEditor::DocumentCommand command(*editor);
			VoxelUndoStack::Step step(editor->History(), "Delete Objects");
			editor->History().MarkDocumentChanged(); // the scene's objects are part of the file
			// The voxels stay in `models`: the history brings the objects back by
			// id, and nothing else refers to them meanwhile.
			bool removed = false;
			{
				Scene& scene = state.scene.Edit();
				const bool hadRoot = scene.Root() != nullptr;
				for (SceneObjectId id : *state.selectedObjects)
					removed = scene.Remove(id) || removed;
				scene.KeepTopLevel(hadRoot);
			}
			if (!removed)
				return false;
			// What is left takes over, so a scene is never left with objects but
			// nothing picked.
			const std::vector<SceneObjectId> left = state.scene->ObjectIds();
			editor->ClearSelection();
			PickObjectsRaw(left.empty() ? std::vector<SceneObjectId>()
			                            : std::vector<SceneObjectId>{left.front()});
			return true;
		}

		bool SceneEditorView::AddModelFileAsObjects(const std::string& path) {
			const std::string fileName = LocalFileSystem::GetFileName(path);
			std::string baseName = fileName;
			const std::size_t dot = baseName.rfind('.');
			if (dot != std::string::npos && dot > 0)
				baseName.resize(dot);

			const DocumentType* type = FindDocumentType(path);
			if (!type || type->kind != DocumentKind::Scene) {
				// A model file becomes one object, named after the file.
				Handle<VoxelModel> imported = KV6ModelIO::Load(path);
				if (!imported) {
					editor->SetStatus("Could not load " + fileName);
					return false;
				}
				AddSceneObject(baseName, imported, "Add Object");
				editor->SetStatus("Added " + baseName);
				return true;
			}

			// A scene file brings its objects in as they stand, keeping their
			// places relative to one another, as appending a scene does elsewhere.
			std::vector<VoxelObject> objects;
			if (!SceneIO::Load(path, objects)) {
				editor->SetStatus("Could not load " + fileName);
				return false;
			}
			if (objects.empty()) {
				editor->SetStatus(fileName + " holds no objects");
				return false;
			}
			VoxelEditor::DocumentCommand command(*editor);
			VoxelUndoStack::Step step(editor->History(), "Add Objects");
			editor->History().MarkDocumentChanged();
			std::vector<SceneObjectId> added;
			{
				Scene& scene = state.scene.Edit();
				std::vector<SceneNode>& home = scene.ObjectHome();
				for (const VoxelObject& object : objects) {
					home.push_back(ToSceneNode(object, scene, models));
					added.push_back(home.back().id);
				}
				for (const auto& entry : models)
					modelRevision[entry.first]++;
			}
			{
				// Rename what came in, one at a time, each against the scene as it
				// stands, so no two end up sharing a name.
				Scene& scene = state.scene.Edit();
				for (SceneObjectId id : scene.WithDescendants(added)) {
					SceneNode* node = scene.Find(id);
					if (!node)
						continue;
					const std::string wanted = node->name.empty() ? baseName : node->name;
					node->name.clear(); // so the name it holds is not taken by itself
					node->name = UnusedObjectName(scene, wanted);
				}
			}
			editor->ClearSelection();
			PickObjectsRaw(added);
			editor->SetStatus("Added " + std::to_string(added.size()) +
			                  (added.size() == 1 ? " object from " : " objects from ") + baseName);
			return true;
		}

		// --- Placing objects --------------------------------------------------

		bool SceneEditorView::GetSelectionPose(Vector3& position, Quaternion& rotation) const {
			if (state.selectedObjects->empty())
				return false;
			// The gizmo sits at the middle of what is picked and takes the active
			// object's axes, as Blender's median-point local gizmo does.
			Vector3 middle = MakeVector3(0.0F, 0.0F, 0.0F);
			int counted = 0;
			for (SceneObjectId id : *state.selectedObjects) {
				if (!state.scene->Find(id))
					continue;
				middle += ObjectPivot(id);
				counted++;
			}
			if (counted == 0)
				return false;
			position = middle * (1.0F / float(counted));
			// The axes are the active object's as they stand in the scene, so a
			// child of a turned parent still gets a gizmo along its own sides.
			rotation = Quaternion(0.0F, 0.0F, 0.0F, 1.0F);
			if (state.scene->Find(state.activeObject))
				rotation =
				  Quaternion(Decompose(ObjectVoxelMatrix(state.activeObject)).rotation).Normalize();
			return true;
		}

		void SceneEditorView::BeginObjectDrag() {
			EndPreviews(); // a drag starts from where the objects really are
			Vector3 position;
			Quaternion rotation;
			if (!GetSelectionPose(position, rotation))
				return;
			PreviewedDrag drag;
			drag.pivot = position;
			for (int k = 0; k < 3; k++) {
				drag.axes[k] = rotation.Apply(MakeVector3(
				  k == 0 ? 1.0F : 0.0F, k == 1 ? 1.0F : 0.0F, k == 2 ? 1.0F : 0.0F));
			}
			for (SceneObjectId id : *state.selectedObjects) {
				const SceneNode* node = state.scene->Find(id);
				if (!node)
					continue;
				// An object under another picked object is already carried by it,
				// as Blender leaves descendants of a selection to their parents.
				bool carriedByAnother = false;
				for (const SceneNode* above = state.scene->ParentOf(id); above && !carriedByAnother;
				     above = state.scene->ParentOf(above->id))
					carriedByAnother = state.IsSelected(above->id);
				if (carriedByAnother)
					continue;
				drag.ids.push_back(id);
				drag.before.push_back(TransformOf(*node));
				drag.parent.push_back(state.scene->ParentTransform(id));
			}
			if (drag.ids.empty())
				return;
			previewedDrag = std::move(drag);
		}

		void SceneEditorView::PreviewObjectDrag(const GizmoTransform& change) {
			if (!previewedDrag)
				return;
			const PreviewedDrag& drag = *previewedDrag;
			// One matrix carries every picked object, so they keep their places
			// relative to one another.
			const Matrix4 carry = DragTransform(change.translation, change.rotation, change.scale,
			                                    drag.pivot, drag.axes);
			Scene& scene = state.scene.Edit();
			for (std::size_t i = 0; i < drag.ids.size(); i++) {
				SceneNode* node = scene.Find(drag.ids[i]);
				if (!node)
					continue;
				// The drag is in world space, while an object holds its place in
				// its parent's, so the change crosses into that space on the way.
				const Matrix4& parent = drag.parent[i];
				const ObjectTransform& start = drag.before[i];
				const Matrix4 was = Matrix4::Translate(start.position) *
				                    Quaternion(start.rotation).ToRotationMatrix() *
				                    Matrix4::Scale(start.scale);
				const ObjectTransform moved = Decompose(parent.Inversed() * carry * parent * was);
				node->position = moved.position;
				// A locked object is carried along, but keeps its turn and scale.
				node->rotation = node->locked ? start.rotation : moved.rotation;
				node->scale = node->locked ? start.scale : moved.scale;
			}
		}

		void SceneEditorView::CommitObjectDrag(const std::string& label) {
			if (!previewedDrag)
				return;
			// Journal the move from where the objects were to where the drag left
			// them: note both, put them back, then make the change inside a step.
			const PreviewedDrag drag = *previewedDrag;
			std::vector<ObjectTransform> after;
			after.reserve(drag.ids.size());
			for (SceneObjectId id : drag.ids) {
				const SceneNode* node = state.scene->Find(id);
				after.push_back(node ? TransformOf(*node) : ObjectTransform());
			}
			previewedDrag.reset();
			RestoreDragged(drag);

			bool changed = false;
			for (std::size_t i = 0; i < drag.ids.size() && !changed; i++) {
				changed = after[i].position != drag.before[i].position ||
				          after[i].rotation != drag.before[i].rotation ||
				          after[i].scale != drag.before[i].scale;
			}
			if (!changed)
				return; // the drag went nowhere
			VoxelEditor::DocumentCommand command(*editor);
			VoxelUndoStack::Step step(editor->History(), label);
			editor->History().MarkDocumentChanged(); // where objects sit is part of the file
			Scene& scene = state.scene.Edit();
			for (std::size_t i = 0; i < drag.ids.size(); i++) {
				SceneNode* node = scene.Find(drag.ids[i]);
				if (!node)
					continue;
				node->position = after[i].position;
				node->rotation = after[i].rotation;
				node->scale = after[i].scale;
			}
		}

		void SceneEditorView::CancelObjectDrag() { EndPreviews(); }

		ObjectTransform SceneEditorView::AllowedTransform(SceneObjectId id,
		                                                  const ObjectTransform& wanted) const {
			// A locked object may only be moved: it keeps its turn and scale.
			const SceneNode* node = state.scene->Find(id);
			ObjectTransform allowed = wanted;
			if (node && node->locked) {
				allowed.rotation = node->rotation;
				allowed.scale = node->scale;
			}
			return allowed;
		}

		void SceneEditorView::PreviewObjectTransform(SceneObjectId id,
		                                             const ObjectTransform& wanted) {
			if (!state.scene->Find(id))
				return;
			const ObjectTransform transform = AllowedTransform(id, wanted);
			// The preview is a drag of this one object, so everything that ends
			// previews (a command, undo, the end of the user action) puts it back.
			if (!previewedDrag || previewedDrag->ids != std::vector<SceneObjectId>{id}) {
				EndPreviews();
				const SceneNode* node = state.scene->Find(id);
				if (!node)
					return;
				PreviewedDrag drag;
				drag.ids = {id};
				drag.before = {TransformOf(*node)};
				drag.parent = {state.scene->ParentTransform(id)};
				previewedDrag = std::move(drag);
			}
			SceneNode* node = state.scene.Edit().Find(id);
			node->position = transform.position;
			node->rotation = transform.rotation;
			node->scale = transform.scale;
		}

		void SceneEditorView::SetObjectTransform(SceneObjectId id, const ObjectTransform& wanted,
		                                         const std::string& label) {
			// Ends the preview: the step starts from the real place.
			VoxelEditor::DocumentCommand command(*editor);
			const ObjectTransform transform = AllowedTransform(id, wanted);
			const SceneNode* node = state.scene->Find(id);
			if (!node || (node->position == transform.position &&
			              node->rotation == transform.rotation && node->scale == transform.scale))
				return;
			VoxelUndoStack::Step step(editor->History(), label);
			editor->History().MarkDocumentChanged(); // where objects sit is part of the file
			SceneNode* placed = state.scene.Edit().Find(id);
			placed->position = transform.position;
			placed->rotation = transform.rotation;
			placed->scale = transform.scale;
		}

		// --- The scene's tree -------------------------------------------------

		bool SceneEditorView::RenameObject(SceneObjectId id, const std::string& wanted) {
			const SceneNode* root = state.scene->Root();
			const SceneNode* node = state.scene->Find(id);
			if (!node || (root && root->id == id))
				return false;
			if (node->locked) {
				editor->SetStatus("'" + node->name +
				                  "' is locked: unlock it in Properties to rename it");
				return false;
			}
			// Spaces around a name are never meant; a name of nothing but them
			// is no name at all.
			const std::size_t first = wanted.find_first_not_of(" \t");
			if (first == std::string::npos) {
				editor->SetStatus("An object needs a name");
				return false;
			}
			const std::string name = wanted.substr(first, wanted.find_last_not_of(" \t") - first + 1);
			if (name == node->name)
				return true;

			VoxelEditor::DocumentCommand command(*editor);
			VoxelUndoStack::Step step(editor->History(), "Rename Object");
			editor->History().MarkDocumentChanged(); // names are saved with the scene
			Scene& scene = state.scene.Edit();
			SceneNode* renamed = scene.Find(id);
			renamed->name.clear(); // so the name it held is not taken by itself
			renamed->name = UnusedObjectName(scene, name);
			if (renamed->name != name)
				editor->SetStatus("\"" + name + "\" is taken: named \"" + renamed->name + "\"");
			return true;
		}

		bool SceneEditorView::MoveObjects(const std::vector<SceneObjectId>& ids,
		                                  SceneObjectId parent, SceneObjectId before) {
			// Opened first: it ends any preview, so the move starts from where the
			// objects really are.
			VoxelEditor::DocumentCommand command(*editor);
			const std::size_t count = state.scene->TopMost(ids).size();
			// Under a new parent each stays put in the world, as Blender keeps an
			// object in place when it is parented in the outliner.
			Scene moved = *state.scene;
			if (!moved.MoveKeepingWorld(ids, parent, before))
				return false;
			if (moved == *state.scene)
				return true; // dropped where they already were

			VoxelUndoStack::Step step(editor->History(),
			                          count == 1 ? "Move Object in Tree" : "Move Objects in Tree");
			editor->History().MarkDocumentChanged(); // the hierarchy is saved with the scene
			state.scene.Edit() = std::move(moved);
			return true;
		}

		SceneObjectId SceneEditorView::GroupObjects(const std::vector<SceneObjectId>& ids) {
			VoxelEditor::DocumentCommand command(*editor);
			Scene scene = *state.scene;
			const SceneNode* root = scene.Root();
			std::vector<SceneObjectId> members;
			for (SceneObjectId id : scene.TopMost(ids)) {
				if (!(root && root->id == id))
					members.push_back(id);
			}
			if (members.empty())
				return kNoSceneObject;
			if (!scene.CanMove(members, kNoSceneObject)) {
				editor->SetStatus("A locked object cannot be grouped: unlock it in Properties first");
				return kNoSceneObject;
			}

			// The group takes the place of the active member (or the first) in
			// its parent, and sits at the middle of the members, so turning or
			// scaling it goes about them.
			const SceneObjectId anchor =
			  std::find(members.begin(), members.end(), state.activeObject) != members.end()
			    ? state.activeObject
			    : members.front();
			const SceneNode* above = scene.ParentOf(anchor);
			const SceneObjectId parent =
			  (above && !(root && above->id == root->id)) ? above->id : kNoSceneObject;
			const Matrix4 toScene = ToolsToScene();
			Vector3 middle = MakeVector3(0.0F, 0.0F, 0.0F);
			for (SceneObjectId id : members)
				middle += Apply(toScene, ObjectPivot(id));
			middle = middle * (1.0F / float(members.size()));
			const Matrix4 parentWorld =
			  parent != kNoSceneObject ? scene.WorldTransform(parent) : scene.TopLevelTransform();

			SceneNode group;
			group.id = scene.nextId++;
			group.name = UnusedObjectName(scene, "Group");
			group.hasModel = false;
			group.position = Apply(parentWorld.Inversed(), middle);
			const SceneObjectId groupId = group.id;
			{
				std::vector<SceneNode>& siblings =
				  parent != kNoSceneObject ? scene.Find(parent)->children : scene.ObjectHome();
				auto at = std::find_if(siblings.begin(), siblings.end(),
				                       [&](const SceneNode& node) { return node.id == anchor; });
				siblings.insert(at, std::move(group));
			}
			if (!scene.MoveKeepingWorld(members, groupId, kNoSceneObject))
				return kNoSceneObject;

			VoxelUndoStack::Step step(editor->History(), "Group Objects");
			editor->History().MarkDocumentChanged(); // the hierarchy is saved with the scene
			state.scene.Edit() = std::move(scene);
			editor->ClearSelection();
			PickObjectsRaw({groupId}); // the group is what moves them together now
			return groupId;
		}

		bool SceneEditorView::UngroupObjects(const std::vector<SceneObjectId>& ids) {
			VoxelEditor::DocumentCommand command(*editor);
			Scene scene = *state.scene;
			const bool hadRoot = scene.Root() != nullptr;
			std::vector<SceneObjectId> freed;
			bool dissolved = false;
			// Parents first, so a group inside another is dissolved from where
			// its parent's dissolving left it.
			for (SceneObjectId id : scene.Ids()) {
				if (std::find(ids.begin(), ids.end(), id) == ids.end())
					continue;
				const SceneNode* root = scene.Root();
				const SceneNode* node = scene.Find(id);
				if (!node || node->hasModel || (root && root->id == id))
					continue; // an object with voxels is no group
				std::vector<SceneObjectId> children;
				for (const SceneNode& child : node->children)
					children.push_back(child.id);
				const SceneNode* above = scene.ParentOf(id);
				const SceneObjectId parent =
				  (above && !(root && above->id == root->id)) ? above->id : kNoSceneObject;
				// They take the group's place, just before it, then it goes.
				if (!children.empty() && !scene.MoveKeepingWorld(children, parent, id))
					continue;
				scene.Remove(id);
				freed.erase(std::remove(freed.begin(), freed.end(), id), freed.end());
				freed.insert(freed.end(), children.begin(), children.end());
				dissolved = true;
			}
			if (!dissolved) {
				editor->SetStatus("Only a group (an object with no voxels) can be ungrouped");
				return false;
			}
			scene.KeepTopLevel(hadRoot);

			VoxelUndoStack::Step step(editor->History(), "Ungroup");
			editor->History().MarkDocumentChanged(); // the hierarchy is saved with the scene
			state.scene.Edit() = std::move(scene);
			editor->ClearSelection();
			PickObjectsRaw(std::move(freed));
			return true;
		}

		void SceneEditorView::AskToSetObjectLocked(SceneObjectId id, bool locked) {
			const SceneNode* node = state.scene->Find(id);
			if (!node || node->locked == locked)
				return;
			const std::string name = "'" + node->name + "'";
			const std::string text =
			  locked ? "Lock " + name +
			             "? It keeps its name, its place in the tree, its turn and its scale, and "
			             "cannot be deleted until it is unlocked. It can still be moved."
			         : "Unlock " + name +
			             "? It can then be renamed, put under another object, turned, scaled or "
			             "deleted, and no longer belongs to its template: the game refuses a "
			             "character skin with an unlocked joint.";
			UIOverlayHost& overlay = *ui->GetOverlay();
			Handle<MessageBoxScreen> box = Handle<MessageBoxScreen>::New(
			  &overlay.GetUIManager().GetRootElement(), text,
			  std::vector<std::string>{locked ? "Lock" : "Unlock", "Cancel"}, 200.0F);
			box->closed = [this, id, locked](ui::UIElement& sender) {
				MessageBoxScreen* answer = dynamic_cast<MessageBoxScreen*>(&sender);
				if (answer && answer->resultIndex == 0)
					SetObjectLocked(id, locked);
			};
			editor->ReleaseHeldInput();
			overlay.Show(box.GetPointerOrNull());
		}

		void SceneEditorView::SetObjectLocked(SceneObjectId id, bool locked) {
			VoxelEditor::DocumentCommand command(*editor);
			const SceneNode* node = state.scene->Find(id);
			if (!node || node->locked == locked)
				return;
			const std::string name = node->name;
			VoxelUndoStack::Step step(editor->History(), locked ? "Lock Object" : "Unlock Object");
			editor->History().MarkDocumentChanged(); // the lock is saved with the scene
			state.scene.Edit().Find(id)->locked = locked;
			editor->SetStatus((locked ? "Locked '" : "Unlocked '") + name + "'");
		}

		void SceneEditorView::SetObjectsHidden(const std::vector<SceneObjectId>& ids, bool hidden) {
			VoxelEditor::DocumentCommand command(*editor);
			std::vector<SceneObjectId> changing;
			for (SceneObjectId id : ids) {
				const SceneNode* node = state.scene->Find(id);
				if (node && node->hidden != hidden)
					changing.push_back(id);
			}
			if (changing.empty())
				return;
			VoxelUndoStack::Step step(editor->History(), hidden ? "Hide Objects" : "Show Objects");
			editor->History().MarkDocumentChanged(); // being hidden is saved with the scene
			Scene& scene = state.scene.Edit();
			for (SceneObjectId id : changing)
				scene.Find(id)->hidden = hidden;
			// Edit mode shows the voxels of an object in sight only.
			editor->ReconcileMode();
		}

		// --- Character skins --------------------------------------------------

		std::uint64_t SceneEditorView::VoxelsVersion() const {
			std::uint64_t version = document.Version();
			for (const auto& entry : modelRevision)
				version += entry.second;
			return version;
		}

		CharacterSkinCheck SceneEditorView::CheckCharacterSkin() {
			// Checked once per change: the check reads every voxel.
			const std::uint64_t voxels = VoxelsVersion();
			if (skinCheckCache.valid && skinCheckCache.sceneVersion == state.scene.Version() &&
			    skinCheckCache.voxelsVersion == voxels)
				return skinCheckCache.result;

			CharacterSkinCheck check;
			const Scene& scene = *state.scene;
			auto holdsPlayer = [](const std::vector<SceneNode>& nodes) {
				return std::any_of(nodes.begin(), nodes.end(),
				                   [](const SceneNode& node) { return node.name == "Player"; });
			};
			check.isSkin = holdsPlayer(scene.roots) ||
			               (scene.Root() && holdsPlayer(scene.Root()->children));
			if (check.isSkin) {
				// The game's own rules, on the scene as it would be saved.
				try {
					CharacterSkin::FromScene(SceneAsObjects());
					check.valid = true;
				} catch (const CharacterSkinError& ex) {
					check.problem = ex.what();
					check.culprit = FindByPath(scene, ex.path);
				} catch (const std::exception& ex) {
					check.problem = ex.what();
				}
			}
			skinCheckCache.valid = true;
			skinCheckCache.sceneVersion = state.scene.Version();
			skinCheckCache.voxelsVersion = voxels;
			skinCheckCache.result = check;
			return check;
		}

		void SceneEditorView::NewCharacterSkin() {
			ConfirmDiscardChanges([this] {
				// The game's own classic body, as the Rifle class wears it; the
				// Christmas hat is shared by every class.
				OpenCharacterTemplate(
				  [](const std::string& file) {
					  const std::string paths[] = {"Models/Player/Rifle/" + file,
					                               "Models/Player/" + file};
					  for (const std::string& path : paths) {
						  if (!FileManager::FileExists(path.c_str()))
							  continue;
						  try {
							  std::unique_ptr<IStream> stream = FileManager::OpenForReading(path.c_str());
							  return VoxelModel::LoadKV6(*stream);
						  } catch (const std::exception& ex) {
							  SPLog("Could not read %s: %s", path.c_str(), ex.what());
						  }
					  }
					  return Handle<VoxelModel>();
				  },
				  "the classic body");
			});
		}

		void SceneEditorView::OpenImportClassicPartsDialog() {
			ConfirmDiscardChanges([this] {
				// Picking any part of the set names its folder; the others are
				// found beside it by their classic names.
				ShowFileDialog("Pick a Part of the Set (Torso.kv6...)", FileBrowserPurpose::Open,
				               std::string(), {DefaultExtension(DocumentKind::Model)},
				               [this](const std::string& picked) {
					               const std::string folder = LocalFileSystem::ParentDir(picked);
					               OpenCharacterTemplate(
					                 [folder](const std::string& file) {
						                 return KV6ModelIO::Load(LocalFileSystem::Join(folder, file));
					                 },
					                 LocalFileSystem::GetFileName(folder));
				               });
			});
		}

		void SceneEditorView::OpenCharacterTemplate(
		  const std::function<Handle<VoxelModel>(const std::string&)>& part,
		  const std::string& from) {
			OpenSceneObjects(CharacterSkin::BuildTemplate(part), std::string());
			const CharacterSkinCheck check = CheckCharacterSkin();
			editor->SetStatus("New character skin from " + from +
			                  (check.valid ? std::string() : ": " + check.problem));
		}

		// --- Document ---------------------------------------------------------

		std::vector<VoxelObject> SceneEditorView::SceneAsObjects() const {
			std::vector<VoxelObject> objects;
			for (const SceneNode& root : state.scene->roots)
				objects.push_back(ToVoxelObject(root, models));
			return objects;
		}

		void SceneEditorView::NewScene(int n, const std::string& path) {
			// A scene is a root holding objects: the root itself carries no voxels,
			// so it is never edited or counted, and moving it moves the lot. One
			// object comes with it, so there is something to edit. This is the
			// document's starting state, so it is built outside the history.
			std::vector<VoxelObject> objects(1);
			objects[0].name = "Scene";
			objects[0].children.resize(1);
			objects[0].children[0].name = "Object";
			objects[0].children[0].model = NewObjectModel(n, editor->CurrentColor());
			OpenSceneObjects(objects, path);
		}

		bool SceneEditorView::LoadScene(const std::string& path) {
			std::vector<VoxelObject> objects;
			if (!SceneIO::Load(path, objects)) {
				// The document that is open is not the one that failed to load, so it
				// stays exactly as it is; the caller says how to break the news.
				editor->SetStatus("Could not load " + path);
				return false;
			}
			OpenSceneObjects(objects, path);
			return true;
		}

		void SceneEditorView::OpenSceneObjects(const std::vector<VoxelObject>& objects,
		                                       const std::string& path) {
			previewedDrag.reset();
			state = SceneState();
			models.clear();
			modelRevision.clear();
			objectRenderModels.clear();
			skinCheckCache.valid = false;
			{
				Scene& scene = state.scene.Edit();
				for (const VoxelObject& object : objects)
					scene.roots.push_back(ToSceneNode(object, scene, models));
			}
			// The scene opens with its first object picked, so Edit mode has
			// something to open and the gizmo something to hold.
			const SceneObjectId first = state.scene->FirstModelObject();
			PickObjectsRaw(first == kNoSceneObject ? std::vector<SceneObjectId>()
			                                       : std::vector<SceneObjectId>{first});
			filePath = path;
			// A scene opens on its objects, as Blender does.
			editor->DocumentReplaced(EditorMode::Object);
			FrameCamera();
			// A file just read is clean; a document made here has yet to be saved.
			savedDocumentId = path.empty() ? -1 : editor->DocumentStateId();
		}

		bool SceneEditorView::Save() {
			// A document that has never been saved has nowhere to go yet, so "save"
			// means asking where. Asked before anything is committed, so cancelling
			// the dialog leaves the document exactly as it was.
			if (filePath.empty()) {
				OpenSaveAsDialog();
				return false;
			}

			// Saving writes the document, so anything still pending belongs in it.
			VoxelEditor::DocumentCommand command(*editor);
			if (!SceneIO::Save(SceneAsObjects(), filePath)) {
				editor->SetStatus("Save failed");
				return false;
			}
			savedDocumentId = editor->DocumentStateId(); // this document state is now clean
			editor->SetStatus("Saved " + filePath);
			return true;
		}

		std::string SceneEditorView::GetMenuTitle() {
			const std::string name = filePath.empty() ? std::string("Untitled scene")
			                                          : LocalFileSystem::GetFileName(filePath);
			return HasUnsavedChanges() ? name + " *" : name;
		}

		void SceneEditorView::NewDocument() {
			ConfirmDiscardChanges([this] {
				NewScene(kNewModelSize, std::string());
				editor->SetStatus("New scene");
			});
		}

		std::vector<EditorMenuItem> SceneEditorView::GetMenuItems() {
			std::vector<EditorMenuItem> items;
			// The file commands first, then the one command that edits the open
			// document with a file: adding a model brings it in as objects.
			items.push_back(EditorMenuItem{"New Scene", [this] { NewDocument(); }, true});
			items.push_back(EditorMenuItem{"Open...", [this] { OpenDocument(); }, true});
			items.push_back(EditorMenuItem{"New Character Skin", [this] { NewCharacterSkin(); }, true});
			items.push_back(EditorMenuItem{"New Skin from Classic Parts...",
			                               [this] { OpenImportClassicPartsDialog(); }, true});
			// Always available: a document with nowhere to go yet is asked where.
			items.push_back(EditorMenuItem{"Save", [this] { Save(); }, true});
			items.push_back(EditorMenuItem{"Save As...", [this] { OpenSaveAsDialog(); }, true});
			items.push_back(EditorMenuItem{"Add Model...", [this] { OpenInsertDialog(); }, true});
			items.push_back(EditorMenuItem{
			  "Exit to Menu", [this] { ConfirmDiscardChanges([this] { wantsClose = true; }); }, true});
			return items;
		}

		bool SceneEditorView::OnMenuEscape() {
			// The menu asks before opening, so it only opens once nothing is left
			// to back out of.
			return editor->EscapeOnce();
		}

		void SceneEditorView::ShowMessage(const std::string& text) {
			editor->ReleaseHeldInput();
			ShowEditorMessage(*ui->GetOverlay(), text);
		}

		void SceneEditorView::ConfirmDiscardChanges(std::function<void()> proceed) {
			if (!proceed)
				return;
			if (!HasUnsavedChanges()) {
				proceed();
				return;
			}
			editor->ReleaseHeldInput();
			gui::ConfirmDiscardChanges(*ui->GetOverlay(), "This scene has unsaved changes.",
			                           [this](std::function<void()> after) {
				                           // A document that was never saved needs
				                           // somewhere to go first; `after` then runs
				                           // only if that save succeeds.
				                           if (filePath.empty())
					                           OpenSaveAsDialog(after);
				                           else if (Save())
					                           after();
			                           },
			                           proceed);
		}

		namespace {
			// What the scene editor opens or takes in: every type an editor opens.
			std::vector<std::string> EditableExtensions() {
				std::vector<std::string> extensions;
				for (const DocumentType& type : DocumentTypes()) {
					if (type.editable)
						extensions.push_back(type.extension);
				}
				return extensions;
			}
		} // namespace

		void SceneEditorView::ShowFileDialog(const std::string& title, FileBrowserPurpose purpose,
		                                     const std::string& initialName,
		                                     const std::vector<std::string>& extensions,
		                                     std::function<void(const std::string&)> picked) {
			// The same options the editor tab on the main screen is built from, so
			// a file behaves the same way wherever it is picked. The browser takes
			// a name that already ends in a listed type at its word, so only the
			// types this dialog is about are listed.
			FileBrowserOptions options = EditorBrowserOptions();
			options.purpose = purpose;
			options.filters.clear();
			options.filters.push_back(FileFilter{DocumentFilterLabel(), extensions});
			options.homeDir = EditorHomeDir();
			// The document's own folder is where this document's dialogs belong;
			// anything else opens where the player last was, here or on the main
			// screen.
			options.initialDir = filePath.empty() ? EditorRememberedFolder(EditorHomeDir())
			                                      : LocalFileSystem::ParentDir(filePath);
			options.initialName = initialName;

			// A dialog takes over input: nothing should stay held while it is up.
			editor->ReleaseHeldInput();
			ShowEditorFileDialog(*ui->GetOverlay(), title, std::move(options), std::move(picked));
		}

		void SceneEditorView::OpenDocument() {
			ConfirmDiscardChanges([this] {
				ShowFileDialog("Open", FileBrowserPurpose::Open, std::string(), EditableExtensions(),
				               [this](const std::string& path) {
					               const DocumentType* type = FindDocumentType(path);
					               if (type && type->kind != DocumentKind::Scene) {
						               // Another kind of document opens in its own
						               // editor, which takes over from this one.
						               handOffPath = path;
						               wantsClose = true;
						               return;
					               }
					               if (!LoadScene(path))
						               ShowMessage("Could not open " +
						                           LocalFileSystem::GetFileName(path) + ".");
				               });
			});
		}

		void SceneEditorView::OpenSaveAsDialog(std::function<void()> after) {
			const std::string name = filePath.empty() ? UntitledFileName(DocumentKind::Scene)
			                                          : LocalFileSystem::GetFileName(filePath);
			ShowFileDialog("Save As", FileBrowserPurpose::Save, name,
			               {DefaultExtension(DocumentKind::Scene)},
			               [this, after](const std::string& path) {
				               if (SaveDocument(path) && after)
					               after();
			               });
		}

		void SceneEditorView::OpenInsertDialog() {
			// Models join the scene as objects, and so do a scene's own objects.
			ShowFileDialog("Add Model", FileBrowserPurpose::Open, std::string(),
			               EditableExtensions(),
			               [this](const std::string& path) { InsertModel(path); });
		}

		void SceneEditorView::InsertModel(const std::string& path) {
			// Object mode works on whole objects, so a model brought in there
			// joins the scene as an object, and so does a whole scene whichever
			// mode asks for it: its objects have nowhere else to go. In Edit mode
			// a model arrives as voxels for the object being edited.
			const DocumentType* type = FindDocumentType(path);
			const bool sceneFile = type && type->kind == DocumentKind::Scene;
			if (CurrentMode() != EditorMode::Edit || sceneFile) {
				AddModelFileAsObjects(path);
				return;
			}
			Handle<VoxelModel> imported = KV6ModelIO::Load(path);
			if (!imported) {
				editor->SetStatus("Could not load " + path);
				return;
			}
			editor->InsertModel(*imported);
		}

		bool SceneEditorView::SaveDocument(const std::string& path) {
			// A scene is written as a scene, whatever the name it is saved under
			// says, so the name says what is in the file.
			const std::string& wanted = DefaultExtension(DocumentKind::Scene);
			std::string target = path;
			if (!LocalFileSystem::HasExtension(target, wanted)) {
				const std::string name = LocalFileSystem::GetFileName(target);
				const std::size_t dot = name.rfind('.');
				if (dot != std::string::npos && dot > 0)
					target.resize(target.size() - (name.size() - dot));
				target += wanted;
			}

			// The document only moves to the new path once the write has succeeded:
			// a failed Save As leaves it where it was, rather than pointing it at a
			// file it could not produce and offering to "save" there again.
			const std::string previousPath = filePath;
			filePath = target;
			if (Save())
				return true;

			filePath = previousPath;
			return false;
		}

		// --- Camera -----------------------------------------------------------

		bool SceneEditorView::ObjectsBounds(const std::vector<SceneObjectId>& ids, Vector3& lo,
		                                    Vector3& hi) const {
			bool any = false;
			for (SceneObjectId id : state.scene->WithDescendants(ids)) {
				const VoxelModel* voxels = ModelOf(id);
				if (!voxels || !state.scene->IsShown(id))
					continue;
				const Matrix4 toTools = ObjectVoxelMatrix(id);
				const Vector3 extent = MakeVector3(float(voxels->GetWidth()) - 0.5F,
				                                   float(voxels->GetHeight()) - 0.5F,
				                                   float(voxels->GetDepth()) - 0.5F);
				for (int corner = 0; corner < 8; corner++) {
					const Vector3 c = Apply(toTools, MakeVector3((corner & 1) ? extent.x : -0.5F,
					                                             (corner & 2) ? extent.y : -0.5F,
					                                             (corner & 4) ? extent.z : -0.5F));
					if (!any) {
						lo = hi = c;
						any = true;
						continue;
					}
					lo = MakeVector3(std::min(lo.x, c.x), std::min(lo.y, c.y), std::min(lo.z, c.z));
					hi = MakeVector3(std::max(hi.x, c.x), std::max(hi.y, c.y), std::max(hi.z, c.z));
				}
			}
			return any;
		}

		namespace {
			void FrameOf(const Vector3& lo, const Vector3& hi, Vector3& centre, float& size) {
				centre = (lo + hi) * 0.5F;
				const Vector3 extent = hi - lo;
				size = std::max(1.0F, std::max(extent.x, std::max(extent.y, extent.z)));
			}
		} // namespace

		void SceneEditorView::FrameCamera() {
			// The whole scene, as it opens.
			Vector3 lo, hi, centre;
			float size = float(kNewModelSize);
			centre = MakeVector3(0.0F, 0.0F, 0.0F);
			if (ObjectsBounds(state.scene->ObjectIds(), lo, hi))
				FrameOf(lo, hi, centre, size);
			cam.Frame(centre, size);
		}

		void SceneEditorView::ResetView() {
			Vector3 lo, hi, centre = MakeVector3(0.0F, 0.0F, 0.0F);
			float size = float(kNewModelSize);
			if (ObjectsBounds(state.scene->ObjectIds(), lo, hi))
				FrameOf(lo, hi, centre, size);
			cam.FlyHome(centre, size);
			editor->SetStatus("View reset");
		}

		bool SceneEditorView::ViewKey(const std::string& key) {
			if (EqualsIgnoringCase(key, kHomeViewKey)) {
				ResetView();
				return true;
			}
			if (!EqualsIgnoringCase(key, kFrameSelectionKey))
				return false;
			// What the mode works on: the voxels selected in Edit mode, the
			// objects picked in Object mode; else all of it.
			Vector3 lo, hi, centre;
			float size;
			bool picked = false;
			if (CurrentMode() == EditorMode::Edit) {
				IntVector3 vlo, vhi;
				picked = editor->SelectionBounds(vlo, vhi);
				if (!picked) {
					vlo = MakeIntVector3(0, 0, 0);
					vhi = document.Size() - MakeIntVector3(1, 1, 1);
				}
				lo = MakeVector3(float(vlo.x), float(vlo.y), float(vlo.z)) - MakeVector3(0.5F, 0.5F, 0.5F);
				hi = MakeVector3(float(vhi.x), float(vhi.y), float(vhi.z)) + MakeVector3(0.5F, 0.5F, 0.5F);
			} else {
				picked = ObjectsBounds(*state.selectedObjects, lo, hi);
				if (!picked && !ObjectsBounds(state.scene->ObjectIds(), lo, hi)) {
					lo = hi = MakeVector3(0.0F, 0.0F, 0.0F);
				}
			}
			FrameOf(lo, hi, centre, size);
			cam.FlyToFrame(centre, size);
			editor->SetStatus(picked ? "Framed the selection" : "Framed the scene");
			return true;
		}

		// --- Host -------------------------------------------------------------

		bool SceneEditorView::KeyIsReserved(const std::string& key) const {
			return BoundToKeySetting(key) || EqualsIgnoringCase(key, kHomeViewKey) ||
			       EqualsIgnoringCase(key, kFrameSelectionKey) || key == "Tab" || key == "F2";
		}

		bool SceneEditorView::ClickViewportWidget(const Vector2& cursor) {
			if (naviCube.HomeAt(cursor)) {
				ResetView();
				return true;
			}
			Vector3 navDir;
			if (!naviCube.DirectionAt(cam.View(), cursor, navDir))
				return false;
			cam.SnapToward(navDir);
			return true;
		}

		client::SceneDefinition SceneEditorView::SetupScene(float width, float height) {
			client::SceneDefinition sceneDef = cam.Scene(width, height);
			// As the model editor's: its own ground, no world, flat light and
			// outlines, so voxels read as the colours they were painted.
			sceneDef.skipWorld = true;
			sceneDef.skipWater = true;
			sceneDef.flatModelLighting = true;
			sceneDef.forceOutlines = true;
			sceneDef.denyCameraBlur = true;
			sceneDef.time = (unsigned int)(globalTime * 1000.0F);
			return sceneDef;
		}

		ScenePanel* SceneEditorView::ShownScenePanel() {
			// The panel is about the scene's objects, which is what Object mode
			// works on; Edit mode is about the voxels of one of them.
			if (CurrentMode() != EditorMode::Object)
				return nullptr;
			return scenePanel.get();
		}

		AABB2 SceneEditorView::ScenePanelBounds(float sh) const {
			const float top = VoxelEditor::BarsHeight();
			return AABB2(0.0F, top, ScenePanel::kWidth, std::max(0.0F, sh - top));
		}

		// --- Drawing ----------------------------------------------------------

		void SceneEditorView::DrawGround() {
			// Opaque but close to the background tone, as the model editor's. It
			// lies in the scene, so it stays put when the view moves into an
			// object to edit it.
			const Vector4 grid = MakeVector4(0.19F, 0.20F, 0.23F, 1.0F);
			const Vector4 gridMajor = MakeVector4(0.30F, 0.32F, 0.37F, 1.0F);
			const Matrix4 sceneToTools = ToolsToScene().Inversed();
			const float z = kGroundPlaneZ;
			const float from = float(-kGroundReach) - 0.5F; // voxel edges, not centres
			const float to = float(kGroundReach) - 0.5F;
			for (int i = -kGroundReach; i <= kGroundReach; i += kGroundStep) {
				const float at = float(i) - 0.5F;
				const bool major = (i % (kGroundStep * kGroundMajorEvery)) == 0;
				const Vector4& color = major ? gridMajor : grid;
				renderer->AddDebugLine(Apply(sceneToTools, MakeVector3(at, from, z)),
				                       Apply(sceneToTools, MakeVector3(at, to, z)), color);
				renderer->AddDebugLine(Apply(sceneToTools, MakeVector3(from, at, z)),
				                       Apply(sceneToTools, MakeVector3(to, at, z)), color);
			}
		}

		void SceneEditorView::DrawVolumeBox() {
			const Vector4 box = MakeVector4(0.4F, 0.7F, 1.0F, 0.5F);
			const IntVector3 size = document.Size();
			const Vector3 a = MakeVector3(-0.5F, -0.5F, -0.5F);
			const Vector3 b =
			  MakeVector3(float(size.x) - 0.5F, float(size.y) - 0.5F, float(size.z) - 0.5F);
			ForEachBoxEdge(a, b,
			               [&](const Vector3& p, const Vector3& q) { renderer->AddDebugLine(p, q, box); });
		}

		// Long axes through the active object's pivot, in its own voxels.
		void SceneEditorView::DrawOriginAxes() {
			const Vector3 org = document.Origin();
			const Vector3 p = MakeVector3(-org.x, -org.y, -org.z);
			const float L = 1000.0F;
			renderer->AddDebugLine(p - MakeVector3(L, 0, 0), p + MakeVector3(L, 0, 0),
			                       MakeVector4(1.0F, 0.3F, 0.3F, 0.5F));
			renderer->AddDebugLine(p - MakeVector3(0, L, 0), p + MakeVector3(0, L, 0),
			                       MakeVector4(0.4F, 1.0F, 0.4F, 0.5F));
			renderer->AddDebugLine(p - MakeVector3(0, 0, L), p + MakeVector3(0, 0, L),
			                       MakeVector4(0.45F, 0.6F, 1.0F, 0.5F));
		}

		void SceneEditorView::DrawRibbon(float sw) {
			client::IFont& font = fontManager->GetSmallGuiFont();
			OverlayColorNP(*renderer, MakeVector4(0.06F, 0.06F, 0.08F, 1.0F));
			OverlayFillRect(*renderer, 0.0F, 0.0F, sw, VoxelEditor::RibbonHeight());

			std::string name = (!filePath.empty()) ? filePath : "(unsaved scene)";
			if (HasUnsavedChanges())
				name += " *";
			const int objects = ObjectCount();
			font.Draw("Scene Editor", MakeVector2(12.0F, 4.0F), 0.95F, MakeVector4(1, 1, 1, 1));
			font.Draw(name + "   (" + std::to_string(objects) +
			            (objects == 1 ? " object)" : " objects)"),
			          MakeVector2(130.0F, 5.0F), 0.85F, MakeVector4(0.75F, 0.75F, 0.78F, 1.0F));

			std::string save = "[Ctrl+S] save";
			Vector2 cs = font.Measure(save);
			font.Draw(save, MakeVector2(sw - 12.0F - cs.x * 0.8F, 5.0F), 0.8F,
			          MakeVector4(0.6F, 0.6F, 0.63F, 1.0F));
		}

		std::string SceneEditorView::HelpLine() const {
			// The camera's keys, named as they are bound: the help never names a
			// key that does something else.
			std::string help = "[MMB] look  |  [Shift+MMB] pan";
			auto keys = [&](std::initializer_list<std::string> bound, const char* what) {
				std::string names;
				for (const std::string& key : bound) {
					if (!key.empty())
						names += (names.empty() ? "" : "/") + key;
				}
				if (!names.empty())
					help += "  |  [" + names + "] " + what;
			};
			keys({cg_keyMoveForward, cg_keyMoveLeft, cg_keyMoveBackward, cg_keyMoveRight}, "move");
			keys({cg_keyJump, cg_keyCrouch}, "up/down");
			keys({cg_keySprint}, "faster");
			help += "  |  [Wheel] zoom";
			auto viewKey = [&](const char* key, const char* what) {
				if (!BoundToKeySetting(key))
					help += std::string("  |  [") + key + "] " + what;
			};
			viewKey(kHomeViewKey, "reset view");
			viewKey(kFrameSelectionKey, "frame selection");
			// Tab moves between the scene's objects and the voxels of the active
			// one, as it does in Blender.
			help += "  |  [Tab] object/edit mode";
			if (CurrentMode() == EditorMode::Object)
				help += "  |  [Ctrl+A] select all  |  [Ctrl+G] group  |  [F2] rename"
				        "  |  [RMB] in the outliner: more";
			return help;
		}

		void SceneEditorView::RefreshRenderModels() {
			NoteDocumentEdits();
			// The active object is drawn from the document, rebuilt once before
			// the frame whatever edits since the last one changed.
			if (!renderModel || renderModelVersion != document.Version()) {
				renderModel = renderer->CreateModel(document.Model());
				renderModelVersion = document.Version();
			}
			// Every other object from its own render model, rebuilt when its
			// voxels have changed since.
			const std::vector<SceneObjectId> ids = state.scene->ObjectIds();
			for (SceneObjectId id : ids) {
				VoxelModel* voxels = ModelOf(id);
				if (!voxels || id == documentOwner)
					continue;
				const std::uint64_t revision = modelRevision[id];
				ObjectRenderModel& entry = objectRenderModels[id];
				if (entry.model && entry.revision == revision)
					continue;
				entry.model = renderer->CreateModel(*voxels);
				entry.revision = revision;
			}
			// An object the scene no longer holds keeps nothing on the GPU. Its
			// voxels themselves stay in `models`, which is how the history brings
			// it back.
			for (auto it = objectRenderModels.begin(); it != objectRenderModels.end();) {
				const bool kept = std::find(ids.begin(), ids.end(), it->first) != ids.end();
				it = kept ? std::next(it) : objectRenderModels.erase(it);
			}
		}

		// --- View interface ---------------------------------------------------

		void SceneEditorView::MouseEvent(float dx, float dy) {
			if (cam.IsLooking() && editor->IsShiftHeld()) { // Shift + wheel-button drag pans
				softwareCursor->Accumulate(dx, dy);
				cam.Pan(dx, dy);
				return;
			}
			if (cam.IsLooking()) {
				cam.Look(dx, dy);
				return;
			}
			softwareCursor->Accumulate(dx, dy);
			// A drag or a scrub begun on the scene panel keeps the pointer until
			// it is let go; hovering over the panel is none of the tool's
			// business either, unless a press the tool took is still held.
			ScenePanel* panel = ShownScenePanel();
			if (panel && !ui->GetOverlay()->IsActive()) {
				const Vector2& cursor = softwareCursor->GetPosition();
				panel->MouseMove(cursor);
				if (panel->IsCapturing())
					return;
				if (panel->Contains(cursor) && !editor->IsPointerHeld())
					return;
			}
			editor->PointerMoved(MakeVector2(dx, dy));
		}

		void SceneEditorView::WheelEvent(float x, float y) {
			if (ui->GetOverlay()->WheelEvent(x, y))
				return;
			if (ui->GetEditorMenu()->IsActive())
				return;
			if (ScenePanel* panel = ShownScenePanel()) {
				if (panel->Wheel(softwareCursor->GetPosition(), y))
					return;
			}
			cam.Zoom(y);
		}

		void SceneEditorView::KeyEvent(const std::string& key, bool down) {
			// Modifier state must track the keyboard even while a modal swallows
			// keys, or a Ctrl released during the menu stays "held" afterwards.
			editor->TrackModifiers(key, down);
			cam.TrackModifiers(key, down);

			// A modal dialog (file browser) owns every key while it is up.
			if (ui->GetOverlay()->KeyEvent(key, down))
				return;

			// A name or a number typed into the scene panel owns the keyboard,
			// Escape included (it abandons the edit rather than open the menu).
			// Mouse buttons are not typing, and Ctrl chords stay shortcuts.
			ScenePanel* panel = ShownScenePanel();
			const bool mouseButton = key == "LeftMouseButton" || key == "RightMouseButton" ||
			                         key == "MiddleMouseButton";
			if (down && panel && !mouseButton && !editor->IsCtrlHeld() && panel->KeyEvent(key))
				return;

			if (ui->GetEditorMenu()->KeyEvent(key, down)) {
				// Releases are swallowed too while the menu is up: drop held
				// movement/look so nothing keeps going once it closes.
				editor->ReleaseHeldInput();
				return;
			}

			const bool objectWork = CurrentMode() == EditorMode::Object;
			if (down && editor->IsCtrlHeld() &&
			    (IsViewShortcut(key, objectWork) || VoxelEditor::IsEditShortcut(key))) {
				// Ctrl is also the default descend key (cg_keyCrouch): undo the
				// drift it caused and stop descending for the rest of this press.
				cam.CancelCtrlDescent();
				// What was typed into the scene panel is settled first.
				if (panel)
					panel->EndInput(true);
				if (VoxelEditor::IsEditShortcut(key)) {
					editor->RunEditShortcut(key);
					return;
				}
				editor->CancelToolInteraction();
				if (EqualsIgnoringCase(key, "s")) {
					if (editor->IsShiftHeld())
						OpenSaveAsDialog();
					else
						Save();
				} else if (EqualsIgnoringCase(key, "n"))
					NewDocument();
				else if (EqualsIgnoringCase(key, "o"))
					OpenDocument();
				// Picking and grouping whole objects is Object mode's.
				else if (EqualsIgnoringCase(key, "a"))
					SelectAllObjects();
				else if (EqualsIgnoringCase(key, "g"))
					GroupObjects(*state.selectedObjects);
				return;
			}

			if (key == "MiddleMouseButton") {
				// Turning and panning go round what is under the cursor, wherever
				// flying has left the orbited point: a voxel of the object being
				// edited, or any object in Object mode.
				if (down) {
					Vector3 at;
					if (CurrentMode() == EditorMode::Object) {
						if (PickObject(&at) != kNoSceneObject)
							cam.OrbitAtDepthOf(at);
					} else {
						editor->DoPick();
						if (editor->HasPick()) {
							const IntVector3 v = editor->PickSolid();
							cam.OrbitAtDepthOf(MakeVector3(float(v.x), float(v.y), float(v.z)));
						}
					}
				}
				cam.SetLooking(down);
				return;
			}
			if (key == "LeftMouseButton") {
				const Vector2& cursor = softwareCursor->GetPosition();
				if (panel) {
					if (!down) {
						panel->MouseUp(cursor);
					} else {
						// The scene panel owns every press on it. A press anywhere
						// else settles what was being typed into it, as a click away
						// from a text field does.
						if (panel->MouseDown(cursor, editor->IsShiftHeld() || editor->IsCtrlHeld(),
						                     globalTime)) {
							if (panel->IsTyping())
								editor->ReleaseHeldInput(); // the keyboard is the panel's now
							return;
						}
						panel->EndInput(true);
					}
				}
				editor->ButtonEvent(PointerButton::Left, down);
				return;
			}
			if (key == "RightMouseButton") {
				// The scene panel's menu, or a right press on the panel itself.
				if (down && panel && panel->RightMouseDown(softwareCursor->GetPosition()))
					return;
				editor->ButtonEvent(PointerButton::Right, down);
				return;
			}

			// A box being typed into owns the keyboard, so none of what follows
			// runs. Releases still fall through, so held keys are let go.
			if (down && editor->OptionBarKey(key))
				return;

			if (down && EditorKeyMatches(cg_keyScreenshot, key)) {
				wantScreenShot = true;
				return;
			}
			// F2 renames the active object, as in Blender and most outliners.
			if (down && key == "F2" && panel) {
				editor->CancelToolInteraction();
				panel->RenameActive();
				editor->ReleaseHeldInput();
				return;
			}
			// Tab moves between the scene's objects and the voxels of the active
			// one, as it does in Blender.
			if (down && key == "Tab") {
				editor->CancelToolInteraction();
				editor->SetMode(CurrentMode() == EditorMode::Edit ? EditorMode::Object
				                                                  : EditorMode::Edit);
				return;
			}
			if (editor->CommandKey(key, down))
				return;
			if (cam.MovementKey(key, down, globalTime))
				return;
			if (down && !editor->IsCtrlHeld() && ViewKey(key))
				return;
			editor->ToolKey(key, down);
		}

		void SceneEditorView::TextInputEvent(const std::string& text) {
			// A dialog takes typing over everything; otherwise it belongs to the
			// scene panel or a number box on the sub-toolbar, when one is being
			// typed into.
			ScenePanel* panel = ShownScenePanel();
			if (ui->GetOverlay()->IsActive())
				ui->GetOverlay()->TextInputEvent(text);
			else if (panel && panel->IsTyping())
				panel->TextInputEvent(text);
			else
				ui->GetOptionBar()->TextInputEvent(text);
		}

		void SceneEditorView::TextEditingEvent(const std::string& text, int start, int len) {
			ui->GetOverlay()->TextEditingEvent(text, start, len);
		}

		bool SceneEditorView::AcceptsTextInput() {
			ScenePanel* panel = ShownScenePanel();
			return ui->GetOverlay()->AcceptsTextInput() || ui->GetOptionBar()->IsEditing() ||
			       (panel && panel->IsTyping());
		}

		AABB2 SceneEditorView::GetTextInputRect() {
			if (ui->GetOverlay()->IsActive())
				return ui->GetOverlay()->GetTextInputRect();
			ScenePanel* panel = ShownScenePanel();
			if (panel && panel->IsTyping())
				return panel->TextInputRect();
			return ui->GetOptionBar()->EditingRect();
		}

		void SceneEditorView::RunFrame(float dt) {
			SPADES_MARK_FUNCTION();
			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();
			globalTime += dt;

			// The movement keys fly the camera unless a modal has the keyboard;
			// its speed follows what is framed, so any scene is crossed as fast.
			Vector3 lo, hi;
			float span = float(kNewModelSize);
			if (ObjectsBounds(state.scene->ObjectIds(), lo, hi)) {
				const Vector3 extent = hi - lo;
				span = std::max(1.0F, std::max(extent.x, std::max(extent.y, extent.z)));
			}
			cam.Update(dt, globalTime, span * kCameraSpeedPerSize,
			           !ui->GetEditorMenu()->IsActive() && !ui->GetOverlay()->IsActive());
			editor->Update(dt);

			renderer->SetFogColor(MakeVector3(0.10F, 0.10F, 0.12F));
			renderer->SetFogDistance(cam.ViewDistance());

			// Rendered full-screen with the bars drawn over the top, as the model
			// editor is, so projection and picking both use the full screen.
			client::SceneDefinition sceneDef = SetupScene(sw, sh);
			RefreshRenderModels();
			renderer->StartScene(sceneDef);
			// Everything is drawn in the space the tools work in (see ToolsToScene),
			// so outlines, gizmos and voxels always agree.
			if (renderModel && documentOwner != kNoSceneObject &&
			    state.scene->IsShown(documentOwner)) {
				client::ModelRenderParam param;
				// Cancel the KV6 pivot so the voxels sit in their own grid.
				param.matrix = ObjectVoxelMatrix(documentOwner) *
				               Matrix4::Translate(document.Origin() * -1.0F);
				param.outlineColor = OutlineColorOf(documentOwner);
				renderer->RenderModel(*renderModel, param);
			}
			for (const auto& entry : objectRenderModels) {
				if (entry.first == documentOwner || !entry.second.model)
					continue;
				const VoxelModel* voxels = ModelOf(entry.first);
				if (!voxels || !state.scene->IsShown(entry.first))
					continue; // deleted, or out of sight
				client::ModelRenderParam param;
				param.matrix =
				  ObjectVoxelMatrix(entry.first) * Matrix4::Translate(voxels->GetOrigin() * -1.0F);
				param.outlineColor = OutlineColorOf(entry.first);
				renderer->RenderModel(*entry.second.model, param);
			}
			DrawGround();
			// The volume box and the pivot axes describe the voxels being edited,
			// so they are drawn only where those voxels are the subject.
			if (CurrentMode() == EditorMode::Edit) {
				DrawVolumeBox();
				DrawOriginAxes();
			}
			editor->DrawScene();
			renderer->EndScene();

			editor->DrawViewportOverlays(sw, sh, HelpLine());
			DrawRibbon(sw);
			editor->DrawBars(sw);

			// The navigation cube, top right under the bars.
			naviCube.Layout(sw - kNaviCubeMargin - kNaviCubeBox * 0.5F,
			                VoxelEditor::BarsHeight() + kNaviCubeTopGap + kNaviCubeBox * 0.5F,
			                kNaviCubeBox * 0.5F - kNaviCubeInset);
			naviCube.Draw(*renderer, fontManager->GetSmallGuiFont(), cam.View(),
			              softwareCursor->GetPosition());

			editor->DrawPanels();
			// Over the viewport and its gizmo: the panel is part of the chrome.
			if (ScenePanel* panel = ShownScenePanel()) {
				panel->Update(ScenePanelBounds(sh));
				panel->Draw(*renderer, *fontManager, softwareCursor->GetPosition(),
				            ui->GetEditorMenu()->IsActive());
			} else {
				// Hidden: nothing may stay half typed or held in it.
				scenePanel->EndInput(true);
			}
			ui->GetEditorMenu()->Draw();

			// Dialogs draw their own pointer (with the text-field I-beam), so the
			// editor's cursor steps aside while one is open.
			ui->GetOverlay()->RunFrame(dt);
			ui->GetOverlay()->Draw();
			if (!ui->GetOverlay()->IsActive())
				softwareCursor->Draw();

			// Shares the client's Screenshots/ numbering, as the model editor does.
			if (wantScreenShot) {
				wantScreenShot = false;
				renderer->FrameDone();
				try {
					std::string name = client::SaveScreenShot(*renderer);
					editor->SetStatus("Screenshot saved: " + name);
				} catch (const std::exception& ex) {
					editor->SetStatus("Screenshot failed");
					SPLog("Editor screenshot failed: %s", ex.what());
				}
			}
		}
	} // namespace gui
} // namespace spades
