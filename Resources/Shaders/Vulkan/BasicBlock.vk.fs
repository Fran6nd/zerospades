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

// Inputs from vertex shader
layout(location = 0) in vec2 ambientOcclusionCoord;
layout(location = 1) in vec4 color;
layout(location = 2) in vec3 fogDensity;

// Uniform buffer for fog color
layout(binding = 0) uniform UniformBufferObject {
	mat4 projectionViewMatrix;
	mat4 viewMatrix;
	vec3 chunkPosition;
	float padding1;
	vec3 sunLightDirection;
	float padding2;
	vec3 viewOriginVector;
	float fogDistance;
} ubo;

// Additional uniforms
layout(binding = 1) uniform FogUniforms {
	vec3 fogColor;
} fog;

// Textures
layout(binding = 2) uniform sampler2D ambientOcclusionTexture;

// Output
layout(location = 0) out vec4 fragColor;

// Shadow/lighting functions (simplified for now)
vec3 EvaluateSunLight() {
	// TODO: Implement proper shadow evaluation
	return vec3(0.6); // Stub: assume full sunlight
}

vec3 EvaluateAmbientLight(float detailAmbientOcclusion) {
	// TODO: Implement proper radiosity evaluation
	return vec3(0.4) * detailAmbientOcclusion; // Stub: simple ambient
}

void main() {
	// color is linear
	fragColor = vec4(color.xyz, 1.0);

	float ao = texture(ambientOcclusionTexture, ambientOcclusionCoord).x;
	vec3 diffuseShading = EvaluateAmbientLight(ao);
	diffuseShading += vec3(color.w) * EvaluateSunLight();

	// apply diffuse shading
	fragColor.xyz *= diffuseShading;

	// apply fog fading
	fragColor.xyz = mix(fragColor.xyz, fog.fogColor, fogDensity);

#if !LINEAR_FRAMEBUFFER
	// gamma correct
	fragColor.xyz = sqrt(fragColor.xyz);
#endif
}
