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
layout(location = 4) in vec2 v_worldPositionOriginal;

layout(binding = 0) uniform sampler2D screenTexture;
layout(binding = 1) uniform sampler2D depthTexture;
layout(binding = 2) uniform sampler2D mainTexture;
layout(binding = 8) uniform sampler2DArray waveTextureArray;
layout(binding = 6) uniform sampler2D mirrorTexture;
layout(binding = 7) uniform sampler2D mirrorDepthTexture;

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
	mat4 viewMatrix;
} waterUBO;

layout(location = 0) out vec4 fragColor;

// Sun direction from OpenGL reference: (0, -1, -1) normalized
const vec3 sunDirection = normalize(vec3(0.0, -1.0, -1.0));

// GGX distribution function for specular
float GGXDistribution(float m, float dotHalf) {
	float m2 = m * m;
	float d = dotHalf * dotHalf * (m2 - 1.0) + 1.0;
	return m2 / (3.14159265 * d * d);
}

vec3 EvaluateSunLight() {
	return vec3(0.6); // Placeholder - should multiply by shadow visibility
}

vec3 EvaluateAmbientLight(float detailAmbientOcclusion) {
	return vec3(0.3, 0.3, 0.35) * detailAmbientOcclusion;
}

float decodeDepth(float w, float near, float far) {
	return far * near / mix(far, near, w);
}

float encodeDepth(float z, float near, float far) {
	// FN/(F(1-w) + Nw) = z
	// FN = z(w(N-F) + F)
	// FN = zw(N-F) + Fz
	// w = F(N - z) / z(N - F)
	return far * (near + z) / (z * (far - near));
}

float depthAt(vec2 pt) {
	float w = texture(depthTexture, pt).x;
	return decodeDepth(w, waterUBO.zNearFar.x, waterUBO.zNearFar.y);
}

void main() {
	vec3 worldPositionFromOrigin = v_worldPosition - waterUBO.viewOriginVector.xyz;
	vec4 waveCoord = v_worldPositionOriginal.xyxy * vec4(vec2(0.04), vec2(0.08704)) + vec4(0.0, 0.0, 0.754, 0.1315);
	vec2 waveCoord2 = v_worldPositionOriginal.xy * 0.00844 + vec2(0.154, 0.7315);

	// evaluate waveform (normal vector)
	vec3 wave = texture(waveTextureArray, vec3(waveCoord.xy, 0.0)).xyz;
	wave = mix(vec3(-0.0025), vec3(0.0025), wave);
	wave.xy *= 0.04 * 1.8;

	// detail
	vec2 wave2 = texture(waveTextureArray, vec3(waveCoord.zw, 1.0)).xy;
	wave2 = mix(vec2(-0.0025), vec2(0.0025), wave2);
	wave2.xy *= 0.08704 * 1.2;
	wave.xy += wave2;

	// rough
	wave2 = texture(waveTextureArray, vec3(waveCoord2.xy, 2.0)).xy;
	wave2 = mix(vec2(-0.0025), vec2(0.0025), wave2);
	wave2.xy *= 0.00844 * 2.5;
	wave.xy += wave2;

	wave.z = (1.0 / 256.0) / 4.0; // (negated normal vector!)
	wave.xyz = normalize(wave.xyz);

	vec2 origScrPos = v_screenPosition.xy / v_screenPosition.z;
	vec2 scrPos = origScrPos;

	/* ------- Refraction -------- */

	// Compute the line segment for refraction ray tracing
	vec3 normalVS = (waterUBO.viewMatrix * vec4(-wave, 0.0)).xyz;
	vec3 refractedVS = refract(normalize(v_viewPosition.xyz), normalVS, 1.0 / 1.5);
	vec3 refractTargetVS = v_viewPosition + refractedVS;
	if (refractTargetVS.z > -0.001) {
		refractTargetVS = mix(v_viewPosition, refractedVS,
			(-0.001 - v_viewPosition.z) / (refractedVS.z - v_viewPosition.z));
	}

	vec3 refractTargetNDC = vec3(
		refractTargetVS.xy / refractTargetVS.z / waterUBO.fovTan.xy,
		encodeDepth(refractTargetVS.z, waterUBO.zNearFar.x, waterUBO.zNearFar.y)
	);

	float scale = 1.0 / v_viewPosition.z;
	vec2 disp = wave.xy * 0.1;
	scrPos += disp * scale * waterUBO.displaceScale * 4.0;

	vec2 refractTargetSS = refractTargetNDC.xy * vec2(-0.5, 0.5) + 0.5;

	// Screen-space ray tracing
	float origDepth = gl_FragCoord.z;
	vec2 targetScrPos = refractTargetSS;
	float targetDepth = refractTargetNDC.z;
	float depth;
	float dither = fract(dot(fract(gl_FragCoord.xy * 0.5), vec2(0.5)));
	for (float i = dither / 16.0; i <= 1.0; i += 1.0 / 16.0) {
		float rayDepth = mix(origDepth, targetDepth, i);
		refractTargetSS = mix(origScrPos, targetScrPos, i);
		depth = texture(depthTexture, refractTargetSS).x;
		if (depth < rayDepth && // ray intersects the object
			depth > rayDepth - 0.1) { // (perhaps ray's actually going behind the object!)
			i = max(0.0, i - 1.0 / 16.0);
			refractTargetSS = mix(origScrPos, targetScrPos, i);
			depth = texture(depthTexture, refractTargetSS).x;
			break;
		}
	}

	// convert to linear Z
	depth = decodeDepth(depth, waterUBO.zNearFar.x, waterUBO.zNearFar.y);

	// make sure the sampled point is above the water plane.
	// convert to view coord
	vec3 sampledViewCoord = vec3(mix(waterUBO.fovTan.zw, waterUBO.fovTan.xy, refractTargetSS), 1.0) * -depth;
	float planeDistance = dot(vec4(sampledViewCoord, 1.0), waterUBO.waterPlane);
 	if (planeDistance < 0.0) {
		// reset!
		// original pos must be in the water.
		refractTargetSS = origScrPos;
		depth = depthAt(refractTargetSS);
		if (depth + v_viewPosition.z < 0.0) {
			// if the pixel is obscured by a object,
			// this fragment of water is not visible
			//discard; done by early-Z test
		}

		sampledViewCoord = vec3(mix(waterUBO.fovTan.zw, waterUBO.fovTan.xy, refractTargetSS), 1.0) * -depth;
	}

	float envelope = min(distance(v_viewPosition * vec3(-1.0, 1.0, 1.0), sampledViewCoord) * 0.8, 1.0);
	envelope = 1.0 - (1.0 - envelope) * (1.0 - envelope);

	vec3 sunlight = EvaluateSunLight();

	// Blend the water color
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
	waterColor *= sunlight + EvaluateAmbientLight(1.0);

	// underwater object color
	fragColor = texture(screenTexture, refractTargetSS);
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

	/* ------- Reflection -------- */

	// Compute the line segment for refraction ray tracing
	vec3 reflectedVS = reflect(normalize(v_viewPosition.xyz), normalVS);
	reflectedVS = reflect(reflectedVS, waterUBO.waterPlane.xyz); // reflection's Z position is inverted
	vec3 reflectTargetVS = v_viewPosition + reflectedVS * (abs(v_viewPosition.z) + 1.0);
	if (reflectTargetVS.z > -0.001) {
		reflectTargetVS = mix(v_viewPosition, reflectedVS,
			(-0.001 - v_viewPosition.z) / (reflectedVS.z - v_viewPosition.z));
	}

	vec3 reflectTargetNDC = vec3(
		reflectTargetVS.xy / reflectTargetVS.z / waterUBO.fovTan.xy,
		encodeDepth(reflectTargetVS.z, waterUBO.zNearFar.x, waterUBO.zNearFar.y)
	);

	vec2 reflectTargetSS = reflectTargetNDC.xy * vec2(-0.5, 0.5) + 0.5;

	// Screen-space ray tracing
	targetScrPos = reflectTargetSS;
	targetDepth = reflectTargetNDC.z;
	for (float i = dither / 16.0; i <= 1.0; i += 1.0 / 16.0) {
		float rayDepth = mix(origDepth, targetDepth, i);
		reflectTargetSS = mix(origScrPos, targetScrPos, i);
		depth = texture(mirrorDepthTexture, reflectTargetSS).x;
		if (depth < rayDepth && // ray intersects the object
			depth > rayDepth - 0.1) { // (perhaps ray's actually going behind the object!)
			i = max(0.0, i - 1.0 / 16.0);
			reflectTargetSS = mix(origScrPos, targetScrPos, i);
			depth = texture(mirrorDepthTexture, reflectTargetSS).x;
			break;
		}
	}

	// convert to linear Z
	bool reflectedSky = depth > 0.99999;
	depth = decodeDepth(depth, waterUBO.zNearFar.x, waterUBO.zNearFar.y);

	// make sure the reflection is from the above the water plane
	sampledViewCoord = vec3(mix(waterUBO.fovTan.zw, waterUBO.fovTan.xy, reflectTargetSS), 1.0) * -depth;
	planeDistance = dot(vec4(sampledViewCoord, 1.0), waterUBO.waterPlane);
	bool validReflection = planeDistance > 0.0;

	vec3 reflected = texture(mirrorTexture, reflectTargetSS).xyz;

	if (!validReflection) {
		// The mirrored framebuffer isn't providing a valid reflected image.
		// Retry ray trace on the normal framebuffer

		// Compute the line segment for refraction ray tracing
		reflectedVS = reflect(normalize(v_viewPosition.xyz), normalVS);
		reflectTargetVS = v_viewPosition + reflectedVS * (abs(v_viewPosition.z) + 1.0);
		if (reflectTargetVS.z > -0.001) {
			reflectTargetVS = mix(v_viewPosition, reflectedVS,
				(-0.001 - v_viewPosition.z) / (reflectedVS.z - v_viewPosition.z));
		}
		reflectTargetNDC = vec3(
			reflectTargetVS.xy / reflectTargetVS.z / waterUBO.fovTan.xy,
			encodeDepth(reflectTargetVS.z, waterUBO.zNearFar.x, waterUBO.zNearFar.y)
		);

		reflectTargetSS = reflectTargetNDC.xy * vec2(-0.5, 0.5) + 0.5;

		// Screen-space ray tracing
		targetScrPos = reflectTargetSS;
		targetDepth = reflectTargetNDC.z;
		for (float i = dither / 32.0; i <= 1.0; i += 1.0 / 32.0) {
			float rayDepth = mix(origDepth, targetDepth, i);
			reflectTargetSS = mix(origScrPos, targetScrPos, i);
			depth = texture(depthTexture, reflectTargetSS).x;
			if (depth < rayDepth && // ray intersects the object
				depth > rayDepth - 0.1) { // (perhaps ray's actually going behind the object!)
				//i = max(0.0, i - 1.0 / 32.0);
				//reflectTargetSS = mix(origScrPos, targetScrPos, i);
				//depth = texture(depthTexture, reflectTargetSS).x;
				break;
			}
		}

		reflectedSky = depth > 0.99999;
		reflected = texture(screenTexture, reflectTargetSS).xyz;
	}

	vec3 ongoing = normalize(worldPositionFromOrigin);

    // bluring for far surface
	float lodBias = 1.0 / ongoing.z;
	float dispScaleByLod = min(1.0, ongoing.z * 0.5);
    lodBias = log2(lodBias);
    lodBias = clamp(lodBias, 0.0, 2.0);

	// compute reflection color
	vec2 reflectionSS = origScrPos;
	disp.y = -abs(disp.y * 3.0);
	reflectionSS -= disp * scale * waterUBO.displaceScale * 15.0;

	vec3 refl = reflected;
#if !LINEAR_FRAMEBUFFER
	refl *= refl; // linearize
#endif

	// reflectivity
	float dotNV = dot(wave, ongoing);
	float reflective = clamp(1.0 - dotNV, 0.0, 1.0);
    float orig_reflective = reflective;
	reflective *= reflective;
	reflective *= reflective;
    reflective = mix(reflective, orig_reflective * 0.6, clamp(lodBias * 0.13 - 0.13, 0.0, 1.0));
	//reflective += 0.03;

	// reflection
#if USE_VOLUMETRIC_FOG
	// it's actually impossible for water reflection to cope with volumetric fog.
	// fade the water reflection so that we don't see sharp boundary of water
	refl *= att;
#endif
	fragColor.xyz = mix(fragColor.xyz, refl, reflective * att);

	/* ------- Specular Reflection -------- */

	// specular reflection
	if (dot(sunlight, vec3(1.0)) > 0.0001 && reflectedSky) {
		// can't use CockTorrance here -- CockTorrance's fresenel term
		// is hard-coded for higher roughness values
		vec3 lightVec = vec3(0.0, 1.0, 1.0);
		vec3 halfVec = lightVec + ongoing;
		halfVec = (dot(halfVec, halfVec) < 0.00000000001)
			? vec3(1.0, 0.0, 0.0) : normalize(halfVec);

		float dotNL = max(dot(wave, lightVec), 0.0);
		float dotNH = max(dot(wave, halfVec), 0.00001);

		// distribution
		float m = 0.001 + 0.00015 / (abs(ongoing.z) + 0.0006); // roughness
		float spec = GGXDistribution(m, dotNH);

		// fresnel
		spec *= reflective;

		// geometric shadowing (Kelemen)
		float visibility = (dotNL*dotNV) / (dotNH*dotNH);
		spec *= max(0.0, visibility);

		// limit brightness (flickering specular reflection might cause seizure to some people)
		spec = min(spec, 120.0);

		fragColor.xyz += sunlight * spec * att;
	}

#if !LINEAR_FRAMEBUFFER
	fragColor.xyz = sqrt(fragColor.xyz);
#endif

	fragColor.w = 1.0;
}
