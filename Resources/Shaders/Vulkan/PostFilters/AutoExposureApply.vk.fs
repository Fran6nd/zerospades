/*
 Copyright (c) 2015 Fran6nd

 This file is part of ZeroSpades, a fork of OpenSpades.

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

// Multiplies the scene colour by the exposure gain stored in the
// persistent 1×1 accumulator, then encodes the linear HDR colour for
// display with pow(c, 1/r_vk_hdrGamma). Folds GL's AutoExposure +
// GLNonlinearizeFilter into one fullscreen pass.

#version 450

layout(binding = 0) uniform sampler2D sceneTexture;
layout(binding = 1) uniform sampler2D gainTexture;

layout(push_constant) uniform Params {
	float invGamma; // 1.0 / r_vk_hdrGamma  (default 2.2 → 0.4545)
} pc;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out vec4 outColor;

void main() {
	vec3 color = texture(sceneTexture, texCoord).rgb;
	float gain = texture(gainTexture, vec2(0.5, 0.5)).r;
	color = max(color * gain, 0.0);     // clamp negatives before pow
	color = pow(color, vec3(pc.invGamma));
	outColor = vec4(color, 1.0);
}
