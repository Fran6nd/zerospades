/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

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

// Lens flare visibility scanner — fragment shader.
//
// Port of Shaders/OpenGL/LensFlare/Scanner.fs.
//
// Compares scanPos.z against the offscreen depth texture, done in-shader
// rather than via a sampler2DShadow hardware compare: the depth texture
// this samples is an R32F color image (CopySceneDepthForSampling / the
// MSAA depth-resolve pass copy the D32 depth attachment into an R32F
// color target, since MoltenVK maps GLSL sampler2D to Metal's
// texture2d<float>, which cannot read a D32/depth2d<float> image — see
// VulkanFramebufferManager::sceneDepthSampleImage). Hardware compare is
// invalid on a color format regardless, so the compare must be manual.
// Softness comes from the 3x Gauss blur applied afterwards.
//
// A radial mask trims the disc inside circlePos (radius = 32 in NDC
// pixels).

#version 450

layout(binding = 0) uniform sampler2D depthTexture;

layout(location = 0) in vec3 scanPos;
layout(location = 1) in vec2 circlePos;

layout(location = 0) out vec4 outColor;

void main() {
    float depth = texture(depthTexture, scanPos.xy).x;
    float val = step(scanPos.z, depth);

    // Circle trim — matches the GL Scanner.fs `radius = 32` parameter.
    float rad = length(circlePos) * 32.0;
    rad = clamp(32.0 - 1.0 - rad, 0.0, 1.0);
    val *= rad;

    outColor = vec4(vec3(val), 1.0);
}
