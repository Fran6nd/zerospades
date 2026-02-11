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
    int blurredOnly;
    float _pad0;
    float _pad1;
    float _pad2;
};

layout(set = 0, binding = 1) uniform sampler2D mainTexture;
layout(set = 0, binding = 2) uniform sampler2D blurTexture1;
layout(set = 0, binding = 3) uniform sampler2D blurTexture2;
layout(set = 0, binding = 4) uniform sampler2D cocTexture;

layout(location = 0) in vec2 texCoord;

layout(location = 0) out vec4 fragColor;

vec4 doGamma(vec4 col) {
    col.xyz *= col.xyz;
    return col;
}

void main() {
    float coc = texture(cocTexture, texCoord).x;

    vec4 a = doGamma(texture(mainTexture, texCoord));
    vec4 b = doGamma(texture(blurTexture1, texCoord));
    b += doGamma(texture(blurTexture2, texCoord)) * 2.0;
    b *= (1.0 / 3.0);

    float per = min(1.0, coc * 5.0);
    vec4 v = (blurredOnly != 0) ? b : mix(a, b, per);

    v.xyz = sqrt(v.xyz);

    fragColor = v;
}
