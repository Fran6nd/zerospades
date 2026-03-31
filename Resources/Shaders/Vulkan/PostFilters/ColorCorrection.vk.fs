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

// Colour correction post-process filter.
// Port of OpenGL/PostFilters/ColorCorrection.fs.
//
// Implements:
//   - White-balance tint (derived from fog colour)
//   - Saturation control
//   - S-curve contrast enhancement
//   - ACES tone mapping + linearise/delinearise (when hdrEnabled = 1.0)
//
// Sharpening (the blurredTexture path in the GL version) is omitted here;
// it requires a separate horizontal Gauss1D pre-pass and is controlled by
// r_sharpen which defaults to 0.  Add it when r_sharpen support is wired.
//
// Descriptor bindings (set 0):
//   0 — mainTexture (combined sampler2D, SHADER_READ_ONLY_OPTIMAL)
//
// Push constants (fragment stage, 24 bytes):
//   vec3  tint        [offset  0] — white-balance tint
//   float saturation  [offset 12] — saturation multiplier
//   float enhancement [offset 16] — S-curve contrast strength
//   float hdrEnabled  [offset 20] — 1.0 → apply ACES tone mapping

#version 450

layout(binding = 0) uniform sampler2D mainTexture;

layout(push_constant) uniform PC {
    vec3  tint;
    float saturation;
    float enhancement;
    float hdrEnabled;
} pc;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out vec4 outColor;

vec3 acesToneMapping(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec3 color = texture(mainTexture, texCoord).xyz;

    // White-balance tint
    color *= pc.tint;

    // Saturation
    vec3 gray = vec3(dot(color, vec3(1.0 / 3.0)));
    color = mix(gray, color, pc.saturation);

    if (pc.hdrEnabled > 0.5) {
        // Linearise (approximate sRGB → linear with gamma-2 power law)
        color *= color;
        // ACES tone map
        color = acesToneMapping(color * 0.8);
        // Delinearise
        color = sqrt(color);
        // S-curve contrast
        color = mix(color, smoothstep(0.0, 1.0, color), pc.enhancement);
    } else {
        // S-curve contrast (SDR path)
        color = mix(color, smoothstep(0.0, 1.0, color), pc.enhancement);
    }

    outColor = vec4(color, 1.0);
}
