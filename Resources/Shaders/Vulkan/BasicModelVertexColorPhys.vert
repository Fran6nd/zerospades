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
	float _pad2;
	mat4 viewMatrix;
	vec3 viewOrigin;
} pushConstants;

layout(location = 0) in uvec3 positionAttribute;
layout(location = 1) in uvec3 colorAttribute;
layout(location = 2) in ivec3 normalAttribute;

layout(location = 0) out vec4 color;           // xyz = vertexColor, w = sun lambert (may be negative)
layout(location = 1) out vec3 ambientLight;
layout(location = 2) out vec3 customColorOut;
layout(location = 3) out vec3 shadowCoord;
layout(location = 4) out vec3 fogDensityOut;
layout(location = 5) out vec3 outFogColor;
layout(location = 6) out vec3 viewSpaceCoord;
layout(location = 7) out vec3 viewSpaceNormal;
layout(location = 8) out vec3 reflectionDir;

void main() {
	vec3 position = vec3(positionAttribute);
	vec4 localPos = vec4(position + pushConstants.modelOrigin, 1.0);
	gl_Position = pushConstants.projectionViewMatrix * localPos;

	// World position via model matrix
	vec3 worldPos = (pushConstants.modelMatrix * localPos).xyz;

	vec3 normalFloat = normalize(vec3(normalAttribute));

	// Transform normal to world space
	vec3 worldNormal = normalize((pushConstants.modelMatrix * vec4(normalFloat, 0.0)).xyz);

	// Sun direction
	vec3 sunDir = normalize(vec3(0.0, -1.0, -1.0));
	float lambert = dot(worldNormal, sunDir); // NOT clamped

	// Ambient light (null radiosity formula)
	float hemisphere = 1.0 - worldNormal.z * 0.2;
	vec3 ambientColor = mix(pushConstants.fogColor, vec3(1.0), 0.5);
	ambientLight = ambientColor * 0.5 * hemisphere;

	// Vertex color + lambert
	vec3 vertexColor = vec3(colorAttribute) / 255.0;
	color = vec4(vertexColor, lambert);
	customColorOut = pushConstants.customColor;

	// Shadow coordinates
	shadowCoord = vec3(worldPos.x / 512.0, (worldPos.y - worldPos.z) / 512.0, worldPos.z / 255.0);

	// Fog
	fogDensityOut = vec3(pushConstants.fogDensity);
	outFogColor = pushConstants.fogColor;

	// View-space data for physical lighting
	vec4 viewModelPos = pushConstants.viewMatrix * pushConstants.modelMatrix * localPos;
	viewSpaceCoord = viewModelPos.xyz;
	viewSpaceNormal = normalize((pushConstants.viewMatrix * vec4(worldNormal, 0.0)).xyz);
	reflectionDir = reflect(worldPos - pushConstants.viewOrigin, worldNormal);
}
