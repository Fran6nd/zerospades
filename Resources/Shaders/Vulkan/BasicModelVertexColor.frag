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

layout(location = 0) in vec4 color;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 customColor;
layout(location = 3) in vec3 diffuseShading;
layout(location = 4) in vec3 fogDensity;
layout(location = 5) in vec3 inFogColor;

layout(location = 0) out vec4 fragColor;

void main() {
	fragColor = color;

	// If the vertex color is very dark/black (near zero), replace with customColor
	// This allows team colors to override black voxels in player/weapon models
	if (dot(fragColor.xyz, vec3(1.0)) < 0.0001) {
		fragColor.xyz = customColor;
	}

	// Linearize color (after team color substitution, matching OpenGL)
	fragColor.xyz *= fragColor.xyz;

	// Apply lighting after linearization
	fragColor.xyz *= diffuseShading;

	// Apply fog fading
	fragColor.xyz = mix(fragColor.xyz, inFogColor, fogDensity);
}
