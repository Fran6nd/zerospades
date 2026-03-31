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

// Depth-aware bilateral blur for SSAO denoising.
//
// Performs a separable 9-tap weighted blur.  Sample weights fall off both
// spatially (Gaussian in i) and depth-wise (exp2(-depthDiff^2 * depthSigma^2))
// so that samples across depth discontinuities receive near-zero weight.
// Run twice: once horizontal (unitShift = (1/w, 0)) and once vertical
// (unitShift = (0, 1/h)).
//
// Descriptor bindings (set 0):
//   0 — ssaoTex  : R8_UNORM single-channel raw SSAO image
//   1 — depthTex : depth buffer (same resolution)
//
// Push constants:
//   unitShift  — per-tap offset in UV space for one texel along the blur axis
//   zNearFar   — (near, far) for depth linearization

#version 450

layout(set = 0, binding = 0) uniform sampler2D ssaoTex;
layout(set = 0, binding = 1) uniform sampler2D depthTex;

layout(push_constant) uniform PC {
    vec2 unitShift;  // (1/width, 0) or (0, 1/height)
    vec2 zNearFar;   // (near, far)
} pc;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out float outSSAO;

float linearDepth(float w) {
    return pc.zNearFar.x * pc.zNearFar.y / mix(pc.zNearFar.y, pc.zNearFar.x, w);
}

void main() {
    float centerRaw = texture(depthTex, texCoord).r;
    if (centerRaw >= 0.999999) {
        outSSAO = 1.0;
        return;
    }
    float centerDepth = linearDepth(centerRaw);

    // Estimate how fast depth changes along the blur axis so we can extrapolate
    // the expected depth at each tap position (reduces edge softening).
    float centerRawDfdi;
    {
        vec2 du = dFdx(texCoord);
        vec2 dv = dFdy(texCoord);
        float dRawDx = dFdx(centerRaw);
        float dRawDy = dFdy(centerRaw);
        // Project gradient onto the blur axis direction.
        // unitShift is proportional to the axis direction in UV space.
        vec2 axis = pc.unitShift;
        vec2 uvPerPixel = vec2(length(du), length(dv)); // ~= pixelShift
        // d(raw)/d(i) where i is the tap index integer
        centerRawDfdi = dot(vec2(dRawDx, dRawDy) / uvPerPixel, axis * uvPerPixel);
        // Simpler: just use the derivative directly in UV direction
        centerRawDfdi = dRawDx * pc.unitShift.x + dRawDy * pc.unitShift.y;
        // unitShift is already one-texel, so this gives d(raw)/d(one tap)
    }

    vec2 sum = vec2(1e-7);

    for (float i = -4.0; i <= 4.0; i += 1.0) {
        vec2 sUV = texCoord + pc.unitShift * i;

        // Extrapolate expected center depth at this tap position.
        float expectedRaw   = centerRaw + centerRawDfdi * i;
        float expectedDepth = linearDepth(expectedRaw);

        float sampledRaw   = texture(depthTex, sUV).r;
        float sampledDepth = linearDepth(sampledRaw);

        float depthDiff = (sampledDepth - expectedDepth) * 8.0;
        float weight    = exp2(-depthDiff * depthDiff - i * i * 0.2);

        // Apply sqrt before averaging to reduce corner darkening (undone on exit).
        float v = texture(ssaoTex, sUV).r;
        v = sqrt(v);
        sum += vec2(v, 1.0) * weight;
    }

    float result = sum.x / sum.y;
    result = result * result; // undo sqrt
    outSSAO = result;
}
