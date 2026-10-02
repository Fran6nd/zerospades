/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <cstdint>
#include <vector>

#include "GLDynamicLight.h"
#include "GLProgramUniform.h"
#include <Client/IRenderer.h>
#include <Core/Math.h>

namespace spades {
	namespace draw {
		class GLRenderer;
		class GLImage;
		class GLProgramManager;

		/**
		 * Lights draws with several dynamic lights at once.
		 *
		 * The lights that reach a draw are split into batches of up to
		 * `MaxLightsPerDraw` that share one spotlight image, and each batch takes a
		 * single draw. A draw that cannot be repeated takes the nearest batch alone. A
		 * program keeps its uniforms, so a batch it already holds this frame is not
		 * uploaded again.
		 */
		class GLDynamicLightShader {
		public:
			/** The most lights one draw takes: `DYNAMIC_LIGHT_MAX` in the shaders. */
			static constexpr std::size_t MaxLightsPerDraw = 8;

		private:
			/** The uniforms of one light of a batch: `name[index]` in the shaders. */
			struct LightUniforms {
				GLProgramUniform origin;
				GLProgramUniform color;
				GLProgramUniform radius;
				GLProgramUniform radiusInversed;
				GLProgramUniform spotMatrix;
				GLProgramUniform isSpot;
				GLProgramUniform isLinear;
				GLProgramUniform linearDirection;
				GLProgramUniform linearLength;

				explicit LightUniforms(std::size_t index);
			};

			GLRenderer* lastRenderer = nullptr;
			Handle<GLImage> whiteImage;

			GLProgramUniform count;
			GLProgramUniform projectionTexture;
			std::vector<LightUniforms> lightUniforms;

			// Batching scratch, kept from draw to draw.
			std::vector<const GLDynamicLight*> pending;
			std::vector<const GLDynamicLight*> deferred;
			std::vector<const GLDynamicLight*> batch;

			// The batch the last program set up holds.
			GLProgram* uploadedProgram = nullptr;
			std::uint32_t uploadedFrame = 0;
			std::vector<const GLDynamicLight*> uploadedBatch;

			/** The image a spotlight projects, or none for another kind of light. */
			static GLImage* GetSpotImage(const GLDynamicLight&);

			/**
			 * Fills `batch` with the first lights of `pending` that fit one draw, in
			 * their order, and `deferred` with the rest; returns the batch's image.
			 */
			GLImage* TakeBatch();

			/** Binds `image` and sets the program up for `batch`. */
			void SetUp(GLRenderer*, GLProgram*, GLImage* image, int texStage);

			/** Sets the program up for the batch of `pending` nearest to `eye`. */
			void SetUpNearestBatch(GLRenderer*, GLProgram*, int texStage, const Vector3& eye);

		public:
			GLDynamicLightShader();
			~GLDynamicLightShader();

			static std::vector<GLShader*> RegisterShader(GLProgramManager*);

			/**
			 * Lights a draw with every light of `lights` that `reaches` accepts: calls
			 * `draw` once per batch, with `program` set up for it and the batch's image on
			 * `texStage`, which is left the active texture stage.
			 */
			template <class Reaches, class Draw>
			void Render(GLRenderer* renderer, GLProgram* program,
			            const std::vector<GLDynamicLight>& lights, int texStage,
			            Reaches&& reaches, Draw&& draw) {
				pending.clear();
				for (const GLDynamicLight& light : lights)
					if (reaches(light))
						pending.push_back(&light);

				while (!pending.empty()) {
					GLImage* image = TakeBatch();
					SetUp(renderer, program, image, texStage);
					draw();
					pending.swap(deferred);
				}
			}

			/**
			 * Lights a draw that must take its lights in one go, as it replaces what is
			 * under it instead of adding to it: sets `program` up with the one batch of
			 * the lights `reaches` accepts whose origins are nearest to `eye`, with its
			 * image on `texStage`, which is left the active texture stage. The lights
			 * that batch has no room for are left out. With no light, the program is set
			 * up to take none.
			 */
			template <class Reaches>
			void SetUpSingleDraw(GLRenderer* renderer, GLProgram* program,
			                     const std::vector<GLDynamicLight>& lights, int texStage,
			                     const Vector3& eye, Reaches&& reaches) {
				pending.clear();
				for (const GLDynamicLight& light : lights)
					if (reaches(light))
						pending.push_back(&light);

				SetUpNearestBatch(renderer, program, texStage, eye);
			}
		};
	} // namespace draw
} // namespace spades
