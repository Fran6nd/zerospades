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
#include <map>
#include <memory>
#include <string>

#include <Core/CharacterSkin.h>
#include <Core/Math.h>
#include <Core/RefCountedObject.h>

#include "IRenderer.h"

namespace spades {
	namespace client {
		class IModel;

		/** How a body stands, which picks the slot each joint shows. */
		enum class CharacterStance { Stand, Crouch, Ragdoll };

		/**
		 * Where each joint of a character is this frame, in game units: the
		 * frame its voxels are drawn in. A joint left unposed is not drawn.
		 */
		struct CharacterPose {
			std::array<Matrix4, kCharacterJointCount> frame;
			std::array<bool, kCharacterJointCount> posed{};

			void Set(CharacterJoint joint, const Matrix4& m) {
				frame[std::size_t(joint)] = m;
				posed[std::size_t(joint)] = true;
			}
			void Clear(CharacterJoint joint) { posed[std::size_t(joint)] = false; }
			const Matrix4& operator[](CharacterJoint joint) const {
				return frame[std::size_t(joint)];
			}
		};

		/**
		 * What the game's animation moves a living body by. The motion is
		 * worked out by the player code, as it always was; a skin only says
		 * where the joints sit at rest.
		 */
		struct CharacterMotion {
			Matrix4 feet = Matrix4::Identity(); // at the player's feet, turned to face forward
			bool crouch = false;
			Vector2 legSwing = MakeVector2(0.0F, 0.0F); // forward and sideways swing of the legs
			float pitch = 0.0F;                         // of the head
			float armPitch = 0.0F;                      // of the arms and what they hold
		};

		/**
		 * A character skin ready to draw: the skin, and a render model for each
		 * of its voxel objects, made once.
		 */
		class CharacterRig {
		public:
			CharacterRig(IRenderer& renderer, CharacterSkin skin);

			const CharacterSkin& Skin() const { return skin; }

			/**
			 * The living body's pose: legs, waist, neck, arms. The joints sit
			 * where the skin puts them, and move as the classic body parts
			 * always have.
			 */
			CharacterPose BodyPose(const CharacterMotion& motion) const;

			/** Draws every posed joint in the slot `stance` picks for it. */
			void Render(IRenderer& renderer, const CharacterPose& pose, CharacterStance stance,
			            bool christmas, ModelRenderParam param) const;
			/**
			 * Draws what `joint` holds in `slot` in the frame `frame`. `mirror`
			 * reflects it as the classic first-person left arm is.
			 */
			void RenderJoint(IRenderer& renderer, CharacterJoint joint, CharacterSlot slot,
			                 const Matrix4& frame, bool mirror, ModelRenderParam param) const;

		private:
			CharacterSkin skin;
			std::map<const VoxelModel*, Handle<IModel>> models;
		};

		/**
		 * The skins players are drawn with, loaded once per file.
		 *
		 * A skin is a .2kv6 called `Skin.2kv6` next to the classic body parts
		 * of a player (`Models/Player/`, or its weapon's folder), else the one
		 * in `Models/Player/`, which then dresses every weapon. When there is
		 * one, it is all that is drawn of that player; when there is none, or
		 * it breaks the rules (said once in the log), the classic parts are
		 * drawn exactly as before.
		 */
		class CharacterSkinLibrary {
		public:
			explicit CharacterSkinLibrary(IRenderer& renderer);
			~CharacterSkinLibrary();

			/** The skin for the models under `folder`, or null for the classic parts. */
			const CharacterRig* Find(const std::string& folder);

		private:
			IRenderer& renderer;
			// Keyed by file; null when there is no such file or it was rejected,
			// so neither is looked for again.
			std::map<std::string, std::unique_ptr<CharacterRig>> byPath;

			const CharacterRig* Load(const std::string& path);
		};
	} // namespace client
} // namespace spades
