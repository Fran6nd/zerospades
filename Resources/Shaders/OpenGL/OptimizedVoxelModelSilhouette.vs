/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

uniform mat4 projectionViewModelMatrix;
uniform mat4 modelMatrix;
uniform vec3 modelOrigin;
uniform vec3 viewOriginVector;

// [x, y, z]
attribute vec3 positionAttribute;

varying float fogDensity;

vec4 ComputeFogDensity(float poweredLength);

void main() {
	vec4 vertexPos = vec4(positionAttribute + modelOrigin, 1.0);

	gl_Position = projectionViewModelMatrix * vertexPos;

	// How far into the fog this fragment sits. A player the fog has already
	// swallowed is as hidden as one behind a wall, so the mask keeps them.
	vec2 horzRelativePos = (modelMatrix * vertexPos).xy - viewOriginVector.xy;
	float horzDistance = dot(horzRelativePos, horzRelativePos);
	fogDensity = ComputeFogDensity(horzDistance).x;
}
