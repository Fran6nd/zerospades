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

layout(std140, binding = 5) uniform WaterMatricesUBO {
	mat4 projectionViewModelMatrix;
	mat4 modelMatrix;
	mat4 viewModelMatrix;
	mat4 viewMatrix; // For Water3 SSR
	vec4 viewOriginVector; // use .xyz
	float fogDistance;
	vec3 _pad0;
} waterMat;

// [x, y]
layout(location = 0) in vec2 positionAttribute;

layout(location = 0) out vec3 v_fogDensity;
layout(location = 1) out vec3 v_screenPosition;
layout(location = 2) out vec3 v_viewPosition;
layout(location = 3) out vec3 v_worldPosition;

// Fog computation from OpenGL Fog.vs
vec4 ComputeFogDensity(float poweredLength) {
	return vec4(min(poweredLength / (waterMat.fogDistance * waterMat.fogDistance), 1.0));
}

void main() {
	vec4 vertexPos = vec4(positionAttribute.xy, 0.0, 1.0);

	v_worldPosition = (waterMat.modelMatrix * vertexPos).xyz;
	v_viewPosition = (waterMat.viewModelMatrix * vertexPos).xyz;

	gl_Position = waterMat.projectionViewModelMatrix * vertexPos;
	v_screenPosition = gl_Position.xyw;
	v_screenPosition.xy = (v_screenPosition.xy + v_screenPosition.z) * 0.5;

	vec2 horzRelativePos = v_worldPosition.xy - waterMat.viewOriginVector.xy;
	float horzDistance = dot(horzRelativePos, horzRelativePos);
	v_fogDensity = ComputeFogDensity(horzDistance).xyz;
}
