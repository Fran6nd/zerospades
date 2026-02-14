#version 450

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

// Vulkan uses SRGB framebuffer
#define LINEAR_FRAMEBUFFER 1

layout(location = 0) in vec3 v_fogDensity;
layout(location = 1) in vec3 v_screenPosition;
layout(location = 2) in vec3 v_viewPosition;
layout(location = 3) in vec3 v_worldPosition;

layout(binding = 0) uniform sampler2D screenTexture;
layout(binding = 1) uniform sampler2D depthTexture;
layout(binding = 2) uniform sampler2D mainTexture;
layout(binding = 3) uniform sampler2D waveTexture;

layout(std140, binding = 4) uniform WaterUBO {
	vec4 fogColor; // xyz used
	vec4 skyColor; // xyz used
	vec2 zNearFar;
	vec2 _pad0;
	vec4 fovTan;
	vec4 waterPlane;
	vec4 viewOriginVector; // use .xyz
	vec2 displaceScale;
	vec2 _pad1;
} waterUBO;

layout(location = 0) out vec4 fragColor;

// Sun direction from OpenGL reference: (0, -1, -1) normalized
const vec3 sunDirection = normalize(vec3(0.0, -1.0, -1.0));

// Sun lighting: matches OpenGL implementation
// Returns vec3(0.6) for full sunlight (shadows not implemented yet)
vec3 EvaluateSunLight() {
	return vec3(0.6); // Placeholder - should multiply by shadow visibility
}

// Ambient lighting: matches OpenGL implementation
vec3 EvaluateAmbientLight(float detailAmbientOcclusion) {
	return vec3(0.3, 0.3, 0.35) * detailAmbientOcclusion;
}

float decodeDepth(float w, float near, float far) {
	return far * near / mix(far, near, w);
}

float depthAt(vec2 pt) {
	float w = texture(depthTexture, pt).x;
	return decodeDepth(w, waterUBO.zNearFar.x, waterUBO.zNearFar.y);
}

void main() {
	vec3 worldPositionFromOrigin = v_worldPosition - waterUBO.viewOriginVector.xyz;
	vec4 waveCoord = v_worldPosition.xyxy
		* vec4(vec2(0.08), vec2(0.15704))
		+ vec4(0.0, 0.0, 0.754, 0.1315);

	vec2 waveCoord2 = v_worldPosition.xy * 0.02344 + vec2(0.154, 0.7315);

	// evaluate waveform
	vec3 wave = texture(waveTexture, waveCoord.xy).xyz;
	wave = mix(vec3(-1.0), vec3(1.0), wave);
	wave.xy *= 0.08 / 200.0;

	// detail (Far Cry seems to use this technique)
	vec2 wave2 = texture(waveTexture, waveCoord.zw).xy;
	wave2 = mix(vec2(-1.0), vec2(1.0), wave2);
	wave2.xy *= 0.15704 / 200.0;
	wave.xy += wave2;

	// rough
	wave2 = texture(waveTexture, waveCoord2.xy).xy;
	wave2 = mix(vec2(-1.0), vec2(1.0), wave2);
	wave2.xy *= 0.02344 / 200.0;
	wave.xy += wave2;

	wave.z = (1.0 / 128.0);
	wave.xyz = normalize(wave.xyz);

	vec2 origScrPos = v_screenPosition.xy / v_screenPosition.z;
	vec2 scrPos = origScrPos;

	float scale = 1.0 / v_viewPosition.z;
	vec2 disp = wave.xy * 0.1;
	scrPos += disp * scale * waterUBO.displaceScale * 4.0;

	// check envelope length.
	// if the displaced location points the out of the water,
	// reset to the original pos.
	float depth = depthAt(scrPos);
	// zNearFar is stored in waterUBO.zNearFar


	// convert to view coord
	vec3 sampledViewCoord = vec3(mix(waterUBO.fovTan.zw, waterUBO.fovTan.xy, scrPos), 1.0) * -depth;
	float planeDistance = dot(vec4(sampledViewCoord, 1.0), waterUBO.waterPlane);
 	if (planeDistance > 0.0) {
		// reset!
		// original pos must be in the water.
		scrPos = origScrPos;
		depth = depthAt(scrPos);
		if (depth + v_viewPosition.z < 0.0) {
			// if the pixel is obscured by a object,
			// this fragment of water is not visible
			//discard; done by early-Z test
		}
	} else {
		depth = planeDistance / dot(waterUBO.waterPlane, vec4(0.0, 0.0, 1.0, 0.0));
		depth = abs(depth);
			depth -= v_viewPosition.z;
	}

	float envelope = clamp((depth + v_viewPosition.z), 0.0, 1.0);
	envelope = 1.0 - (1.0 - envelope) * (1.0 - envelope);

	// water color
	// TODO: correct integral
	vec2 waterCoord = v_worldPosition.xy;
	vec2 integralCoord = floor(waterCoord) + 0.5;
	vec2 blurDir = (worldPositionFromOrigin.xy);
	blurDir /= max(length(blurDir), 1.0);
	vec2 blurDirSign = mix(vec2(-1.0), vec2(1.0), step(0.0, blurDir));
	vec2 startPos = (waterCoord - integralCoord) * blurDirSign;
	vec2 diffPos = blurDir * envelope * blurDirSign * 0.5 /*limit blur*/;
	vec2 subCoord = 1.0 - clamp((vec2(0.5) - startPos) / diffPos, 0.0, 1.0);
	vec2 sampCoord = integralCoord + subCoord * blurDirSign;
	vec3 waterColor = texture(mainTexture, sampCoord / 512.0).xyz;

	// underwater object color
	fragColor = texture(screenTexture, scrPos);
#if !LINEAR_FRAMEBUFFER
	fragColor.xyz *= fragColor.xyz; // screen color to linear
#endif

	// apply fog color to water color now.
	// note that fog is already applied to underwater object.
	waterColor = mix(waterColor, waterUBO.fogColor.xyz, v_fogDensity);

	// blend water color with the underwater object's color.
	fragColor.xyz = mix(fragColor.xyz, waterColor, envelope);

	// attenuation factor for addition blendings below
	vec3 att = 1.0 - v_fogDensity;

	// reflectivity
	vec3 sunlight = EvaluateSunLight();
	vec3 ongoing = normalize(worldPositionFromOrigin);
	float reflective = dot(wave, ongoing);
	reflective = clamp(1.0 - reflective, 0.0, 1.0);
	reflective *= reflective;
	reflective *= reflective;
	reflective += 0.03;

	// fresnel refrection to sky
	fragColor.xyz = mix(fragColor.xyz,
		mix(waterUBO.skyColor.xyz * reflective * 0.6,
			waterUBO.fogColor.xyz, v_fogDensity), reflective);

	// specular reflection
	if (dot(sunlight, vec3(1.0)) > 0.0001) {
		vec3 refl = reflect(ongoing, wave);
		float spec = max(dot(refl, sunDirection), 0.0);
		spec *= spec; // ^2
		spec *= spec; // ^4
		spec *= spec; // ^16
		spec *= spec; // ^32
		spec *= spec; // ^64
		spec *= spec; // ^128
		spec *= spec; // ^256
		spec *= spec; // ^512
		spec *= spec; // ^1024
		spec *= reflective;
		fragColor.xyz += sunlight * spec * 1000.0 * att;
	}

#if !LINEAR_FRAMEBUFFER
	fragColor.xyz = sqrt(fragColor.xyz);
#endif

	fragColor.w = 1.0;
}
