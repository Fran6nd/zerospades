/*
 Copyright (c) 2015 yvt

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

// Multiplies the scene colour by the exposure gain stored in the
// persistent 1×1 accumulator, writing the result to the output image.

#version 450

layout(binding = 0) uniform sampler2D sceneTexture;
layout(binding = 1) uniform sampler2D gainTexture;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out vec4 outColor;

void main() {
	vec3 color = texture(sceneTexture, texCoord).rgb;
	float gain = texture(gainTexture, vec2(0.5, 0.5)).r;
	outColor = vec4(color * gain, 1.0);
}
