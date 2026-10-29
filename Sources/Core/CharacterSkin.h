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

#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Math.h"
#include "RefCountedObject.h"
#include "VoxelModel.h"
#include "VoxelModel2KV6.h"

namespace spades {
	/**
	 * The articulations of a character skin: the empties the game moves.
	 *
	 * A skin is one .2kv6 with a fixed tree (see CharacterSkin). Each joint is
	 * an empty at the point a body part turns about; the voxels hang under it
	 * in slots, and the game animates only the joints.
	 */
	enum class CharacterJoint {
		Player,       // the feet, facing forward: placed and turned with the player
		HipLeft,      // turns with the walk
		HipRight,
		Waist,        // drops when crouching
		Neck,         // looks up and down
		Arms,         // follows the aim; the tool is held here
		ArmLeft,      // carried by Arms, apart only in the ragdoll
		ArmRight,
		View,         // groups the first-person arms; never moved itself
		ViewUpperArm, // first-person arms, placed from where the tool wants the hands
		ViewForearm,
		Count
	};
	constexpr std::size_t kCharacterJointCount = std::size_t(CharacterJoint::Count);

	/**
	 * Which voxels of a joint are shown: the ones for how the body stands.
	 * A joint without the slot asked for falls back to Stand (see Parts).
	 */
	enum class CharacterSlot {
		Stand,
		Crouch,
		Ragdoll,
		Christmas, // Neck only: worn during the Christmas event
		Dead,      // Player only: the whole body lying down, when there is no ragdoll
		Count
	};
	constexpr std::size_t kCharacterSlotCount = std::size_t(CharacterSlot::Count);

	/** One voxel object of a skin, and where it sits relative to its joint. */
	struct CharacterPart {
		Handle<VoxelModel> model;
		// Skin space (voxel units, the axes a .kv6 is drawn in), from the
		// object's own voxel grid to the joint: its slot's transform and any
		// objects between.
		Matrix4 local;
	};

	/**
	 * Why a scene is no character skin, and which object of it is at fault:
	 * its names from the top of the scene down (empty when it is the scene
	 * as a whole), so an editor can point at it.
	 */
	class CharacterSkinError : public std::runtime_error {
	public:
		CharacterSkinError(std::vector<std::string> path, const std::string& message)
		    : std::runtime_error(message), path(std::move(path)) {}
		std::vector<std::string> path;
	};

	/**
	 * A character skin read from a .2kv6, checked against the one structure
	 * the game knows how to animate:
	 *
	 *     Player                 [Dead]
	 *     ├─ HipLeft             [Stand, Crouch, Ragdoll]
	 *     ├─ HipRight            [Stand, Crouch, Ragdoll]
	 *     ├─ Waist               [Stand, Crouch, Ragdoll]
	 *     │  ├─ Neck             [Stand, Ragdoll, Christmas]
	 *     │  └─ Arms             [Stand, Crouch]
	 *     │     ├─ ArmLeft       [Stand, Crouch, Ragdoll]
	 *     │     └─ ArmRight      [Stand, Crouch, Ragdoll]
	 *     └─ View (optional)
	 *        ├─ UpperArm         [Stand]
	 *        └─ Forearm          [Stand]
	 *
	 * Joints are locked empties that may only be moved: no voxels, no turn,
	 * no scale, since the game turns them about their own axes. Being locked
	 * is what says the joint still belongs to the template; unlocking one in
	 * an editor gives that up, and the game then refuses the skin. Slots are empties
	 * named as above, each optional; anything under a slot is the skin's own
	 * (voxels and empties, turned and scaled freely) as long as it takes no
	 * joint's or slot's name. Nothing else may hang from a joint. A file made
	 * in the editor, whose scene root holds Player, is read the same.
	 *
	 * Hidden objects, and what is under them, are part of the structure but
	 * are not drawn: a hidden slot shows nothing. Keyframes in the file are
	 * ignored: the game drives the joints itself.
	 */
	class CharacterSkin {
	public:
		/**
		 * Reads a skin out of a loaded .2kv6. Throws CharacterSkinError,
		 * saying what is wrong and where, when the scene breaks the structure
		 * or the limits below.
		 */
		static CharacterSkin FromScene(const std::vector<VoxelObject>& scene);

		/**
		 * The skin template: every joint, locked and placed where the classic
		 * body has its parts turn, each slot filled from the classic part
		 * `part` gives for a file name ("Torso.kv6", "LegCrouch.kv6"...). A
		 * part it has none of (a null handle) leaves its slot out. The same
		 * layout the classic parts have always been drawn in, so a skin built
		 * from a set of them looks exactly like it.
		 */
		static std::vector<VoxelObject>
		BuildTemplate(const std::function<Handle<VoxelModel>(const std::string& file)>& part);

		static const char* JointName(CharacterJoint joint);
		static const char* SlotName(CharacterSlot slot);
		/** The joint `joint` hangs from; Count for Player. */
		static CharacterJoint ParentOf(CharacterJoint joint);

		/**
		 * From skin space to the game's: a tenth of a block per voxel, with
		 * x and y turned round, as every player model has always been drawn.
		 */
		static Matrix4 SkinToGame();

		bool Has(CharacterJoint joint) const { return present[std::size_t(joint)]; }
		/** Where `joint` sits at rest relative to its parent joint, in game units. */
		Vector3 RestOffset(CharacterJoint joint) const;
		/** Whether `joint` holds a slot of that name (possibly empty of voxels). */
		bool HasSlot(CharacterJoint joint, CharacterSlot slot) const;
		/**
		 * The voxels `joint` shows in `slot`: those of the slot itself when
		 * the skin has it, else those of Stand.
		 */
		const std::vector<CharacterPart>& Parts(CharacterJoint joint, CharacterSlot slot) const;
		/** Every voxel object, in the order met; for building what renders them. */
		std::vector<const CharacterPart*> AllParts() const;

		// Limits a skin is held to, so one cannot hide inside the map or
		// stand head and shoulders over it: within the box around the player
		// that its tool is already kept inside, at rest.
		static constexpr std::size_t kMaxParts = 64;
		static constexpr std::size_t kMaxVoxels = 200000;

	private:
		CharacterSkin() = default;

		std::array<bool, kCharacterJointCount> present{};
		std::array<Vector3, kCharacterJointCount> rest{};
		std::array<std::array<bool, kCharacterSlotCount>, kCharacterJointCount> slotPresent{};
		std::array<std::array<std::vector<CharacterPart>, kCharacterSlotCount>, kCharacterJointCount>
		  parts;

		class Reader;
	};
} // namespace spades
