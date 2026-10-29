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

#include "Scene.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace spades {
	namespace gui {
		namespace {
			bool SameKeyframe(const TransformKeyframe& a, const TransformKeyframe& b) {
				return a.time == b.time && a.position == b.position && a.rotation == b.rotation &&
				       a.scale == b.scale;
			}

			// Walks `nodes` and everything under them, parents first, until `visit`
			// returns false. `parent` is the node they hang from (null for roots).
			template <class Visit>
			bool Walk(std::vector<SceneNode>& nodes, SceneNode* parent, Visit visit) {
				for (SceneNode& node : nodes) {
					if (!visit(node, parent))
						return false;
					if (!Walk(node.children, &node, visit))
						return false;
				}
				return true;
			}
		} // namespace

		bool SceneNode::operator==(const SceneNode& o) const {
			if (id != o.id || name != o.name || position != o.position || rotation != o.rotation ||
			    scale != o.scale || hasModel != o.hasModel || locked != o.locked || hidden != o.hidden ||
			    keyframes.size() != o.keyframes.size() || children.size() != o.children.size())
				return false;
			for (std::size_t i = 0; i < keyframes.size(); i++) {
				if (!SameKeyframe(keyframes[i], o.keyframes[i]))
					return false;
			}
			return children == o.children;
		}

		std::size_t SceneNode::CountWithChildren() const {
			std::size_t count = 1;
			for (const SceneNode& child : children)
				count += child.CountWithChildren();
			return count;
		}

		bool Scene::operator==(const Scene& o) const {
			return nextId == o.nextId && roots == o.roots;
		}

		std::size_t Scene::Bytes() const {
			std::size_t bytes = 0;
			Walk(const_cast<Scene*>(this)->roots, nullptr, [&](SceneNode& node, SceneNode*) {
				bytes += sizeof(SceneNode) + node.name.capacity() +
				         node.keyframes.capacity() * sizeof(TransformKeyframe) +
				         node.children.capacity() * sizeof(SceneNode);
				return true;
			});
			return bytes;
		}

		SceneNode* Scene::Find(SceneObjectId id) {
			SceneNode* found = nullptr;
			if (id == kNoSceneObject)
				return nullptr;
			Walk(roots, nullptr, [&](SceneNode& node, SceneNode*) {
				if (node.id != id)
					return true;
				found = &node;
				return false;
			});
			return found;
		}

		const SceneNode* Scene::Find(SceneObjectId id) const {
			return const_cast<Scene*>(this)->Find(id);
		}

		SceneNode* Scene::ParentOf(SceneObjectId id) {
			SceneNode* found = nullptr;
			if (id == kNoSceneObject)
				return nullptr;
			Walk(roots, nullptr, [&](SceneNode& node, SceneNode* parent) {
				if (node.id != id)
					return true;
				found = parent;
				return false;
			});
			return found;
		}

		const SceneNode* Scene::ParentOf(SceneObjectId id) const {
			return const_cast<Scene*>(this)->ParentOf(id);
		}

		SceneNode* Scene::Root() {
			// The root is a lone top-level node holding no voxels: infrastructure,
			// not an object. A file may hold its objects at the top instead.
			if (roots.size() == 1 && !roots.front().hasModel && !roots.front().locked)
				return &roots.front();
			return nullptr;
		}

		const SceneNode* Scene::Root() const { return const_cast<Scene*>(this)->Root(); }

		std::vector<SceneNode>& Scene::ObjectHome() {
			SceneNode* root = Root();
			return root ? root->children : roots;
		}

		std::vector<SceneObjectId> Scene::ObjectIds() const {
			std::vector<SceneObjectId> ids;
			Walk(const_cast<Scene*>(this)->roots, nullptr, [&](SceneNode& node, SceneNode*) {
				if (node.hasModel)
					ids.push_back(node.id);
				return true;
			});
			return ids;
		}

		bool Scene::Remove(SceneObjectId id) {
			if (id == kNoSceneObject)
				return false;
			std::vector<SceneNode>* siblings = &roots;
			if (SceneNode* parent = ParentOf(id))
				siblings = &parent->children;
			auto it = std::find_if(siblings->begin(), siblings->end(),
			                       [&](const SceneNode& node) { return node.id == id; });
			if (it == siblings->end())
				return false;
			siblings->erase(it);
			return true;
		}

		bool Scene::IsUnder(SceneObjectId id, SceneObjectId ancestor) const {
			if (ancestor == kNoSceneObject)
				return false;
			for (const SceneNode* above = ParentOf(id); above; above = ParentOf(above->id)) {
				if (above->id == ancestor)
					return true;
			}
			return false;
		}

		std::vector<SceneObjectId> Scene::TopMost(const std::vector<SceneObjectId>& ids) const {
			auto listed = [&](SceneObjectId id) {
				return std::find(ids.begin(), ids.end(), id) != ids.end();
			};
			std::vector<SceneObjectId> top;
			for (SceneObjectId id : Ids()) {
				if (!listed(id))
					continue;
				bool carried = false;
				for (const SceneNode* above = ParentOf(id); above && !carried;
				     above = ParentOf(above->id))
					carried = listed(above->id);
				if (!carried)
					top.push_back(id);
			}
			return top;
		}

		bool Scene::CanMove(const std::vector<SceneObjectId>& ids, SceneObjectId parent) const {
			if (ids.empty())
				return false;
			if (parent != kNoSceneObject && !Find(parent))
				return false;
			const SceneNode* root = Root();
			for (SceneObjectId id : ids) {
				const SceneNode* node = Find(id);
				if (!node || node->locked || (root && root->id == id))
					return false;
				// Under itself, or under one of its own children: the branch
				// would hang from nothing.
				if (id == parent || IsUnder(parent, id))
					return false;
			}
			return true;
		}

		bool Scene::Move(const std::vector<SceneObjectId>& ids, SceneObjectId parent,
		                 SceneObjectId before) {
			if (!CanMove(ids, parent))
				return false;
			const std::vector<SceneObjectId> movers = TopMost(ids);
			auto moving = [&](SceneObjectId id) {
				return std::find(movers.begin(), movers.end(), id) != movers.end();
			};
			auto destination = [&]() -> std::vector<SceneNode>& {
				return parent != kNoSceneObject ? Find(parent)->children : ObjectHome();
			};

			// Where they land is named by the first sibling from `before` on
			// that stays put, so taking the movers out cannot shift it.
			SceneObjectId anchor = kNoSceneObject;
			{
				const std::vector<SceneNode>& siblings = destination();
				auto it = std::find_if(siblings.begin(), siblings.end(),
				                       [&](const SceneNode& node) { return node.id == before; });
				for (; it != siblings.end(); ++it) {
					if (!moving(it->id)) {
						anchor = it->id;
						break;
					}
				}
			}

			std::vector<SceneNode> taken;
			taken.reserve(movers.size());
			for (SceneObjectId id : movers) {
				std::vector<SceneNode>* siblings = &roots;
				if (SceneNode* above = ParentOf(id))
					siblings = &above->children;
				auto it = std::find_if(siblings->begin(), siblings->end(),
				                       [&](const SceneNode& node) { return node.id == id; });
				taken.push_back(std::move(*it));
				siblings->erase(it);
			}

			// Found again: the removals above may have moved it in memory.
			std::vector<SceneNode>& siblings = destination();
			auto at = std::find_if(siblings.begin(), siblings.end(),
			                       [&](const SceneNode& node) { return node.id == anchor; });
			siblings.insert(at, std::make_move_iterator(taken.begin()),
			                std::make_move_iterator(taken.end()));
			return true;
		}

		bool Scene::MoveKeepingWorld(const std::vector<SceneObjectId>& ids, SceneObjectId parent,
		                             SceneObjectId before) {
			if (!CanMove(ids, parent))
				return false;
			const bool hadRoot = Root() != nullptr;
			const std::vector<SceneObjectId> movers = TopMost(ids);
			// Where each sits in the world, and what it hangs from, before the move.
			std::vector<Matrix4> world;
			std::vector<SceneObjectId> wasUnder;
			for (SceneObjectId id : movers) {
				world.push_back(WorldTransform(id));
				const SceneNode* above = ParentOf(id);
				wasUnder.push_back(above ? above->id : kNoSceneObject);
			}
			if (!Move(ids, parent, before))
				return false;
			for (std::size_t i = 0; i < movers.size(); i++) {
				const SceneNode* above = ParentOf(movers[i]);
				if ((above ? above->id : kNoSceneObject) == wasUnder[i])
					continue; // only reordered: its transform already reads the same
				const ObjectTransform local = Decompose(ParentTransform(movers[i]).Inversed() * world[i]);
				SceneNode* node = Find(movers[i]);
				node->position = local.position;
				node->rotation = local.rotation;
				node->scale = local.scale;
			}
			KeepTopLevel(hadRoot);
			return true;
		}

		std::vector<SceneObjectId> Scene::WithDescendants(const std::vector<SceneObjectId>& ids) const {
			auto listed = [&](SceneObjectId id) {
				return std::find(ids.begin(), ids.end(), id) != ids.end();
			};
			std::vector<SceneObjectId> all;
			for (SceneObjectId id : Ids()) {
				bool in = listed(id);
				for (const SceneNode* above = ParentOf(id); above && !in; above = ParentOf(above->id))
					in = listed(above->id);
				if (in)
					all.push_back(id);
			}
			return all;
		}

		bool Scene::IsShown(SceneObjectId id) const {
			const SceneNode* node = Find(id);
			if (!node || node->hidden)
				return false;
			for (const SceneNode* above = ParentOf(id); above; above = ParentOf(above->id)) {
				if (above->hidden)
					return false;
			}
			return true;
		}

		bool Scene::AnyLocked(const std::vector<SceneObjectId>& ids) const {
			for (SceneObjectId id : WithDescendants(ids)) {
				const SceneNode* node = Find(id);
				if (node && node->locked)
					return true;
			}
			return false;
		}

		Matrix4 Scene::TopLevelTransform() const {
			const SceneNode* root = Root();
			return root ? WorldTransform(root->id) : Matrix4::Identity();
		}

		void Scene::KeepTopLevel(bool hadRoot) {
			if (hadRoot || !Root())
				return;
			SceneNode root;
			root.id = nextId++;
			root.name = "Scene";
			root.children = std::move(roots);
			roots.clear();
			roots.push_back(std::move(root));
		}

		std::vector<SceneObjectId> Scene::Ids() const {
			std::vector<SceneObjectId> ids;
			Walk(const_cast<Scene*>(this)->roots, nullptr, [&](SceneNode& node, SceneNode*) {
				ids.push_back(node.id);
				return true;
			});
			return ids;
		}

		SceneObjectId Scene::FirstModelObject() const {
			SceneObjectId found = kNoSceneObject;
			Walk(const_cast<Scene*>(this)->roots, nullptr, [&](SceneNode& node, SceneNode*) {
				if (!node.hasModel)
					return true;
				found = node.id;
				return false;
			});
			return found;
		}

		Matrix4 Scene::WorldTransform(SceneObjectId id) const {
			// Compose from the root down, so a child follows every parent above it.
			std::vector<SceneObjectId> chain;
			for (SceneObjectId at = id; at != kNoSceneObject;) {
				chain.push_back(at);
				const SceneNode* parent = const_cast<Scene*>(this)->ParentOf(at);
				at = parent ? parent->id : kNoSceneObject;
			}
			Matrix4 world = Matrix4::Identity();
			for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
				if (const SceneNode* node = Find(*it))
					world = world * LocalTransform(*node);
			}
			return world;
		}

		ObjectTransform TransformOf(const SceneNode& node) {
			ObjectTransform transform;
			transform.position = node.position;
			transform.rotation = node.rotation;
			transform.scale = node.scale;
			return transform;
		}

		Matrix4 DragTransform(const Vector3& translation, const Quaternion& rotation,
		                      const Vector3& scale, const Vector3& pivot, const Vector3 axes[3]) {
			// Scale along the gizmo's own axes: into that frame, stretch, and back.
			Matrix4 frame = Matrix4::Identity();
			for (int k = 0; k < 3; k++) {
				const Vector3 axis = axes[k];
				frame.m[k * 4 + 0] = axis.x;
				frame.m[k * 4 + 1] = axis.y;
				frame.m[k * 4 + 2] = axis.z;
			}
			const Matrix4 stretch = frame * Matrix4::Scale(scale) * frame.Transposed();
			const Matrix4 turn = rotation.Normalize().ToRotationMatrix();
			return Matrix4::Translate(translation) * Matrix4::Translate(pivot) * turn * stretch *
			       Matrix4::Translate(pivot * -1.0F);
		}

		ObjectTransform Decompose(const Matrix4& matrix) {
			ObjectTransform transform;
			transform.position = matrix.GetOrigin();
			Vector3 axes[3] = {matrix.GetAxis(0), matrix.GetAxis(1), matrix.GetAxis(2)};
			float lengths[3] = {axes[0].GetLength(), axes[1].GetLength(), axes[2].GetLength()};

			// A drag may turn the frame inside out (a scale handle pulled through
			// the middle). A turn cannot express that, so the flip is kept as a
			// negative factor on the first axis and taken out of the frame, which
			// leaves a proper rotation to read the turn from.
			const bool flipped =
			  Vector3::Dot(Vector3::Cross(axes[0], axes[1]), axes[2]) < 0.0F;
			if (flipped) {
				axes[0] = axes[0] * -1.0F;
				lengths[0] = -lengths[0];
			}
			transform.scale = MakeVector3(lengths[0], lengths[1], lengths[2]);

			// A flattened axis leaves no direction to read a turn from, so the
			// axis it had is kept.
			Matrix4 rotation = Matrix4::Identity();
			for (int k = 0; k < 3; k++) {
				const float length = std::fabs(lengths[k]);
				const Vector3 unit =
				  (length > 0.0F) ? axes[k] * (1.0F / length) : rotation.GetAxis(k);
				rotation.m[k * 4 + 0] = unit.x;
				rotation.m[k * 4 + 1] = unit.y;
				rotation.m[k * 4 + 2] = unit.z;
			}
			transform.rotation = Quaternion::FromRotationMatrix(rotation).Normalize().v;
			return transform;
		}

		Matrix4 Scene::ParentTransform(SceneObjectId id) const {
			const SceneNode* parent = const_cast<Scene*>(this)->ParentOf(id);
			return parent ? WorldTransform(parent->id) : Matrix4::Identity();
		}

		Matrix4 LocalTransform(const SceneNode& node) {
			return Matrix4::Translate(node.position) *
			       Quaternion(node.rotation).ToRotationMatrix() * Matrix4::Scale(node.scale);
		}

		Vector3 EulerDegreesOf(const Vector4& rotation) {
			// The turn is Rz * Ry * Rx; its matrix is column-major, so row r of
			// column c is m[c * 4 + r].
			const Matrix4 m = Quaternion(rotation).Normalize().ToRotationMatrix();
			const float r00 = m.m[0], r10 = m.m[1], r20 = m.m[2];
			const float r11 = m.m[5], r21 = m.m[6], r12 = m.m[9], r22 = m.m[10];
			const float y = std::asin(Clamp(-r20, -1.0F, 1.0F));
			float x, z;
			if (std::fabs(r20) < 1.0F - 1.0e-6F) {
				x = std::atan2(r21, r22);
				z = std::atan2(r10, r00);
			} else {
				// Straight up or down, X and Z turn about the same line: all of
				// it is given to X.
				x = std::atan2(-r12, r11);
				z = 0.0F;
			}
			// Float noise around zero would read as "-0.0".
			auto tidy = [](float radians) {
				const float degrees = RAD2DEG(radians);
				return std::fabs(degrees) < 1.0e-4F ? 0.0F : degrees;
			};
			return MakeVector3(tidy(x), tidy(y), tidy(z));
		}

		Vector4 RotationFromEulerDegrees(const Vector3& degrees) {
			const Matrix4 turn = Matrix4::Rotate(MakeVector3(0.0F, 0.0F, 1.0F), DEG2RAD(degrees.z)) *
			                     Matrix4::Rotate(MakeVector3(0.0F, 1.0F, 0.0F), DEG2RAD(degrees.y)) *
			                     Matrix4::Rotate(MakeVector3(1.0F, 0.0F, 0.0F), DEG2RAD(degrees.x));
			return Quaternion::FromRotationMatrix(turn).Normalize().v;
		}
	} // namespace gui
} // namespace spades
