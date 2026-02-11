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
    vec2 offset;
    vec2 _pad0;
};

layout(set = 0, binding = 1) uniform sampler2D mainTexture;
layout(set = 0, binding = 2) uniform sampler2D cocTexture;

layout(location = 0) in vec2 texCoord;

layout(location = 0) out vec4 fragColor;

vec4 doGamma(vec4 col) {
    col.xyz *= col.xyz;
    return col;
}

void main() {
    float coc = texture(cocTexture, texCoord).x;
    vec4 v = vec4(0.0);

    vec4 offsets = vec4(0.0, 0.25, 0.5, 0.75) * coc;
    vec4 offsets2 = offsets + coc * 0.125;

    v += doGamma(texture(mainTexture, texCoord));
    v += doGamma(texture(mainTexture, texCoord + offset * offsets.y));
    v += doGamma(texture(mainTexture, texCoord + offset * offsets.z));
    v += doGamma(texture(mainTexture, texCoord + offset * offsets.w));
    v += doGamma(texture(mainTexture, texCoord + offset * offsets2.x));
    v += doGamma(texture(mainTexture, texCoord + offset * offsets2.y));
    v += doGamma(texture(mainTexture, texCoord + offset * offsets2.z));
    v += doGamma(texture(mainTexture, texCoord + offset * offsets2.w));
    v *= 0.125;

    v.xyz = sqrt(v.xyz);

    fragColor = v;
}
