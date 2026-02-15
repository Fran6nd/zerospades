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
} pushConstants;

layout(location = 0) in uvec3 positionAttribute;
layout(location = 1) in uvec2 aoCoordAttribute;
layout(location = 2) in uvec3 colorAttribute;  // colorRed, colorGreen, colorBlue
layout(location = 3) in ivec3 normalAttribute;

layout(location = 0) out vec4 color;          // xyz = linearized vertex color, w = sun lambert
layout(location = 1) out vec3 ambientLight;    // ambient lighting component
layout(location = 2) out vec3 fogDensity;
layout(location = 3) out vec3 outFogColor;
layout(location = 4) out vec3 shadowCoord;     // shadow map coordinates

void main() {
	// Convert uint8 position to float
	vec3 position = vec3(positionAttribute);
	vec4 worldPos = vec4(position + pushConstants.modelOrigin, 1.0);
	gl_Position = pushConstants.projectionViewMatrix * worldPos;

	// Convert int8 normal to float and normalize
	vec3 normalFloat = normalize(vec3(normalAttribute));

	// Sun direction matching OpenGL: normalize(vec3(0, -1, -1))
	vec3 sunDir = normalize(vec3(0.0, -1.0, -1.0));
	float lambert = max(dot(normalFloat, sunDir), 0.0);

	// Convert color from uint8 [0,255] to float [0,1] and linearize
	vec3 vertexColor = vec3(colorAttribute) / 255.0;
	vertexColor *= vertexColor;

	// Ambient light matching OpenGL null radiosity formula
	// fogColor is already linearized in C++ code
	float hemisphere = 1.0 - normalFloat.z * 0.2;
	vec3 ambientColor = mix(pushConstants.fogColor, vec3(1.0), 0.5);
	vec3 ambient = ambientColor * 0.5 * hemisphere;

	// Pass vertex color (xyz) and sun lambert (w) to fragment shader
	color = vec4(vertexColor, lambert);
	ambientLight = ambient;

	// Shadow coordinate matching OpenGL Map.vs: PrepareForMapShadow
	// mapShadowCoord.y -= mapShadowCoord.z (diagonal sun projection)
	// mapShadowCoord.z /= 255.0 (normalize height)
	// mapShadowCoord.xy /= 512.0 (normalize to texture coords)
	vec3 wPos = worldPos.xyz;
	shadowCoord = vec3(wPos.x / 512.0, (wPos.y - wPos.z) / 512.0, wPos.z / 255.0);

	// Fog density based on horizontal distance (matching SW/GL implementation)
	vec2 horzRelativePos = worldPos.xy - pushConstants.viewOrigin.xy;
	float horzDistance = dot(horzRelativePos, horzRelativePos);
	fogDensity = vec3(min(horzDistance / (pushConstants.fogDistance * pushConstants.fogDistance), 1.0));
	outFogColor = pushConstants.fogColor;
}
