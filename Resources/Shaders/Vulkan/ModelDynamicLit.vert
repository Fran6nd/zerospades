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
	mat4 projectionViewModelMatrix;
	mat4 modelMatrix;
	vec3 modelOrigin;
	float fogDensity;
	vec3 customColor;
	float lightRadius;
	vec3 lightOrigin;
	float lightType; // 0=point, 1=linear, 2=spotlight
	vec3 lightColor;
	float lightRadiusInversed;
	vec3 lightLinearDirection;
	float lightLinearLength;
} pc;

layout(location = 0) in uvec3 positionAttribute;
layout(location = 1) in uvec3 colorAttribute;
layout(location = 2) in ivec3 normalAttribute;

layout(location = 0) out vec4 color;
layout(location = 1) out vec3 lightPos;
layout(location = 2) out vec3 lightNormal;
layout(location = 3) out float fogDensityOut;

void main() {
	vec3 position = vec3(positionAttribute);
	vec4 localPos = vec4(position + pc.modelOrigin, 1.0);
	gl_Position = pc.projectionViewModelMatrix * localPos;

	// Compute world position for light calculation
	vec3 worldPos = (pc.modelMatrix * localPos).xyz;

	// World-space normal
	vec3 normal = normalize(mat3(pc.modelMatrix) * normalize(vec3(normalAttribute)));
	lightNormal = normal;

	// Vertex color
	vec3 vertexColor = vec3(colorAttribute) / 255.0;
	// Replace black voxels with custom color (team color)
	if (dot(vertexColor, vec3(1.0)) < 0.0001)
		vertexColor = pc.customColor;
	color = vec4(vertexColor, 1.0);

	// Light position computation
	vec3 lightPosition = pc.lightOrigin;
	if (pc.lightType == 1.0) {
		// Linear light: closest point on line segment
		float d = dot(worldPos - pc.lightOrigin, pc.lightLinearDirection);
		d = clamp(d, 0.0, pc.lightLinearLength);
		lightPosition += pc.lightLinearDirection * d;
	}
	lightPos = lightPosition - worldPos;

	// Fog density (precomputed on CPU)
	fogDensityOut = pc.fogDensity;
}
