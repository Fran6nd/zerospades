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

layout(set = 0, binding = 0) uniform Uniforms {
    vec3 mix1;
    float _pad0;
    vec3 mix2;
    float _pad1;
};

layout(set = 0, binding = 1) uniform sampler2D texture1;
layout(set = 0, binding = 2) uniform sampler2D texture2;

layout(location = 0) in vec2 texCoord;

layout(location = 0) out vec4 fragColor;

void main() {
    vec3 color1 = texture(texture1, texCoord).xyz;
    vec3 color2 = texture(texture2, texCoord).xyz;

    // Gamma-correct mixing (square colors, mix, then sqrt)
    vec3 color = color1 * color1 * mix1;
    color += color2 * color2 * mix2;
    color = sqrt(color);

    fragColor = vec4(color, 1.0);
}
