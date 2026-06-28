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

// Selects the radiosity-on permutation (matches GL MapRadiosity.fs) vs the
// no-radiosity permutation (matches GL MapRadiosityNull.fs + BasicBlock.fs).
// Driven by r_radiosity at pipeline-creation time.
layout(constant_id = 0) const int USE_RADIOSITY = 0;

layout(set = 0, binding = 0) uniform sampler2D mapShadowTexture;
layout(set = 0, binding = 1) uniform sampler3D ambientShadowTexture;
layout(set = 0, binding = 2) uniform sampler3D radiosityTextureFlat;
layout(set = 0, binding = 3) uniform sampler3D radiosityTextureX;
layout(set = 0, binding = 4) uniform sampler3D radiosityTextureY;
layout(set = 0, binding = 5) uniform sampler3D radiosityTextureZ;
layout(set = 0, binding = 6) uniform sampler2D ambientOcclusionAtlas; // Gfx/AmbientOcclusion.png

// Set 1: dynamic model-shadow cascades (owned by VulkanShadowMapRenderer).
layout(set = 1, binding = 0) uniform ShadowSampling {
	mat4 cascadeMatrix[3];
	int enabled;
} shadowSampling;
layout(set = 1, binding = 1) uniform sampler2D modelShadowMap0;
layout(set = 1, binding = 2) uniform sampler2D modelShadowMap1;
layout(set = 1, binding = 3) uniform sampler2D modelShadowMap2;

layout(location = 0) in vec4 color;           // xyz = linearized vertex color, w = sun lambert
layout(location = 1) in vec3 ambientLight;     // hemisphere ambient fallback (unused, kept for VS↔FS compat)
layout(location = 2) in vec3 fogDensity;
layout(location = 3) in vec3 inFogColor;
layout(location = 4) in vec3 shadowCoord;      // shadow map coordinates
layout(location = 5) in vec3 aoCoord;          // 3D coords into AO texture
layout(location = 6) in vec3 radiosityTextureCoord; // 3D coords into radiosity textures
layout(location = 7) in vec3 normalVarying;    // surface normal in world space
layout(location = 8) in vec2 ambientOcclusionCoord; // 2D coords into AO atlas
layout(location = 9) in vec3 modelShadowCoord0;     // light-clip coords per cascade
layout(location = 10) in vec3 modelShadowCoord1;
layout(location = 11) in vec3 modelShadowCoord2;

layout(location = 0) out vec4 fragColor;

// Sample one cascade. Returns 1 lit, 0 shadowed, or -1 if the fragment is
// outside this cascade's box (caller falls through to the next, coarser one).
float SampleModelCascade(sampler2D tex, vec3 c) {
	vec2 uv = c.xy * 0.5 + 0.5; // clip [-1,1] -> texcoord [0,1]
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || c.z < 0.0 || c.z > 1.0)
		return -1.0;
	float stored = texture(tex, uv).r;
	// Local Z 0 = sun side; an occluder with smaller stored depth is nearer the
	// sun, so this fragment is shadowed. Bias avoids self-shadow acne.
	return (stored < c.z - 0.0015) ? 0.0 : 1.0;
}

// Dynamic (player/grenade) shadow term, combined multiplicatively with the
// map shadow. Finest cascade first; outside all cascades = lit.
float EvaluteModelShadow() {
	if (shadowSampling.enabled == 0)
		return 1.0;
	float v = SampleModelCascade(modelShadowMap0, modelShadowCoord0);
	if (v < 0.0) v = SampleModelCascade(modelShadowMap1, modelShadowCoord1);
	if (v < 0.0) v = SampleModelCascade(modelShadowMap2, modelShadowCoord2);
	return (v < 0.0) ? 1.0 : v;
}

// Linear (RGB10A2) decode of radiosity values. Mirrors GL MapRadiosity.fs
// DecodeRadiosityValue, but only the high-precision (linear) branch — the
// Vulkan port stores radiosity in A2R10G10B10_UNORM_PACK32 always.
vec3 DecodeRadiosityValue(vec3 val) {
	val *= 1023.0 / 1022.0;
	val = (val * 2.0) - 1.0;
	return val;
}

void main() {
	// Map shadow (matches GL Map.fs: EvaluateMapShadow)
	float shadowVal = texture(mapShadowTexture, shadowCoord.xy).w;
	float shadow = (shadowVal < shadowCoord.z - 0.0001) ? 0.0 : 1.0;

	// Fold in dynamic model shadows (GL: VisibilityOfSunLight = map * model).
	shadow *= EvaluteModelShadow();

	vec3 nrm = normalize(normalVarying);
	vec3 vertexColor = color.xyz;
	float sunLambert = color.w;

	// Sun contribution — matches GL Common.fs EvaluateSunLight() * color.w
	vec3 sun = vec3(0.6) * sunLambert * shadow;

	// Per-block ambient occlusion (sampled from 3D ambient shadow texture).
	// .x = AO accumulation, .y = sample weight (1 in air, 0 in solids).
	vec2 ambTexVal = texture(ambientShadowTexture, aoCoord).xy;
	float aoFactor = max(ambTexVal.x / max(ambTexVal.y, 0.25), 0.0);

	vec3 diffuse;
	if (USE_RADIOSITY != 0) {
		// MapRadiosity.fs path — directional radiosity + ambient·skyAmbient.
		vec3 radiosity = DecodeRadiosityValue(texture(radiosityTextureFlat, radiosityTextureCoord).xyz);
		radiosity += nrm.x * DecodeRadiosityValue(texture(radiosityTextureX, radiosityTextureCoord).xyz);
		radiosity += nrm.y * DecodeRadiosityValue(texture(radiosityTextureY, radiosityTextureCoord).xyz);
		radiosity += nrm.z * DecodeRadiosityValue(texture(radiosityTextureZ, radiosityTextureCoord).xyz);
		radiosity = max(radiosity, 0.0) * 1.5;

		// Ambient color matches GL GLShadowShader: fog * 0.5 with a min-luminance
		// floor of 0.35 (keeps things visible when the sky is near-black).
		float aoTerm = aoFactor * (0.8 - nrm.z * 0.2);
		vec3 ambientColor = inFogColor * 0.5;
		float ambL = (ambientColor.x + ambientColor.y + ambientColor.z) / 3.0;
		ambientColor += ((ambientColor + 0.003) / (ambL + 0.003)) * max(0.35 - ambL, 0.0);

		diffuse = radiosity + aoTerm * ambientColor + sun;
	} else {
		// MapRadiosityNull.fs path — ambient = mix(fog, white, 0.5) * 0.5 * ao * hemisphere.
		// Hemisphere is (1 - normal.z * 0.2), not the radiosity path's (0.8 - normal.z * 0.2).
		// AO is sampled from the 2D AmbientOcclusion atlas (per-vertex tile coord),
		// matching GL BasicBlock.fs — gives crisper voxel-corner AO than the
		// 3D ambientShadowTexture used in the radiosity branch.
		//
		// Note: VulkanImageManager y-flips bitmaps on upload (so top-down PNG
		// data lands top-up in screen-space 2D UI rendering). The AO atlas is
		// sampled from world geometry though, not screen space — and GL uploads
		// the same PNG without a flip, so its tile order is opposite to ours.
		// Invert v here so aoID maps to the same physical tile as GL.
		vec2 aoUV = vec2(ambientOcclusionCoord.x, 1.0 - ambientOcclusionCoord.y);
		float ao = texture(ambientOcclusionAtlas, aoUV).x;
		float hemisphere = 1.0 - nrm.z * 0.2;
		vec3 ambientColor = mix(inFogColor, vec3(1.0), 0.5);
		diffuse = ambientColor * (0.5 * ao * hemisphere) + sun;
	}

	fragColor = vec4(vertexColor * diffuse, 1.0);

	// Fog fade
	fragColor.xyz = mix(fragColor.xyz, inFogColor, fogDensity);
}
