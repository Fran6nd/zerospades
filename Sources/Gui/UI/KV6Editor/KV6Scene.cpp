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

#include "KV6Scene.h"

#include <algorithm>

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
			    scale != o.scale || hasModel != o.hasModel ||
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

		Matrix4 LocalTransform(const SceneNode& node) {
			return Matrix4::Translate(node.position) *
			       Quaternion(node.rotation).ToRotationMatrix() * Matrix4::Scale(node.scale);
		}
	} // namespace gui
} // namespace spades
