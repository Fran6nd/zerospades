/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades.

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

#include "CharacterSkin.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "Debug.h"
#include "Exception.h"

namespace spades {
	namespace {
		constexpr unsigned SlotBit(CharacterSlot slot) { return 1u << unsigned(slot); }

		struct JointRule {
			CharacterJoint joint;
			const char* name;
			CharacterJoint parent;
			// Needed whenever its parent is there. Only View is not, and the
			// first-person arms come with it.
			bool required;
			unsigned slots; // SlotBit of each slot it may hold
		};

		constexpr unsigned kBodySlots = SlotBit(CharacterSlot::Stand) |
		                                SlotBit(CharacterSlot::Crouch) |
		                                SlotBit(CharacterSlot::Ragdoll);

		const JointRule kRules[] = {
		  {CharacterJoint::Player, "Player", CharacterJoint::Count, true,
		   SlotBit(CharacterSlot::Dead)},
		  {CharacterJoint::HipLeft, "HipLeft", CharacterJoint::Player, true, kBodySlots},
		  {CharacterJoint::HipRight, "HipRight", CharacterJoint::Player, true, kBodySlots},
		  {CharacterJoint::Waist, "Waist", CharacterJoint::Player, true, kBodySlots},
		  {CharacterJoint::Neck, "Neck", CharacterJoint::Waist, true,
		   SlotBit(CharacterSlot::Stand) | SlotBit(CharacterSlot::Ragdoll) |
		     SlotBit(CharacterSlot::Christmas)},
		  {CharacterJoint::Arms, "Arms", CharacterJoint::Waist, true,
		   SlotBit(CharacterSlot::Stand) | SlotBit(CharacterSlot::Crouch)},
		  {CharacterJoint::ArmLeft, "ArmLeft", CharacterJoint::Arms, true, kBodySlots},
		  {CharacterJoint::ArmRight, "ArmRight", CharacterJoint::Arms, true, kBodySlots},
		  {CharacterJoint::View, "View", CharacterJoint::Player, false, 0},
		  {CharacterJoint::ViewUpperArm, "UpperArm", CharacterJoint::View, true,
		   SlotBit(CharacterSlot::Stand)},
		  {CharacterJoint::ViewForearm, "Forearm", CharacterJoint::View, true,
		   SlotBit(CharacterSlot::Stand)},
		};
		static_assert(sizeof(kRules) / sizeof(kRules[0]) == kCharacterJointCount,
		              "one rule per joint, in CharacterJoint order");

		const char* const kSlotNames[] = {"Stand", "Crouch", "Ragdoll", "Christmas", "Dead"};
		static_assert(sizeof(kSlotNames) / sizeof(kSlotNames[0]) == kCharacterSlotCount,
		              "one name per slot, in CharacterSlot order");

		const JointRule& RuleOf(CharacterJoint joint) { return kRules[std::size_t(joint)]; }

		const JointRule* JointNamed(const std::string& name) {
			for (const JointRule& rule : kRules) {
				if (name == rule.name)
					return &rule;
			}
			return nullptr;
		}

		int SlotNamed(const std::string& name) {
			for (std::size_t i = 0; i < kCharacterSlotCount; i++) {
				if (name == kSlotNames[i])
					return int(i);
			}
			return -1;
		}

		// A joint may only be moved: the game turns it about its own axes.
		constexpr float kTolerance = 1.0e-4F;
		bool IsUnturned(const Vector4& q) {
			// q and -q are the same turn.
			return std::fabs(q.x) < kTolerance && std::fabs(q.y) < kTolerance &&
			       std::fabs(q.z) < kTolerance && std::fabs(std::fabs(q.w) - 1.0F) < kTolerance;
		}
		bool IsUnscaled(const Vector3& s) {
			return std::fabs(s.x - 1.0F) < kTolerance && std::fabs(s.y - 1.0F) < kTolerance &&
			       std::fabs(s.z - 1.0F) < kTolerance;
		}

		Matrix4 LocalOf(const VoxelObject& object) {
			return Matrix4::Translate(object.position) *
			       Quaternion(object.rotation).ToRotationMatrix() * Matrix4::Scale(object.scale);
		}

		std::string Allowed(CharacterJoint joint) {
			std::string list;
			for (const JointRule& rule : kRules) {
				if (rule.parent == joint)
					list += (list.empty() ? "" : ", ") + std::string(rule.name);
			}
			for (std::size_t i = 0; i < kCharacterSlotCount; i++) {
				if (RuleOf(joint).slots & (1u << i))
					list += (list.empty() ? "" : ", ") + std::string(kSlotNames[i]);
			}
			return list.empty() ? std::string("nothing") : list;
		}
	} // namespace

	namespace {
		std::string Printf(const char* format, ...) {
			char buffer[512];
			va_list args;
			va_start(args, format);
			std::vsnprintf(buffer, sizeof(buffer), format, args);
			va_end(args);
			return buffer;
		}
	} // namespace

	// Walks the file once, checking each object against the structure as it
	// goes, so the first thing wrong is the one reported, with where it is.
	class CharacterSkin::Reader {
	public:
		explicit Reader(CharacterSkin& skin) : skin(skin) {}

		/** Names from the top of the scene to the object being read. */
		std::vector<std::string> path;

		template <class... Args> [[noreturn]] void Fail(const char* format, Args... args) {
			throw CharacterSkinError(path, Printf(format, args...));
		}

		void ReadJoint(const VoxelObject& object, CharacterJoint joint) {
			const char* name = RuleOf(joint).name;
			if (object.model)
				Fail("Joint '%s' holds voxels; joints are empties, put the voxels in one of its "
				     "slots (%s)",
				     name, Allowed(joint).c_str());
			if (!object.locked)
				Fail("Joint '%s' is unlocked; a skin's joints stay locked, as the template made "
				     "them",
				     name);
			if (!IsUnturned(object.rotation) || !IsUnscaled(object.scale))
				Fail("Joint '%s' is turned or scaled; joints may only be moved", name);
			skin.present[std::size_t(joint)] = true;
			// Moved in skin space; the game reads it in its own.
			const Vector4 offset =
			  SkinToGame() * MakeVector4(object.position.x, object.position.y, object.position.z, 0.0F);
			skin.rest[std::size_t(joint)] = offset.GetXYZ();

			std::set<std::string> seen;
			for (const VoxelObject& child : object.children) {
				path.push_back(child.name);
				if (!seen.insert(child.name).second)
					Fail("'%s' holds two objects called '%s'", name, child.name.c_str());
				const JointRule* rule = JointNamed(child.name);
				const int slot = SlotNamed(child.name);
				if (rule && rule->parent == joint)
					ReadJoint(child, rule->joint);
				else if (slot >= 0 && (RuleOf(joint).slots & (1u << unsigned(slot))))
					ReadSlot(child, joint, CharacterSlot(slot));
				else
					Fail("'%s' cannot hang under '%s'; only %s can", child.name.c_str(), name,
					     Allowed(joint).c_str());
				path.pop_back();
			}
			for (const JointRule& rule : kRules) {
				if (rule.parent == joint && rule.required && !skin.present[std::size_t(rule.joint)])
					Fail("'%s' is missing its joint '%s'", name, rule.name);
			}
		}

		void ReadSlot(const VoxelObject& object, CharacterJoint joint, CharacterSlot slot) {
			if (object.model)
				Fail("Slot '%s' of '%s' holds voxels itself; slots are empties, put the voxels in "
				     "objects under it",
				     kSlotNames[std::size_t(slot)], RuleOf(joint).name);
			skin.slotPresent[std::size_t(joint)][std::size_t(slot)] = true;
			slotPaths[std::size_t(joint)][std::size_t(slot)] = path;
			for (const VoxelObject& child : object.children) {
				path.push_back(child.name);
				ReadContent(child, LocalOf(object), joint, slot);
				path.pop_back();
			}
		}

		void ReadContent(const VoxelObject& object, const Matrix4& above, CharacterJoint joint,
		                 CharacterSlot slot) {
			if (JointNamed(object.name) || SlotNamed(object.name) >= 0)
				Fail("'%s' is a joint or slot name and cannot be used inside a slot (under '%s')",
				     object.name.c_str(), RuleOf(joint).name);
			const Matrix4 local = above * LocalOf(object);
			if (object.model) {
				if (++partCount > kMaxParts)
					Fail("The skin holds more than %zu voxel objects", kMaxParts);
				voxelCount += CountSolids(*object.model);
				if (voxelCount > kMaxVoxels)
					Fail("The skin holds more than %zu voxels", kMaxVoxels);
				skin.parts[std::size_t(joint)][std::size_t(slot)].push_back({object.model, local});
			}
			for (const VoxelObject& child : object.children) {
				path.push_back(child.name);
				ReadContent(child, local, joint, slot);
				path.pop_back();
			}
		}

		// At rest the body stays within the box its tool is kept inside while
		// it plays: up to two blocks to either side, four above the feet and
		// two below. The first-person arms are drawn at the eye, apart from it.
		void CheckBounds() {
			const AABB3 box(-2.0F, -2.0F, -4.0F, 4.0F, 4.0F, 6.0F);
			for (std::size_t j = 0; j < kCharacterJointCount; j++) {
				const CharacterJoint joint = CharacterJoint(j);
				if (joint == CharacterJoint::View || ParentOf(joint) == CharacterJoint::View)
					continue;
				Vector3 at = MakeVector3(0.0F, 0.0F, 0.0F);
				for (CharacterJoint k = joint; k != CharacterJoint::Count; k = ParentOf(k))
					at += skin.rest[std::size_t(k)];
				const Matrix4 toGame = Matrix4::Translate(at) * SkinToGame();
				for (std::size_t s = 0; s < kCharacterSlotCount; s++) {
					for (const CharacterPart& part : skin.parts[j][s]) {
						const VoxelModel& m = *part.model;
						for (int corner = 0; corner < 8; corner++) {
							const Vector3 c = MakeVector3((corner & 1) ? float(m.GetWidth()) : 0.0F,
							                              (corner & 2) ? float(m.GetHeight()) : 0.0F,
							                              (corner & 4) ? float(m.GetDepth()) : 0.0F);
							const Vector3 p =
							  (toGame * part.local * MakeVector4(c.x, c.y, c.z, 1.0F)).GetXYZ();
							if (box.Contains(p))
								continue;
							path = slotPaths[j][s];
							Fail("Slot '%s' of '%s' reaches outside the space a player may take "
							     "up (%.1f, %.1f, %.1f blocks from the feet)",
							     kSlotNames[s], RuleOf(joint).name, p.x, p.y, p.z);
						}
					}
				}
			}
		}

	private:
		CharacterSkin& skin;
		std::size_t partCount = 0;
		std::size_t voxelCount = 0;
		std::array<std::array<std::vector<std::string>, kCharacterSlotCount>, kCharacterJointCount>
		  slotPaths;

		static std::size_t CountSolids(const VoxelModel& model) {
			std::size_t count = 0;
			for (int x = 0; x < model.GetWidth(); x++)
				for (int y = 0; y < model.GetHeight(); y++)
					for (int z = 0; z < model.GetDepth(); z++)
						count += model.IsSolid(x, y, z) ? 1 : 0;
			return count;
		}
	};

	CharacterSkin CharacterSkin::FromScene(const std::vector<VoxelObject>& scene) {
		SPADES_MARK_FUNCTION();

		CharacterSkin skin;
		Reader reader(skin);

		// Player at the top, or under the one root a scene made in the editor
		// keeps everything in.
		const VoxelObject* player = nullptr;
		if (scene.size() == 1 && scene[0].name == "Player") {
			player = &scene[0];
		} else if (scene.size() == 1 && !scene[0].model && scene[0].children.size() == 1 &&
		           scene[0].children[0].name == "Player") {
			const VoxelObject& root = scene[0];
			reader.path.push_back(root.name);
			if (root.position.GetLength() > kTolerance || !IsUnturned(root.rotation) ||
			    !IsUnscaled(root.scale))
				reader.Fail("The scene root holding 'Player' is moved, turned or scaled");
			player = &root.children[0];
		} else {
			reader.Fail("A skin holds a single object called 'Player' at its top");
		}
		reader.path.push_back(player->name);
		if (player->position.GetLength() > kTolerance)
			reader.Fail("'Player' is moved; it stands for the player's feet and stays at the "
			            "origin");

		reader.ReadJoint(*player, CharacterJoint::Player);
		skin.rest[std::size_t(CharacterJoint::Player)] = MakeVector3(0.0F, 0.0F, 0.0F);
		reader.CheckBounds();
		return skin;
	}

	std::vector<VoxelObject> CharacterSkin::BuildTemplate(
	  const std::function<Handle<VoxelModel>(const std::string& file)>& part) {
		auto joint = [](CharacterJoint which, const Vector3& at) {
			VoxelObject object;
			object.name = RuleOf(which).name;
			object.position = at;
			object.locked = true;
			return object;
		};
		// A slot holding the classic part `file`, drawn exactly where the
		// classic code draws it: at its own pivot, scaled by `scale`, and turned
		// half round about z when `halfTurn` (the first-person arms).
		auto slot = [&](CharacterSlot which, const char* file, const char* name,
		                const Vector3& scale = MakeVector3(1.0F, 1.0F, 1.0F),
		                bool halfTurn = false) {
			VoxelObject holder;
			Handle<VoxelModel> voxels = part(file);
			if (!voxels)
				return holder; // named nothing: left out below
			holder.name = kSlotNames[std::size_t(which)];
			VoxelObject content;
			content.name = name;
			content.model = voxels;
			const Vector3 o = voxels->GetOrigin();
			Vector3 at = MakeVector3(o.x * scale.x, o.y * scale.y, o.z * scale.z);
			if (halfTurn) {
				at = MakeVector3(-at.x, -at.y, at.z);
				content.rotation = MakeVector4(0.0F, 0.0F, 1.0F, 0.0F);
			}
			content.position = at;
			content.scale = scale;
			holder.children.push_back(std::move(content));
			return holder;
		};
		auto add = [](VoxelObject& to, VoxelObject child) {
			if (!child.name.empty())
				to.children.push_back(std::move(child));
		};
		// Where the classic body turns each part, from the classic drawing
		// code, in skin space (tenths of a block, x and y turned round).
		auto at = [](float x, float y, float z) { return MakeVector3(-x * 10.0F, -y * 10.0F, z * 10.0F); };
		const Vector3 half = MakeVector3(0.5F, 0.5F, 0.5F);

		VoxelObject player = joint(CharacterJoint::Player, at(0.0F, 0.0F, 0.0F));

		VoxelObject hipLeft = joint(CharacterJoint::HipLeft, at(-0.25F, 0.0F, -0.1F));
		add(hipLeft, slot(CharacterSlot::Stand, "Leg.kv6", "Leg"));
		add(hipLeft, slot(CharacterSlot::Crouch, "LegCrouch.kv6", "Leg"));
		VoxelObject hipRight = joint(CharacterJoint::HipRight, at(0.25F, 0.0F, -0.1F));
		add(hipRight, slot(CharacterSlot::Stand, "LegRight.kv6", "Leg"));
		add(hipRight, slot(CharacterSlot::Crouch, "LegRightCrouch.kv6", "Leg"));

		VoxelObject waist = joint(CharacterJoint::Waist, at(0.0F, 0.0F, -1.0F));
		add(waist, slot(CharacterSlot::Stand, "Torso.kv6", "Torso"));
		add(waist, slot(CharacterSlot::Crouch, "TorsoCrouch.kv6", "Torso"));
		VoxelObject neck = joint(CharacterJoint::Neck, at(0.0F, 0.0F, 0.0F));
		add(neck, slot(CharacterSlot::Stand, "Head.kv6", "Head"));
		add(neck, slot(CharacterSlot::Christmas, "XmasHat.kv6", "Hat"));
		VoxelObject arms = joint(CharacterJoint::Arms, at(0.0F, 0.0F, 0.1F));
		add(arms, slot(CharacterSlot::Stand, "Arms.kv6", "BothArms"));
		// Apart only in the ragdoll, which draws the single arm at three
		// quarters, the left one mirrored.
		VoxelObject armLeft = joint(CharacterJoint::ArmLeft, at(-0.4F, 0.0F, 0.0F));
		add(armLeft, slot(CharacterSlot::Ragdoll, "Arm.kv6", "Arm", MakeVector3(-0.75F, 0.75F, 1.0F)));
		VoxelObject armRight = joint(CharacterJoint::ArmRight, at(0.4F, 0.0F, 0.0F));
		add(armRight, slot(CharacterSlot::Ragdoll, "Arm.kv6", "Arm", MakeVector3(0.75F, 0.75F, 1.0F)));
		arms.children.push_back(std::move(armLeft));
		arms.children.push_back(std::move(armRight));
		waist.children.push_back(std::move(neck));
		waist.children.push_back(std::move(arms));

		// The first-person arms are half the size of the body's.
		VoxelObject view = joint(CharacterJoint::View, at(0.0F, 0.0F, 0.0F));
		VoxelObject upper = joint(CharacterJoint::ViewUpperArm, at(0.0F, 0.0F, 0.0F));
		add(upper, slot(CharacterSlot::Stand, "UpperArm.kv6", "Upper", half, true));
		VoxelObject fore = joint(CharacterJoint::ViewForearm, at(0.0F, 0.0F, 0.0F));
		add(fore, slot(CharacterSlot::Stand, "Arm.kv6", "Lower", half, true));
		view.children.push_back(std::move(upper));
		view.children.push_back(std::move(fore));

		player.children.push_back(std::move(hipLeft));
		player.children.push_back(std::move(hipRight));
		player.children.push_back(std::move(waist));
		player.children.push_back(std::move(view));
		add(player, slot(CharacterSlot::Dead, "Dead.kv6", "Body"));
		return {std::move(player)};
	}

	const char* CharacterSkin::JointName(CharacterJoint joint) { return RuleOf(joint).name; }

	const char* CharacterSkin::SlotName(CharacterSlot slot) {
		return kSlotNames[std::size_t(slot)];
	}

	CharacterJoint CharacterSkin::ParentOf(CharacterJoint joint) { return RuleOf(joint).parent; }

	Matrix4 CharacterSkin::SkinToGame() {
		return Matrix4::Scale(0.1F) * Matrix4::Scale(-1.0F, -1.0F, 1.0F);
	}

	Vector3 CharacterSkin::RestOffset(CharacterJoint joint) const {
		return rest[std::size_t(joint)];
	}

	bool CharacterSkin::HasSlot(CharacterJoint joint, CharacterSlot slot) const {
		return slotPresent[std::size_t(joint)][std::size_t(slot)];
	}

	const std::vector<CharacterPart>& CharacterSkin::Parts(CharacterJoint joint,
	                                                       CharacterSlot slot) const {
		const std::size_t j = std::size_t(joint);
		return slotPresent[j][std::size_t(slot)] ? parts[j][std::size_t(slot)]
		                                         : parts[j][std::size_t(CharacterSlot::Stand)];
	}

	std::vector<const CharacterPart*> CharacterSkin::AllParts() const {
		std::vector<const CharacterPart*> all;
		for (const auto& joint : parts)
			for (const auto& slot : joint)
				for (const CharacterPart& part : slot)
					all.push_back(&part);
		return all;
	}
} // namespace spades
