/*
 Copyright (c) 2015 yvt

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

// This shader applies exposure gain to the scene.

#version 450

layout(set = 0, binding = 0) uniform sampler2D mainTexture;
layout(set = 0, binding = 1) uniform sampler2D exposureTexture;

layout(location = 0) in vec2 texCoord;

layout(location = 0) out vec4 fragColor;

void main() {
    vec4 color = texture(mainTexture, texCoord);
    float gain = texture(exposureTexture, vec2(0.5, 0.5)).x;

    fragColor = vec4(color.rgb * gain, color.a);
}
