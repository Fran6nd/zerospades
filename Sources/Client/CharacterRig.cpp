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

#include "CharacterRig.h"

#include <utility>

#include "IModel.h"
#include <Core/Debug.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/VoxelModel2KV6.h>

namespace spades {
	namespace client {
		namespace {
			using J = CharacterJoint;

			// How far crouching moves each joint from where it stands, in game
			// units: the classic body's own numbers.
			const Vector3 kCrouchHips = MakeVector3(0.0F, 0.25F, 0.05F);
			const Vector3 kCrouchWaist = MakeVector3(0.0F, 0.0F, 0.5F);
			const Vector3 kCrouchNeck = MakeVector3(0.0F, 0.0F, -0.05F);
			const Vector3 kCrouchArms = MakeVector3(0.0F, 0.0F, -0.1F);

			// The classic first-person left arm is the right one reflected, and
			// shifted by one of its voxels (a twentieth of a block) so it lines
			// up the same way.
			Matrix4 ViewArmMirror() {
				return Matrix4::Translate(0.0F, -0.05F, 0.0F) * Matrix4::Scale(1.0F, -1.0F, 1.0F);
			}

			Matrix4 TurnX(float a) { return Matrix4::Rotate(MakeVector3(1.0F, 0.0F, 0.0F), a); }
			Matrix4 TurnY(float a) { return Matrix4::Rotate(MakeVector3(0.0F, 1.0F, 0.0F), a); }
		} // namespace

		CharacterRig::CharacterRig(IRenderer& renderer, CharacterSkin skinIn)
		    : skin(std::move(skinIn)) {
			for (const CharacterPart* part : skin.AllParts()) {
				const VoxelModel* voxels = part->model.GetPointerOrNull();
				if (voxels && models.find(voxels) == models.end())
					models[voxels] = renderer.CreateModel(*part->model);
			}
		}

		CharacterPose CharacterRig::BodyPose(const CharacterMotion& motion) const {
			const bool crouch = motion.crouch;
			auto at = [&](J joint, const Vector3& crouchShift) {
				return Matrix4::Translate(skin.RestOffset(joint) +
				                          (crouch ? crouchShift : MakeVector3(0.0F, 0.0F, 0.0F)));
			};
			const Vector3 still = MakeVector3(0.0F, 0.0F, 0.0F);

			CharacterPose pose;
			pose.Set(J::Player, motion.feet);
			// The legs swing opposite ways.
			pose.Set(J::HipLeft, motion.feet * at(J::HipLeft, kCrouchHips) *
			                       TurnX(-motion.legSwing.x) * TurnY(-motion.legSwing.y));
			pose.Set(J::HipRight, motion.feet * at(J::HipRight, kCrouchHips) *
			                        TurnX(motion.legSwing.x) * TurnY(motion.legSwing.y));
			pose.Set(J::Waist, motion.feet * at(J::Waist, kCrouchWaist));
			pose.Set(J::Neck, pose[J::Waist] * at(J::Neck, kCrouchNeck) * TurnX(motion.pitch));
			pose.Set(J::Arms, pose[J::Waist] * at(J::Arms, kCrouchArms) * TurnX(motion.armPitch));
			pose.Set(J::ArmLeft, pose[J::Arms] * at(J::ArmLeft, still));
			pose.Set(J::ArmRight, pose[J::Arms] * at(J::ArmRight, still));
			return pose;
		}

		void CharacterRig::Render(IRenderer& renderer, const CharacterPose& pose,
		                          CharacterStance stance, bool christmas,
		                          ModelRenderParam param) const {
			const CharacterSlot slot = stance == CharacterStance::Crouch    ? CharacterSlot::Crouch
			                           : stance == CharacterStance::Ragdoll ? CharacterSlot::Ragdoll
			                                                                : CharacterSlot::Stand;
			for (std::size_t j = 0; j < kCharacterJointCount; j++) {
				if (!pose.posed[j])
					continue;
				const J joint = J(j);
				// The Christmas head is worn in every stance, when the skin has one.
				const CharacterSlot shown =
				  (christmas && joint == J::Neck && skin.HasSlot(joint, CharacterSlot::Christmas))
				    ? CharacterSlot::Christmas
				    : slot;
				RenderJoint(renderer, joint, shown, pose.frame[j], false, param);
			}
		}

		void CharacterRig::RenderJoint(IRenderer& renderer, CharacterJoint joint,
		                               CharacterSlot slot, const Matrix4& frame, bool mirror,
		                               ModelRenderParam param) const {
			const Matrix4 toGame =
			  (mirror ? frame * ViewArmMirror() : frame) * CharacterSkin::SkinToGame();
			for (const CharacterPart& part : skin.Parts(joint, slot)) {
				auto found = models.find(part.model.GetPointerOrNull());
				if (found == models.end() || !found->second)
					continue;
				// The render model has the voxels' pivot baked in; the part's
				// transform starts from their own grid, so the pivot is taken
				// back out.
				param.matrix =
				  toGame * part.local * Matrix4::Translate(part.model->GetOrigin() * -1.0F);
				renderer.RenderModel(*found->second, param);
			}
		}

		CharacterSkinLibrary::CharacterSkinLibrary(IRenderer& renderer) : renderer(renderer) {}

		CharacterSkinLibrary::~CharacterSkinLibrary() {}

		const CharacterRig* CharacterSkinLibrary::Find(const std::string& folder) {
			if (const CharacterRig* rig = Load(folder + "Skin.2kv6"))
				return rig;
			return Load("Models/Player/Skin.2kv6");
		}

		const CharacterRig* CharacterSkinLibrary::Load(const std::string& path) {
			auto cached = byPath.find(path);
			if (cached != byPath.end())
				return cached->second.get();

			std::unique_ptr<CharacterRig>& rig = byPath[path];
			if (!FileManager::FileExists(path.c_str()))
				return nullptr;
			try {
				std::unique_ptr<IStream> stream = FileManager::OpenForReading(path.c_str());
				const std::vector<VoxelObject> scene = VoxelModel2KV6::Load(*stream);
				rig.reset(new CharacterRig(renderer, CharacterSkin::FromScene(scene)));
				SPLog("Character skin loaded: %s", path.c_str());
			} catch (const std::exception& ex) {
				// Rejected once and remembered: the classic parts stand in.
				SPLog("Character skin rejected: %s: %s", path.c_str(), ex.what());
				rig.reset();
			}
			return rig.get();
		}
	} // namespace client
} // namespace spades
