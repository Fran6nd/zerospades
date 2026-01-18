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

layout(location = 0) in vec2 positionAttribute;

layout(location = 0) out vec2 texCoord;
layout(location = 1) out vec2 modulationTexCoord;

layout(binding = 0) uniform DrawUniforms {
	vec4 drawRange;
	vec3 color;
};

void main() {
	gl_Position.xy = mix(drawRange.xy, drawRange.zw, positionAttribute.xy);
	gl_Position.z = 0.5;
	gl_Position.w = 1.0;

	texCoord = mix(vec2(0.0), vec2(1.0), positionAttribute.xy);
	modulationTexCoord = gl_Position.xy * 0.5 + vec2(0.5, 0.5);
}
