/*
 Copyright (c) 2017 yvt

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

layout(location = 0) in vec2 positionAttribute;

layout(location = 0) out vec2 texCoord;
layout(location = 1) out vec4 viewcentricWorldPositionPartial;

layout(binding = 4) uniform TemporalAAUniforms {
	vec2 inverseVP;
	float fogDistance;
	float _pad0;
	mat4 reprojectionMatrix;
	mat4 viewProjectionMatrixInv;
};

void main() {
	vec2 pos = positionAttribute;
	vec2 scrPos = pos * 2.0 - 1.0;

	gl_Position = vec4(scrPos, 0.5, 1.0);

	texCoord = pos;
	viewcentricWorldPositionPartial = viewProjectionMatrixInv * vec4(pos, 0.0, 1.0);
}
