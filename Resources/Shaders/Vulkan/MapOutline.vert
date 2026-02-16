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

layout(push_constant) uniform PushConstants {
	mat4 projectionViewMatrix;
	vec3 modelOrigin;
	float fogDistance;
	vec3 viewOrigin;
	float _pad;
	vec3 fogColor;
} pc;

layout(location = 0) in uvec3 positionAttribute;

layout(location = 0) out float fogDensity;
layout(location = 1) out vec3 outFogColor;

void main() {
	vec3 position = vec3(positionAttribute);
	vec4 worldPos = vec4(position + pc.modelOrigin, 1.0);
	gl_Position = pc.projectionViewMatrix * worldPos;

	vec2 horzRelativePos = worldPos.xy - pc.viewOrigin.xy;
	float horzDistance = dot(horzRelativePos, horzRelativePos);
	fogDensity = min(horzDistance / (pc.fogDistance * pc.fogDistance), 1.0);
	outFogColor = pc.fogColor;
}
