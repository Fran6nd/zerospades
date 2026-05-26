/*
 Copyright (c) 2013 Fran6nd

 This file is part of ZeroSpades, a fork of OpenSpades.

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

// Port of OpenGL/PostFilters/ColorCorrection.fs.
//
// GL applies, in this order on a sRGB-encoded input (post-Nonlinearize):
//   1. tint multiply  (white-balance — cancels fog cast)
//   2. saturation desat (mix toward gray)
//   3. linearize (square)
//   4. ACES filmic tonemap
//   5. delinearize (sqrt)
//   6. enhancement smoothstep
//
// Vulkan input is LINEAR (no Nonlinearize stage; sRGB encoding happens at
// the swapchain blit). We sqrt the input first to match GL's "operates on
// encoded values" semantics for tint+saturation, then linearize back for
// ACES, and output linear [0,1] so the sRGB blit can do the final
// encoding for display.

#version 450

layout(binding = 0) uniform sampler2D sceneTexture;

layout(push_constant) uniform Params {
    vec4 tintAndEnhancement;  // xyz = tint, w = enhancement
    vec4 saturationPad;       // x = saturation, y/z/w = unused
} pc;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out vec4 outColor;

vec3 acesToneMapping(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec3 color = texture(sceneTexture, texCoord).rgb;
    color = max(color, 0.0);

    // Move to encoded (perceptual) space so tint and saturation match GL.
    color = sqrt(color);

    // White-balance tint (cancels the fog colour cast).
    color *= pc.tintAndEnhancement.xyz;

    // Saturation: mix toward perceptual gray.
    vec3 gray = vec3(dot(color, vec3(1.0 / 3.0)));
    float saturation = pc.saturationPad.x;
    color = mix(gray, color, saturation);

    // Back to linear for ACES tonemap.
    color = color * color;
    color = acesToneMapping(color * 0.8);

    // Enhancement smoothstep — operates on encoded values per GL.
    vec3 enc = sqrt(color);
    float enhancement = pc.tintAndEnhancement.w;
    enc = mix(enc, smoothstep(0.0, 1.0, enc), enhancement);
    color = enc * enc;

    // Output linear; sRGB swapchain blit will encode for display.
    outColor = vec4(color, 1.0);
}
