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
	mat4 modelMatrix;
	vec3 modelOrigin;
	float fogDensity;
	vec3 customColor;
	float _pad;
	vec3 fogColor;
} pushConstants;

layout(location = 0) in uvec3 positionAttribute;
layout(location = 1) in uvec3 colorAttribute;  // RGB color stored in u,v as (R, G, B)
layout(location = 2) in ivec3 normalAttribute;

layout(location = 0) out vec4 color;           // xyz = vertexColor, w = sun lambert
layout(location = 1) out vec3 ambientLight;    // ambient lighting component
layout(location = 2) out vec3 customColorOut;
layout(location = 3) out vec3 shadowCoord;     // shadow map coordinates
layout(location = 4) out vec3 fogDensityOut;
layout(location = 5) out vec3 outFogColor;

void main() {
	// Convert uint8 position to float
	vec3 position = vec3(positionAttribute);
	vec4 localPos = vec4(position + pushConstants.modelOrigin, 1.0);
	gl_Position = pushConstants.projectionViewMatrix * localPos;

	// World position via model matrix
	vec3 worldPos = (pushConstants.modelMatrix * localPos).xyz;

	// Convert int8 normal to float and normalize
	vec3 normalFloat = normalize(vec3(normalAttribute));

	// Sun direction matching OpenGL: normalize(vec3(0, -1, -1))
	vec3 sunDir = normalize(vec3(0.0, -1.0, -1.0));
	float lambert = max(dot(normalFloat, sunDir), 0.0);

	// Ambient light matching OpenGL null radiosity formula
	// fogColor is already linearized in C++ code
	float hemisphere = 1.0 - normalFloat.z * 0.2;
	vec3 ambientColor = mix(pushConstants.fogColor, vec3(1.0), 0.5);
	ambientLight = ambientColor * 0.5 * hemisphere;

	// Pass vertex color + lambert to fragment shader
	vec3 vertexColor = vec3(colorAttribute) / 255.0;
	color = vec4(vertexColor, lambert);
	customColorOut = pushConstants.customColor;

	// Shadow map coordinates (sun projects diagonally along y-z)
	shadowCoord = vec3(worldPos.x / 512.0, (worldPos.y - worldPos.z) / 512.0, worldPos.z / 255.0);

	// Fog density pre-computed on CPU from model world position
	fogDensityOut = vec3(pushConstants.fogDensity);
	outFogColor = pushConstants.fogColor;
}
