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

// This shader computes the gain of the auto exposure.

#version 450

layout(set = 0, binding = 0) uniform Uniforms {
    float minGain;
    float maxGain;
    float blendRate;
    float _pad0;
};

layout(set = 0, binding = 1) uniform sampler2D mainTexture;

layout(location = 0) in vec2 texCoord;

layout(location = 0) out vec4 fragColor;

void main() {
    float brightness = texture(mainTexture, vec2(0.5, 0.5)).x;

    // reverse "raise to the n-th power"
    brightness = sqrt(brightness);
    brightness = sqrt(brightness);

    // compute gain
    float gain = clamp(0.6 / brightness, minGain, maxGain);

    fragColor.xyz = vec3(gain);
    fragColor.w = blendRate;
}
