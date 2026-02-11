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

#version 450

layout(set = 0, binding = 0) uniform sampler2D mainTexture;

layout(location = 0) in vec2 texCoord;

layout(location = 0) out vec4 fragColor;

void main() {
    // linear RGB
    vec3 color = texture(mainTexture, texCoord).xyz;

    // desaturate - use max component as brightness
    float brightness = max(max(color.x, color.y), color.z);

    // remove NaN and Infinity
    if (!(brightness >= 0.0 && brightness <= 16.0))
        brightness = 0.05;

    // lower bound
    brightness = max(0.05, brightness);

    // upper bound
    brightness = min(1.3, brightness);

    // raise to the n-th power to reduce overbright
    brightness *= brightness;
    brightness *= brightness;

    fragColor = vec4(brightness, brightness, brightness, 1.0);
}
