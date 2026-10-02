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

// Common code for dynamic light rendering


// -- shadowing

float EvaluateLighting();

float VisibilityOfLight() {
	return EvaluateLighting();
}

float EvaluateDynamicLightShadow() {
	return VisibilityOfLight();
}

// -- lighting (without bumpmapping)

// The most lights one draw takes: `GLDynamicLightShader::MaxLightsPerDraw`.
#define DYNAMIC_LIGHT_MAX 8

uniform int dynamicLightCount;
uniform vec3 dynamicLightOrigin[DYNAMIC_LIGHT_MAX];
uniform vec3 dynamicLightColor[DYNAMIC_LIGHT_MAX];
uniform float dynamicLightRadius[DYNAMIC_LIGHT_MAX];
uniform float dynamicLightRadiusInversed[DYNAMIC_LIGHT_MAX];
// Projects onto the light's image; a point or linear light's maps everything to
// its centre, so it can still be sampled
uniform mat4 dynamicLightSpotMatrix[DYNAMIC_LIGHT_MAX];
// `1` for a spotlight, whose cone and image shape the light, `0` otherwise
uniform float dynamicLightIsSpot[DYNAMIC_LIGHT_MAX];
uniform float dynamicLightIsLinear[DYNAMIC_LIGHT_MAX];
uniform vec3 dynamicLightLinearDirection[DYNAMIC_LIGHT_MAX];
uniform float dynamicLightLinearLength[DYNAMIC_LIGHT_MAX];
// Shared by every spotlight of the draw
uniform sampler2D dynamicLightProjectionTexture;

varying vec3 dynamicLightWorldPosition;
varying vec3 dynamicLightNormal;

vec3 EvaluateDynamicLight(int i, vec3 normal) {
	vec3 position = dynamicLightWorldPosition;

	// The image is sampled before anything below can return: a texture's mipmap level
	// is undefined inside control flow that differs between fragments.
	vec3 lightTexCoord = (dynamicLightSpotMatrix[i] * vec4(position, 1.0)).xyw;
	vec3 texValue = texture2DProj(dynamicLightProjectionTexture, lightTexCoord).xyz;
	texValue = mix(vec3(1.0), texValue, dynamicLightIsSpot[i]);

	vec3 lightPosition = dynamicLightOrigin[i];
	if (dynamicLightIsLinear[i] > 0.5) {
		// Linear light approximation - choose the closest point on the light
		// geometry as the representative light source
		float d = dot(position - lightPosition, dynamicLightLinearDirection[i]);
		lightPosition += dynamicLightLinearDirection[i] * clamp(d, 0.0, dynamicLightLinearLength[i]);
	}

	vec3 lightPos = lightPosition - position;

	float coneFalloff = 1.0;
	if (dynamicLightIsSpot[i] > 0.5) {
		// Nothing behind the light source
		if (lightTexCoord.z <= 0.0)
			return vec3(0.0);

		// Soft cone edge instead of a hard cut. The projected coordinates span
		// [0, 1] with the cone axis at (0.5, 0.5), so measure from there and
		// rescale to 1.0 at the cone edge.
		vec2 coneCoord = lightTexCoord.xy / lightTexCoord.z - vec2(0.5);
		float coneDistance = length(coneCoord) * 2.0;
		// Smooth falloff at cone edge (0.8 to 1.1 normalized)
		coneFalloff = smoothstep(1.1, 0.8, coneDistance);
		if (coneFalloff <= 0.0)
			return vec3(0.0);
	}

	// diffuse lighting
	float intensity = dot(normalize(lightPos), normal);
	if (intensity < 0.0)
		return vec3(0.0);

	// attenuation
	float distance = length(lightPos);
	if (distance >= dynamicLightRadius[i])
		return vec3(0.0);
	distance = max(1.0 - distance * dynamicLightRadiusInversed[i], 0.0);
	float attenuation = distance * distance;

	// apply attenuation
	intensity *= attenuation * coneFalloff;

	// TODO: specular lighting?
	return dynamicLightColor[i] * intensity * texValue;
}

vec3 EvaluateDynamicLightNoBump() {
	vec3 normal = normalize(dynamicLightNormal);

	vec3 light = vec3(0.0);
	for (int i = 0; i < DYNAMIC_LIGHT_MAX; i++) {
		if (i >= dynamicLightCount)
			break;
		light += EvaluateDynamicLight(i, normal);
	}

	return light * EvaluateDynamicLightShadow();
}

// TODO: bumpmapping variant (requires tangent vector)
