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
	float padding1;
	vec3 customColor;
	float padding2;
} pushConstants;

layout(location = 0) in uvec3 positionAttribute;
layout(location = 1) in uvec3 colorAttribute;  // RGB color stored in u,v as (R, G, B)
layout(location = 2) in ivec3 normalAttribute;

layout(location = 0) out vec4 color;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec3 customColorOut;
layout(location = 3) out float lighting;

void main() {
	// Convert uint8 position to float
	vec3 position = vec3(positionAttribute);
	vec4 worldPos = vec4(position + pushConstants.modelOrigin, 1.0);
	gl_Position = pushConstants.projectionViewMatrix * worldPos;

	// Convert int8 normal to float and normalize
	vec3 normalFloat = normalize(vec3(normalAttribute));

	// Simple lighting from above
	vec3 sunDir = normalize(vec3(-1.0, -1.0, -1.0));
	lighting = max(dot(normalFloat, sunDir), 0.2);

	// Convert color from uint8 [0,255] to float [0,1]
	// Pass the unlit color to fragment shader so it can detect black voxels
	vec3 vertexColor = vec3(colorAttribute) / 255.0;

	color = vec4(vertexColor, 1.0);
	normal = normalFloat;
	customColorOut = pushConstants.customColor;
}
