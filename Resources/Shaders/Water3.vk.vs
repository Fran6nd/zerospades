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
layout(location = 4) out vec2 v_worldPositionOriginal;

// Wave texture array for displacement
layout(binding = 8) uniform sampler2DArray waveTextureArray;

// Fog computation from OpenGL Fog.vs
vec4 ComputeFogDensity(float poweredLength) {
	return vec4(min(poweredLength / (waterMat.fogDistance * waterMat.fogDistance), 1.0));
}

vec3 DisplaceWater(vec2 worldPos) {
	vec4 waveCoord = worldPos.xyxy
		* vec4(vec2(0.04), vec2(0.08704))
		+ vec4(0.0, 0.0, 0.754, 0.1315);

	vec2 waveCoord2 = worldPos.xy * 0.00844 + vec2(0.154, 0.7315);

	float wave = textureLod(waveTextureArray, vec3(waveCoord.xy, 0.0), 0.0).w;
	float disp = mix(-0.1, 0.1, wave) * 1.8;

	float wave2 = textureLod(waveTextureArray, vec3(waveCoord.zw, 1.0), 0.0).w;
	disp += mix(-0.1, 0.1, wave2) * 1.2;

	float wave3 = textureLod(waveTextureArray, vec3(waveCoord2.xy, 2.0), 0.0).w;
	disp += mix(-0.1, 0.1, wave3) * 2.5;

	vec2 waveDerivatives = textureLod(waveTextureArray, vec3(waveCoord2.xy, 2.0), 3.0).xy;
	vec2 dispHorz = (waveDerivatives - 0.5) * -2.0;

	return vec3(dispHorz, disp * 4.0);
}

void main() {
	vec4 vertexPos = vec4(positionAttribute.xy, 0.0, 1.0);

	v_worldPosition = (waterMat.modelMatrix * vertexPos).xyz;
	v_worldPositionOriginal = v_worldPosition.xy;
	v_worldPosition += DisplaceWater(v_worldPositionOriginal);
	v_viewPosition = (waterMat.viewModelMatrix * vec4(v_worldPosition, 1.0)).xyz;

	gl_Position = waterMat.projectionViewModelMatrix * vec4(v_worldPosition, 1.0);
	v_screenPosition = gl_Position.xyw;
	v_screenPosition.xy = (v_screenPosition.xy + v_screenPosition.z) * 0.5;

	vec2 horzRelativePos = v_worldPosition.xy - waterMat.viewOriginVector.xy;
	float horzDistance = dot(horzRelativePos, horzRelativePos);
	v_fogDensity = ComputeFogDensity(horzDistance).xyz;
}
