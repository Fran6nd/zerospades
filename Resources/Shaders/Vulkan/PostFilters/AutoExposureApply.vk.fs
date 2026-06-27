/*
 Copyright (c) 2015 Fran6nd

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

// Multiplies the scene colour by the exposure gain stored in the
// persistent 1×1 accumulator and writes the result LINEARLY. Unlike
// GL's GLNonlinearizeFilter, we do NOT apply pow(c, 1/gamma) here —
// the Vulkan swapchain is VK_FORMAT_B8G8R8A8_SRGB and the
// vkCmdBlitImage that hands the post-process output to the swapchain
// performs linear → sRGB encoding for us. Applying pow here would
// double-encode and blow out the midtones (see shot0003 regression).
//
// The push-constant `invGamma` is left in place but unused; it
// becomes available again if a real tonemap operator (Reinhard / ACES)
// is added later to compress HDR overshoot.

#version 450

layout(binding = 0) uniform sampler2D sceneTexture;
layout(binding = 1) uniform sampler2D gainTexture;

layout(push_constant) uniform Params {
	float invGamma; // reserved
} pc;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out vec4 outColor;

void main() {
	vec3 color = texture(sceneTexture, texCoord).rgb;
	float gain = texture(gainTexture, vec2(0.5, 0.5)).r;
	outColor = vec4(max(color * gain, 0.0), 1.0);
}
