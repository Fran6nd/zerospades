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

#version 450

layout(location = 0) in vec3 scanPos;
layout(location = 1) in vec2 circlePos;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform ScannerUniforms {
	vec4 scanRange;
	vec4 drawRange;
	float scanZ;
	float radius;
};

layout(binding = 1) uniform sampler2D depthTexture;

void main() {
	float depth = texture(depthTexture, scanPos.xy).r;
	float val = (depth >= scanPos.z) ? 1.0 : 0.0;

	// circle trim
	float rad = length(circlePos);
	rad *= radius;
	rad = clamp(radius - 1.0 - rad, 0.0, 1.0);
	val *= rad;

	outColor = vec4(vec3(val), 1.0);
}
