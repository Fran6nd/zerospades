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

// SSAO composite pass — multiplies the ambient occlusion factor onto the scene colour.
//
// The occlusion factor is in [0,1]: 0 = fully occluded, 1 = no occlusion.
// To preserve HDR highlights and avoid darkening bright emissive surfaces
// disproportionately, the AO is lifted slightly in high-luminance regions.
//
// Descriptor bindings (set 0):
//   0 — colorTex : scene colour image (may be HDR)
//   1 — ssaoTex  : R8_UNORM filtered occlusion factor

#version 450

layout(set = 0, binding = 0) uniform sampler2D colorTex;
layout(set = 0, binding = 1) uniform sampler2D ssaoTex;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec4  color = texture(colorTex, texCoord);
    float ao    = texture(ssaoTex,  texCoord).r;

    // Reduce AO contribution in bright/emissive regions.
    float luma = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
    ao = mix(ao, 1.0, clamp(luma * 0.4, 0.0, 1.0));

    outColor = vec4(color.rgb * ao, color.a);
}
