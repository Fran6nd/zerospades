/*
 Copyright (c) 2013 Fran6nd

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
layout(location = 1) out vec3 ambientLight;    // ambient lighting component (hemisphere fallback)
layout(location = 2) out vec3 fogDensity;
layout(location = 3) out vec3 outFogColor;
layout(location = 4) out vec3 shadowCoord;     // shadow map coordinates
layout(location = 5) out vec3 aoCoord;         // 3D coords into ambient-occlusion texture
layout(location = 6) out vec3 radiosityTextureCoord; // 3D coords into radiosity textures
layout(location = 7) out vec3 normalVarying;   // per-face normal in world space

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

	// Ambient color matching GL GLShadowShader: fog * 0.5 with a minimum
	// luminance floor of 0.35 so things stay visible even when the sky is
	// near-black. (fogColor is already linearized in C++.)
	float hemisphere = 1.0 - normalFloat.z * 0.2;
	vec3 ac = pushConstants.fogColor * 0.5;
	float L = (ac.x + ac.y + ac.z) / 3.0;
	ac += ((ac + 0.003) / (L + 0.003)) * max(0.35 - L, 0.0);
	vec3 ambient = ac * hemisphere;

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

	// AO 3D-texture coords. World position with z+1 (the 0-th slice is the
	// "below ground" guard plane), divided by texture extent. Map dimensions
	// are 512x512x64 in this game, so the texture is 512x512x65.
	aoCoord = (worldPos.xyz + vec3(0.0, 0.0, 1.0)) / vec3(512.0, 512.0, 65.0);

	// Radiosity 3D-texture coords (matches GL MapRadiosity.vs).
	radiosityTextureCoord = worldPos.xyz / vec3(512.0, 512.0, 64.0);
	normalVarying = normalFloat;
}
