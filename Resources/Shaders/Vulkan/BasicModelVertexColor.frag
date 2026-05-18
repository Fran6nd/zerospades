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

layout(set = 0, binding = 0) uniform sampler2D mapShadowTexture;
layout(set = 0, binding = 1) uniform sampler3D ambientShadowTexture;
layout(set = 0, binding = 2) uniform sampler3D radiosityTextureFlat;
layout(set = 0, binding = 3) uniform sampler3D radiosityTextureX;
layout(set = 0, binding = 4) uniform sampler3D radiosityTextureY;
layout(set = 0, binding = 5) uniform sampler3D radiosityTextureZ;

layout(location = 0) in vec4 color;           // xyz = vertexColor, w = sun lambert
layout(location = 1) in vec3 ambientLight;     // hemisphere ambient fallback (kept for VS↔FS compat)
layout(location = 2) in vec3 customColor;
layout(location = 3) in vec3 shadowCoord;
layout(location = 4) in vec3 fogDensity;
layout(location = 5) in vec3 inFogColor;
layout(location = 6) in vec3 aoCoord;          // 3D coords into AO texture
layout(location = 7) in vec3 radiosityTextureCoord;
layout(location = 8) in vec3 normalVarying;

layout(location = 0) out vec4 fragColor;

vec3 DecodeRadiosityValue(vec3 val) {
	val *= 1023.0 / 1022.0;
	val = (val * 2.0) - 1.0;
	return val;
}

void main() {
	// Evaluate map shadow (matching OpenGL Map.fs: EvaluateMapShadow)
	float shadowVal = texture(mapShadowTexture, shadowCoord.xy).w;
	float shadow = (shadowVal < shadowCoord.z - 0.0001) ? 0.0 : 1.0;

	vec3 vertexColor = color.xyz;

	// If the vertex color is very dark/black (near zero), replace with customColor
	// This allows team colors to override black voxels in player/weapon models
	if (dot(vertexColor, vec3(1.0)) < 0.0001) {
		vertexColor = customColor;
	}

	// Linearize color (after team color substitution, matching OpenGL)
	vertexColor *= vertexColor;

	// Per-block ambient occlusion (matching GL MapRadiosity.fs).
	vec2 ambTexVal = texture(ambientShadowTexture, aoCoord).xy;
	float aoFactor = max(ambTexVal.x / max(ambTexVal.y, 0.25), 0.0);

	// Directional radiosity (port of GL MapRadiosity.fs EvaluateRadiosity)
	vec3 radiosity = DecodeRadiosityValue(texture(radiosityTextureFlat, radiosityTextureCoord).xyz);
	vec3 nrm = normalize(normalVarying);
	radiosity += nrm.x * DecodeRadiosityValue(texture(radiosityTextureX, radiosityTextureCoord).xyz);
	radiosity += nrm.y * DecodeRadiosityValue(texture(radiosityTextureY, radiosityTextureCoord).xyz);
	radiosity += nrm.z * DecodeRadiosityValue(texture(radiosityTextureZ, radiosityTextureCoord).xyz);
	radiosity = max(radiosity, 0.0) * 1.5;

	float aoTerm = aoFactor * (0.8 - nrm.z * 0.2);
	vec3 ambientColor = mix(inFogColor, vec3(1.0), 0.5);

	// Combine lighting: directional radiosity + AO·skyAmbient + sun · shadow
	float sunLambert = color.w;
	vec3 sun = vec3(0.6) * sunLambert * shadow;
	fragColor = vec4(vertexColor * (radiosity + aoTerm * ambientColor + sun), 1.0);

	// Apply fog fading
	fragColor.xyz = mix(fragColor.xyz, inFogColor, fogDensity);

	// Gamma correct.  This matches BasicBlock.vk.fs (and the GL renderer
	// without r_hdr): the shader produces a pseudo-sRGB value, then the
	// framebuffer stores it as UNORM.  Without this step, players were
	// being written linearly and ended up visibly darker than the world
	// blocks at the same shading level.
	fragColor.xyz = sqrt(max(fragColor.xyz, 0.0));
}
