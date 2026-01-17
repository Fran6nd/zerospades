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
	vec3 fogColor;
	float _pad0;
	vec3 viewAxisFront;
	float _pad1;
	vec3 viewAxisUp;
	float _pad2;
	vec3 viewAxisSide;
	float _pad3;
	float fovX;
	float fovY;
} pushConstants;

layout(location = 0) in vec3 viewDir;

layout(location = 0) out vec4 fragColor;

void main() {
	// Sun direction matches OpenGL: (0, -1, -1) normalized
	vec3 sunDir = normalize(vec3(0.0, -1.0, -1.0));

	// Compute brightness based on view direction dot product with sun direction
	float bright = dot(sunDir, normalize(viewDir));

	// Apply gradient similar to OpenGL fog shader
	// Base brightness adjustment
	float gradientFactor = 0.8 + bright * 0.3;

	// Sun glow effect (exponential falloff)
	float sunGlow = exp2(bright * 16.0 - 15.0);
	gradientFactor *= (sunGlow + 1.0);

	// Apply to fog color
	vec3 skyColor = pushConstants.fogColor * gradientFactor;

	fragColor = vec4(skyColor, 1.0);
}
