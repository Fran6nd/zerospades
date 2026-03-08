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

// Bloom final composite pass.
// Mirrors GLBloomFilter's GammaMix call with mix1=0.8, mix2=0.2 on a linear HDR framebuffer:
//   outColor = scene * 0.8 + bloom * 0.2

layout(binding = 0) uniform sampler2D sceneTexture;
layout(binding = 1) uniform sampler2D bloomTexture;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 scene = texture(sceneTexture, texCoord).rgb;
    vec3 bloom = texture(bloomTexture, texCoord).rgb;
    outColor = vec4(scene * 0.8 + bloom * 0.2, 1.0);
}
