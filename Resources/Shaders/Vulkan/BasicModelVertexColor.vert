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
layout(location = 3) in uint aoXAttribute;     // per-face AO atlas u (0..255)
layout(location = 4) in uint aoYAttribute;     // per-face AO atlas v (0..255)

layout(location = 0) out vec4 color;           // xyz = vertexColor, w = sun lambert
layout(location = 1) out vec3 ambientLight;    // hemisphere ambient fallback
layout(location = 2) out vec3 customColorOut;
layout(location = 3) out vec3 shadowCoord;     // shadow map coordinates
layout(location = 4) out vec3 fogDensityOut;
layout(location = 5) out vec3 outFogColor;
layout(location = 6) out vec3 aoCoord;          // 3D coords into AO texture
layout(location = 7) out vec3 radiosityTextureCoord; // 3D coords into radiosity textures
layout(location = 8) out vec3 normalVarying;    // world-space surface normal
layout(location = 9) out vec2 ambientOcclusionCoord; // 2D coords into AO atlas

// Keep gl_Position bit-identical with ModelDynamicLit.vert so the additive
// dynamic-light pass (depth test EQUAL) matches this opaque pass's depth and
// the weapon model doesn't flicker.
invariant gl_Position;

void main() {
	// Convert uint8 position to float
	vec3 position = vec3(positionAttribute);
	vec4 localPos = vec4(position + pushConstants.modelOrigin, 1.0);
	gl_Position = pushConstants.projectionViewMatrix * localPos;

	// World position via model matrix
	vec3 worldPos = (pushConstants.modelMatrix * localPos).xyz;

	// Transform normal to world space via model matrix (handles mirrored models correctly)
	vec3 normalFloat = normalize((pushConstants.modelMatrix * vec4(normalize(vec3(normalAttribute)), 0.0)).xyz);

	// Sun direction matching OpenGL: normalize(vec3(0, -1, -1))
	vec3 sunDir = normalize(vec3(0.0, -1.0, -1.0));
	float lambert = max(dot(normalFloat, sunDir), 0.0);

	// Ambient color matching GL GLShadowShader: fog * 0.5 with a minimum
	// luminance floor of 0.35 so things stay visible even when the sky is
	// near-black. (fogColor is already linearized in C++.)
	float hemisphere = 1.0 - normalFloat.z * 0.2;
	vec3 ac = pushConstants.fogColor * 0.5;
	float L = (ac.x + ac.y + ac.z) / 3.0;
	ac += ((ac + 0.003) / (L + 0.003)) * max(0.35 - L, 0.0);
	ambientLight = ac * hemisphere;

	// Pass vertex color + lambert to fragment shader
	vec3 vertexColor = vec3(colorAttribute) / 255.0;
	color = vec4(vertexColor, lambert);
	customColorOut = pushConstants.customColor;

	// Shadow map coordinates (sun projects diagonally along y-z)
	shadowCoord = vec3(worldPos.x / 512.0, (worldPos.y - worldPos.z) / 512.0, worldPos.z / 255.0);

	// Fog density pre-computed on CPU from model world position
	fogDensityOut = vec3(pushConstants.fogDensity);
	outFogColor = pushConstants.fogColor;

	// AO 3D-texture coords (matches BasicMap; 512x512x65 texture).
	aoCoord = (worldPos + vec3(0.0, 0.0, 1.0)) / vec3(512.0, 512.0, 65.0);

	// Radiosity 3D-texture coords (matches GL MapRadiosity.vs).
	radiosityTextureCoord = worldPos / vec3(512.0, 512.0, 64.0);
	normalVarying = normalFloat;

	// 2D AO atlas coords (matches GL OptimizedVoxelModel.fs).  aoX/aoY are the
	// 256-pixel-space tile + corner offsets baked per-face in EmitFace.
	ambientOcclusionCoord = (vec2(aoXAttribute, aoYAttribute) + 0.5) * (1.0 / 256.0);
}
