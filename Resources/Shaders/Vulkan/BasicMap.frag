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

// set 0: static height-map shadow
layout(set = 0, binding = 0) uniform sampler2D mapShadowTexture;

// set 1: cascaded dynamic shadow maps
layout(set = 1, binding = 0) uniform CascadeData {
	mat4 lightSpaceMatrix[3];
} cascade;
layout(set = 1, binding = 1) uniform sampler2D cascadeShadow0;
layout(set = 1, binding = 2) uniform sampler2D cascadeShadow1;
layout(set = 1, binding = 3) uniform sampler2D cascadeShadow2;

layout(location = 0) in vec4 color;           // xyz = linearized vertex color, w = sun lambert
layout(location = 1) in vec3 ambientLight;     // ambient lighting component
layout(location = 2) in vec3 fogDensity;
layout(location = 3) in vec3 inFogColor;
layout(location = 4) in vec3 shadowCoord;      // static shadow map coordinates
layout(location = 5) in vec3 inWorldPos;       // world-space position

layout(location = 0) out vec4 fragColor;

float sampleCascadeShadow(vec3 worldPos) {
	// Try each cascade from nearest to farthest
	for (int i = 0; i < 3; i++) {
		vec4 sp = cascade.lightSpaceMatrix[i] * vec4(worldPos, 1.0);
		sp.xyz /= sp.w;
		// NDC to UV [0,1]
		vec2 uv = sp.xy * 0.5 + 0.5;
		if (uv.x > 0.0 && uv.x < 1.0 && uv.y > 0.0 && uv.y < 1.0
				&& sp.z > 0.0 && sp.z < 1.0) {
			float depth;
			if (i == 0) depth = texture(cascadeShadow0, uv).r;
			else if (i == 1) depth = texture(cascadeShadow1, uv).r;
			else depth = texture(cascadeShadow2, uv).r;
			return (sp.z > depth + 0.001) ? 0.0 : 1.0;
		}
	}
	return 1.0; // outside all cascades = no shadow
}

void main() {
	// Evaluate static height-map shadow (matching OpenGL Map.fs: EvaluateMapShadow)
	float shadowVal = texture(mapShadowTexture, shadowCoord.xy).w;
	float staticShadow = (shadowVal < shadowCoord.z - 0.0001) ? 0.0 : 1.0;

	// Evaluate dynamic cascade shadow (models casting shadows)
	float dynamicShadow = sampleCascadeShadow(inWorldPos);

	// Combined shadow: both static and dynamic must be lit
	float shadow = staticShadow * dynamicShadow;

	// Combine lighting: ambient + sun * shadow
	vec3 vertexColor = color.xyz;
	float sunLambert = color.w;
	vec3 sun = vec3(0.6) * sunLambert * shadow;
	fragColor = vec4(vertexColor * (ambientLight + sun), 1.0);

	// Apply fog fading
	fragColor.xyz = mix(fragColor.xyz, inFogColor, fogDensity);
}
