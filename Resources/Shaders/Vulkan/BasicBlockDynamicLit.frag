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
	float lightRadius;
	vec3 fogColor;
	float lightRadiusInversed;
	vec3 lightOrigin;
	float lightType;
	vec3 lightColor;
	float lightLinearLength;
	vec3 lightLinearDirection;
	float _pad;
	mat4 lightSpotMatrix;
} pc;

layout(location = 0) in vec4 color;
layout(location = 1) in vec3 lightPos;
layout(location = 2) in vec3 lightNormal;
layout(location = 3) in vec3 lightTexCoord;
layout(location = 4) in vec3 fogDensity;

layout(location = 0) out vec4 fragColor;

void main() {
	// Spotlight projection check
	if (pc.lightType == 2.0) {
		if (lightTexCoord.z < 0.0 ||
		    any(lessThan(lightTexCoord.xy, vec2(0.0))) ||
		    any(greaterThan(lightTexCoord.xy, vec2(lightTexCoord.z))))
			discard;
	}

	// Diffuse lighting
	float intensity = dot(normalize(lightPos), normalize(lightNormal));
	if (intensity < 0.0)
		discard;

	// Distance attenuation
	float dist = length(lightPos);
	if (dist >= pc.lightRadius)
		discard;
	float att = max(1.0 - dist * pc.lightRadiusInversed, 0.0);
	float attenuation = att * att;

	intensity *= attenuation;

	// Output: surface color * light contribution
	fragColor = vec4(color.xyz, 1.0);
	fragColor.xyz *= pc.lightColor * intensity;

	// Fog fading (fade to black, not fog color, since this is additive)
	fragColor.xyz = mix(fragColor.xyz, vec3(0.0), fogDensity);
}
