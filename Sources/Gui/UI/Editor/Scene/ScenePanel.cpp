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

#include "ScenePanel.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Core/Settings.h>
#include <Gui/OverlayPaint.h>


SPADES_SETTING(cg_keyDelete);

namespace spades {
	namespace gui {
		namespace {
			// The three transform properties, and how their fields are named,
			// stepped and written.
			enum class Property { Position, Rotation, Scale };

			struct PropertyInfo {
				Property property;
				const char* key;     // field ids are "<key>.<axis>"
				const char* caption;
				const char* stepLabel; // names the undo step of a change to it
				float step;
				int decimals;
			};

			// Positions are in voxels, turns in degrees, scale a factor: each
			// steps by what one scrub notch of it is worth.
			const PropertyInfo kProperties[] = {
			  {Property::Position, "position", "Position", "Move Object", 0.25F, 2},
			  {Property::Rotation, "rotation", "Rotation", "Rotate Object", 1.0F, 1},
			  {Property::Scale, "scale", "Scale", "Scale Object", 0.01F, 3},
			};

			const char* const kScaleLockId = "scale.lock";
			const char* const kObjectLockId = "object.lock";
			const char* const kAxisLetters[3] = {"X", "Y", "Z"};
			const Vector4 kAxisColors[3] = {MakeVector4(0.96F, 0.42F, 0.42F, 1.0F),
			                                MakeVector4(0.55F, 0.9F, 0.4F, 1.0F),
			                                MakeVector4(0.45F, 0.62F, 1.0F, 1.0F)};

			std::string FieldId(const PropertyInfo& info, int axis) {
				return std::string(info.key) + "." + kAxisLetters[axis];
			}

			// The property and axis a field id names; false for anything else.
			bool ParseFieldId(const std::string& id, const PropertyInfo*& info, int& axis) {
				for (const PropertyInfo& candidate : kProperties) {
					for (int k = 0; k < 3; k++) {
						if (id == FieldId(candidate, k)) {
							info = &candidate;
							axis = k;
							return true;
						}
					}
				}
				return false;
			}

			float& Component(Vector3& v, int axis) {
				return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
			}
			float Component(const Vector3& v, int axis) {
				return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
			}

			std::string DisplayName(const SceneNode& node) {
				return node.name.empty() ? std::string("(unnamed)") : node.name;
			}

			// The objects the outliner lists: everything but the scene's root,
			// which is infrastructure rather than something to pick.
			const std::vector<SceneNode>& TopLevel(const Scene& scene) {
				const SceneNode* root = scene.Root();
				return root ? root->children : scene.roots;
			}
		} // namespace

		ScenePanel::ScenePanel(ISceneEditContext& editor, client::IAudioDevice* audioDevice)
		    : editor(editor),
		      frame(audioDevice),
		      tree(audioDevice),
		      properties(audioDevice),
		      menu(audioDevice) {
			menu.OnChosen = [this](const std::string& id) { RunCommand(id); };
			frame.SetTabs({"Outliner", "Properties", "Skin"});

			tree.SetEmptyText("No objects yet: [New Object] adds one");
			tree.OnItemClicked = [this](TreeView::ItemId id, bool extend) {
				if (extend)
					this->editor.ToggleObjectSelected(SceneObjectId(id));
				else
					this->editor.SelectObject(SceneObjectId(id));
			};
			tree.OnEmptyClicked = [this] { this->editor.SelectObject(kNoSceneObject); };
			tree.OnRenamed = [this](TreeView::ItemId id, const std::string& name) {
				this->editor.RenameObject(SceneObjectId(id), name);
			};
			tree.CanDrop = [this](TreeView::ItemId dragged, const TreeView::DropTarget& target) {
				const Destination to = Resolve(dragged, target);
				return this->editor.GetScene().CanMove(to.ids, to.parent);
			};
			tree.OnDropped = [this](TreeView::ItemId dragged, const TreeView::DropTarget& target) {
				const Destination to = Resolve(dragged, target);
				this->editor.MoveObjects(to.ids, to.parent, to.before);
			};

			properties.OnNumberChanged = [this](const std::string& id, float value, bool committed) {
				OnNumberChanged(id, value, committed);
			};
			properties.OnToggled = [this](const std::string& id) {
				if (id == kScaleLockId) {
					scaleLocked = !scaleLocked;
				} else if (id == kObjectLockId) {
					// Asked first: unlocking a template's joint breaks the template.
					const SceneObjectId active = this->editor.ActiveObject();
					if (const SceneNode* node = this->editor.GetScene().Find(active))
						this->editor.AskToSetObjectLocked(active, !node->locked);
				}
			};
		}

		void ScenePanel::Update(const AABB2& bounds) {
			frame.SetBounds(bounds);
			tree.SetBounds(frame.Body());
			properties.SetBounds(frame.Body());
			skinCheck = editor.CheckCharacterSkin();
			RefreshTree();
			RefreshProperties();
			// An edit whose gesture is over (settled, or dropped when the
			// active object changed under it) has nothing more to apply.
			if (!properties.IsPressed() && !properties.IsEditing())
				pending.reset();
		}

		void ScenePanel::RefreshTree() {
			const Scene& scene = editor.GetScene();
			const SceneObjectId active = editor.ActiveObject();
			std::vector<TreeView::Item> items;
			items.reserve(scene.Ids().size());
			// Pre-order, as the tree wants it: each object, then what hangs under it.
			struct Walker {
				const ISceneEditContext& editor;
				SceneObjectId active;
				SceneObjectId culprit;
				std::vector<TreeView::Item>& items;
				// `hiddenAbove`: something this hangs under is out of sight,
				// which takes it out of sight too.
				void Add(const SceneNode& node, int depth, bool hiddenAbove) {
					TreeView::Item item;
					item.id = node.id;
					item.depth = depth;
					item.label = DisplayName(node);
					item.selected = editor.IsObjectSelected(node.id);
					item.active = item.selected && node.id == active;
					item.dimmed = !node.hasModel;
					item.flagged = node.id == culprit;
					item.hidden = hiddenAbove || node.hidden;
					if (node.locked)
						item.badge = "locked";
					items.push_back(std::move(item));
					const bool hidden = hiddenAbove || node.hidden;
					for (const SceneNode& child : node.children)
						Add(child, depth + 1, hidden);
				}
			} walker{editor, active, skinCheck.culprit, items};
			const SceneNode* root = scene.Root();
			for (const SceneNode& node : TopLevel(scene))
				walker.Add(node, 0, root && root->hidden);
			tree.SetItems(std::move(items));

			// A pick made anywhere else is brought into view, once.
			if (active != revealed) {
				revealed = active;
				if (active != kNoSceneObject)
					tree.Reveal(active);
			}
		}

		void ScenePanel::RefreshProperties() {
			const Scene& scene = editor.GetScene();
			const SceneObjectId id = editor.ActiveObject();
			const SceneNode* node = scene.Find(id);
			std::vector<PropertyGrid::Row> rows;
			if (!node) {
				PropertyGrid::Row heading;
				heading.caption = "Pick an object to see where it sits";
				rows.push_back(std::move(heading));
				properties.SetContent(kNoSceneObject, std::move(rows));
				return;
			}

			PropertyGrid::Row heading;
			heading.caption = DisplayName(*node);
			heading.toggle.id = kObjectLockId;
			heading.toggle.label = "Locked";
			heading.toggle.on = node->locked;
			rows.push_back(std::move(heading));

			const bool editing = pending && pending->id == id;
			const Vector3 euler = editing ? pending->euler : EulerDegreesOf(node->rotation);
			for (const PropertyInfo& info : kProperties) {
				PropertyGrid::Row row;
				row.caption = info.caption;
				const Vector3& values = info.property == Property::Position   ? node->position
				                        : info.property == Property::Rotation ? euler
				                                                              : node->scale;
				for (int k = 0; k < 3; k++) {
					PropertyGrid::Field field;
					field.id = FieldId(info, k);
					field.axis = kAxisLetters[k];
					field.axisColor = kAxisColors[k];
					field.value = Component(values, k);
					field.step = info.step;
					field.decimals = info.decimals;
					// A locked object may only be moved.
					field.enabled = !node->locked || info.property == Property::Position;
					row.fields.push_back(std::move(field));
				}
				if (info.property == Property::Scale) {
					row.toggle.id = kScaleLockId;
					row.toggle.label = "Lock";
					row.toggle.on = scaleLocked;
				}
				rows.push_back(std::move(row));
			}
			properties.SetContent(id, std::move(rows));
		}

		void ScenePanel::OnNumberChanged(const std::string& fieldId, float value,
		                                    bool committed) {
			const PropertyInfo* info = nullptr;
			int axis = 0;
			if (!ParseFieldId(fieldId, info, axis))
				return;
			const SceneObjectId id = editor.ActiveObject();
			const SceneNode* node = editor.GetScene().Find(id);
			if (!node) {
				pending.reset();
				return;
			}
			// The first value of a gesture: what it changes is measured from here.
			if (!pending || pending->id != id) {
				PendingEdit edit;
				edit.id = id;
				edit.start = TransformOf(*node);
				edit.startEuler = edit.euler = EulerDegreesOf(node->rotation);
				pending = edit;
			}

			ObjectTransform transform = pending->start;
			Vector3 euler = pending->startEuler;
			switch (info->property) {
				case Property::Position: Component(transform.position, axis) = value; break;
				case Property::Rotation:
					Component(euler, axis) = value;
					// Unchanged angles keep the exact turn they started from, so
					// a value typed back to what it was is no step at all.
					if (euler != pending->startEuler)
						transform.rotation = RotationFromEulerDegrees(euler);
					break;
				case Property::Scale: {
					const float from = Component(pending->start.scale, axis);
					if (!scaleLocked)
						Component(transform.scale, axis) = value;
					else if (from != 0.0F)
						transform.scale = pending->start.scale * (value / from);
					else // no ratio to keep from nothing: all three take the value
						transform.scale = MakeVector3(value, value, value);
					break;
				}
			}
			pending->euler = euler;

			if (committed) {
				pending.reset();
				editor.SetObjectTransform(id, transform, info->stepLabel);
			} else {
				editor.PreviewObjectTransform(id, transform);
			}
		}

		ScenePanel::Destination ScenePanel::Resolve(TreeView::ItemId dragged,
		                                                  const TreeView::DropTarget& target) const {
			const Scene& scene = editor.GetScene();
			Destination to;
			// Dragging a picked object carries every picked one, as in Blender.
			const SceneObjectId id = SceneObjectId(dragged);
			to.ids = editor.IsObjectSelected(id) ? editor.SelectedObjects()
			                                     : std::vector<SceneObjectId>{id};
			if (target.item == TreeView::kNoItem)
				return to; // after everything, at the top level

			const SceneObjectId onto = SceneObjectId(target.item);
			if (target.place == TreeView::DropPlace::Inside) {
				to.parent = onto;
				return to;
			}
			// Beside `onto`: under its parent, which for the top level is the
			// root the outliner does not show.
			const SceneNode* root = scene.Root();
			const SceneNode* parent = scene.ParentOf(onto);
			if (parent && !(root && parent->id == root->id))
				to.parent = parent->id;
			if (target.place == TreeView::DropPlace::Before) {
				to.before = onto;
				return to;
			}
			const std::vector<SceneNode>& siblings = parent ? parent->children : scene.roots;
			auto it = std::find_if(siblings.begin(), siblings.end(),
			                       [&](const SceneNode& node) { return node.id == onto; });
			if (it != siblings.end() && std::next(it) != siblings.end())
				to.before = std::next(it)->id;
			return to;
		}

		void ScenePanel::Draw(client::IRenderer& renderer, client::FontManager& fontManager,
		                         const Vector2& cursorPos, bool menuActive) {
			// While the menu is up, nothing under it answers to the pointer.
			const bool pointerElsewhere = menuActive || menu.IsOpen();
			frame.Draw(renderer, fontManager, cursorPos, pointerElsewhere);
			if (frame.ActiveTab() == kOutlinerTab)
				tree.Draw(renderer, fontManager, cursorPos, pointerElsewhere);
			else if (frame.ActiveTab() == kPropertiesTab)
				properties.Draw(renderer, fontManager, cursorPos, pointerElsewhere);
			else
				DrawSkinTab(renderer, fontManager);
			menu.Draw(renderer, fontManager, cursorPos, renderer.ScreenWidth(),
			          renderer.ScreenHeight());
		}

		void ScenePanel::OpenMenu(const Vector2& at) {
			const Scene& scene = editor.GetScene();
			const SceneNode* root = scene.Root();
			const std::vector<SceneObjectId> picked = editor.SelectedObjects();
			bool anyGroup = false, anyParented = false, anyChildren = false, anyShown = false;
			for (SceneObjectId id : picked) {
				const SceneNode* node = scene.Find(id);
				if (!node)
					continue;
				anyGroup = anyGroup || !node->hasModel;
				anyShown = anyShown || !node->hidden;
				anyChildren = anyChildren || !node->children.empty();
				const SceneNode* above = scene.ParentOf(id);
				anyParented = anyParented || (above && !(root && above->id == root->id));
			}
			bool anyObject = false, everyOne = true;
			for (SceneObjectId id : scene.Ids()) {
				if (root && root->id == id)
					continue;
				anyObject = true;
				everyOne = everyOne && editor.IsObjectSelected(id);
			}
			const bool somePicked = !picked.empty();
			const SceneNode* activeNode = scene.Find(editor.ActiveObject());
			const bool hasActive = activeNode != nullptr;
			// Locked objects keep their name, place and existence.
			const bool renamable = hasActive && !activeNode->locked;
			const bool deletable = somePicked && !scene.AnyLocked(picked);
			const bool movable = somePicked && scene.CanMove(picked, kNoSceneObject);

			auto item = [](const char* id, const char* label, std::string hotKey, bool enabled,
			               bool separatorBefore = false) {
				ContextMenu::Item it;
				it.id = id;
				it.label = label;
				it.hotKey = std::move(hotKey);
				it.enabled = enabled;
				it.separatorBefore = separatorBefore;
				return it;
			};
			menu.Open(at,
			          {item("rename", "Rename", "F2", renamable),
			           item("selectHierarchy", "Select Hierarchy", "", anyChildren, true),
			           item("selectAll", anyObject && everyOne ? "Deselect All" : "Select All",
			                "Ctrl+A", anyObject),
			           item("group", "Group under Empty", "Ctrl+G", movable, true),
			           item("ungroup", "Ungroup", "", anyGroup && movable),
			           item("toTop", "Move to Top Level", "", anyParented && movable),
			           // Hides what is picked while any of it shows, else shows it all.
			           item("visibility", anyShown || !somePicked ? "Hide" : "Show", "",
			                somePicked, true),
			           item("new", "New Object", "", true, true),
			           item("delete", "Delete", std::string(cg_keyDelete), deletable)});
		}

		void ScenePanel::RunCommand(const std::string& id) {
			const Scene& scene = editor.GetScene();
			std::vector<SceneObjectId> picked = editor.SelectedObjects();
			if (id == "rename") {
				RenameActive();
			} else if (id == "selectHierarchy") {
				std::vector<SceneObjectId> all = scene.WithDescendants(picked);
				// The active object stays active: it goes last, where that is read.
				auto active = std::find(all.begin(), all.end(), editor.ActiveObject());
				if (active != all.end())
					std::rotate(active, active + 1, all.end());
				editor.SelectObjects(all);
			} else if (id == "selectAll") {
				editor.SelectAllObjects();
			} else if (id == "group") {
				editor.GroupObjects(picked);
			} else if (id == "ungroup") {
				editor.UngroupObjects(picked);
			} else if (id == "toTop") {
				// Only those hanging under something: the rest are there already,
				// and moving them would only shuffle them to the end.
				const SceneNode* root = scene.Root();
				std::vector<SceneObjectId> parented;
				for (SceneObjectId pickedId : picked) {
					const SceneNode* above = scene.ParentOf(pickedId);
					if (above && !(root && above->id == root->id))
						parented.push_back(pickedId);
				}
				editor.MoveObjects(parented, kNoSceneObject, kNoSceneObject);
			} else if (id == "visibility") {
				bool anyShown = false;
				for (SceneObjectId pickedId : picked) {
					const SceneNode* node = scene.Find(pickedId);
					anyShown = anyShown || (node && !node->hidden);
				}
				editor.SetObjectsHidden(picked, anyShown);
			} else if (id == "new") {
				editor.CreateObject();
			} else if (id == "delete") {
				editor.DeleteSelectedObjects();
			}
		}

		namespace {
			// `text` broken into lines no wider than `width`, at spaces.
			std::vector<std::string> Wrap(client::IFont& font, const std::string& text,
			                              float width) {
				std::vector<std::string> lines;
				std::string line;
				std::size_t at = 0;
				while (at <= text.size()) {
					std::size_t end = text.find(' ', at);
					if (end == std::string::npos)
						end = text.size();
					const std::string word = text.substr(at, end - at);
					const std::string joined = line.empty() ? word : line + " " + word;
					if (!line.empty() && font.Measure(joined).x > width) {
						lines.push_back(line);
						line = word;
					} else {
						line = joined;
					}
					at = end + 1;
				}
				if (!line.empty())
					lines.push_back(line);
				return lines;
			}
		} // namespace

		void ScenePanel::DrawSkinTab(client::IRenderer& renderer,
		                                client::FontManager& fontManager) {
			client::IFont& font = fontManager.GetSmallGuiFont();
			const AABB2 body = frame.Body();
			const float pad = 8.0F;
			const float lineHeight = 18.0F;
			const float width = body.GetWidth() - pad * 2.0F;
			float y = body.GetMinY() + pad;
			auto paragraph = [&](const std::string& text, const Vector4& color) {
				for (const std::string& line : Wrap(font, text, width)) {
					font.Draw(line, MakeVector2(body.GetMinX() + pad, y), 1.0F, color);
					y += lineHeight;
				}
				y += lineHeight * 0.5F;
			};
			const Vector4 plain = MakeVector4(0.8F, 0.8F, 0.84F, 1.0F);
			const Vector4 good = MakeVector4(0.5F, 1.0F, 0.6F, 1.0F);
			const Vector4 bad = MakeVector4(1.0F, 0.45F, 0.4F, 1.0F);
			if (!skinCheck.isSkin) {
				paragraph("Not a character skin.", plain);
				paragraph("A skin is a scene with a 'Player' at its top, laid out as the template "
				          "lays it out. Esc menu > New Character Skin starts one from the classic "
				          "body; New Skin from Classic Parts starts one from a folder of them.",
				          plain);
				return;
			}
			if (skinCheck.valid) {
				paragraph("The game can load this skin.", good);
				paragraph("Put it in a mod as Models/Player/Skin.2kv6 to dress every player, or "
				          "as Models/Player/<Weapon>/Skin.2kv6 for one class.",
				          plain);
				return;
			}
			paragraph("The game would refuse this skin:", bad);
			paragraph(skinCheck.problem, plain);
			if (skinCheck.culprit != kNoSceneObject)
				paragraph("The object at fault is marked in the outliner; click here to pick it.",
				          plain);
		}

		bool ScenePanel::RightMouseDown(const Vector2& p) {
			if (menu.IsOpen()) {
				menu.MouseDown(p);
				return true;
			}
			if (!frame.Contains(p))
				return false;
			EndInput(true);
			if (frame.ActiveTab() != kOutlinerTab || p.y < frame.Body().GetMinY())
				return true;
			// As in any outliner, the menu is about the object clicked: it is
			// picked first, unless it is among the picked already.
			const TreeView::ItemId under = tree.ItemAt(p);
			if (under != TreeView::kNoItem && !editor.IsObjectSelected(SceneObjectId(under)))
				editor.SelectObject(SceneObjectId(under));
			OpenMenu(p);
			return true;
		}

		bool ScenePanel::MouseDown(const Vector2& p, bool extend, float time) {
			if (menu.IsOpen()) {
				menu.MouseDown(p);
				return true;
			}
			if (!frame.Contains(p))
				return false;
			if (p.y < frame.Body().GetMinY()) {
				EndInput(true); // leaving a tab settles what was typed in it
				frame.Click(p);
				return true;
			}
			if (frame.ActiveTab() == kOutlinerTab)
				tree.MouseDown(p, extend, time);
			else if (frame.ActiveTab() == kPropertiesTab)
				properties.MouseDown(p);
			else if (skinCheck.culprit != kNoSceneObject)
				editor.SelectObject(skinCheck.culprit); // a click on the report shows the culprit
			return true;
		}

		void ScenePanel::MouseMove(const Vector2& p) {
			tree.MouseMove(p);
			properties.MouseMove(p);
		}

		void ScenePanel::MouseUp(const Vector2& p) {
			tree.MouseUp(p);
			properties.MouseUp(p);
		}

		bool ScenePanel::Wheel(const Vector2& p, float rows) {
			if (!frame.Contains(p))
				return false;
			if (frame.ActiveTab() == kOutlinerTab)
				tree.Wheel(p, rows);
			return true; // over the panel, the wheel never zooms the view
		}

		bool ScenePanel::IsCapturing() const {
			return tree.IsPressed() || properties.IsPressed() || menu.IsOpen();
		}

		bool ScenePanel::IsTyping() const { return tree.IsRenaming() || properties.IsEditing(); }

		bool ScenePanel::KeyEvent(const std::string& key) {
			if (menu.IsOpen())
				return menu.KeyEvent(key);
			if (tree.IsRenaming())
				return tree.KeyEvent(key);
			if (tree.IsPressed()) {
				if (key != "Escape")
					return false;
				tree.CancelGesture(); // the dragged objects stay where they were
				return true;
			}
			return properties.KeyEvent(key);
		}

		void ScenePanel::TextInputEvent(const std::string& text) {
			tree.TextInputEvent(text);
			properties.TextInputEvent(text);
		}

		AABB2 ScenePanel::TextInputRect() const {
			if (tree.IsRenaming())
				return tree.RenameRect();
			return properties.EditingRect();
		}

		void ScenePanel::EndInput(bool commit) {
			menu.Close();
			tree.EndRename(commit);
			tree.CancelGesture();
			properties.EndGesture(commit);
		}

		void ScenePanel::RenameActive() {
			const SceneObjectId active = editor.ActiveObject();
			if (active == kNoSceneObject || !editor.GetScene().Find(active))
				return;
			EndInput(true);
			frame.SetActiveTab(kOutlinerTab);
			tree.BeginRename(active);
		}
	} // namespace gui
} // namespace spades
