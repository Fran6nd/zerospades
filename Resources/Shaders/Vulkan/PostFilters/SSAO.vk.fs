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

// HBAO — Horizon-Based Ambient Occlusion
// Based on: Bavoil & Sainz, "Image-Space Horizon-Based AO" (SIGGRAPH 2008)
//
// For each pixel this shader:
//   1. Reconstructs view-space position and surface normal from the depth buffer.
//   2. Rotates a fixed set of NUM_DIRS screen-space directions by a per-pixel random
//      angle sampled from a tiled 4x4 noise texture to break up banding.
//   3. Marches along each direction NUM_STEPS steps, computing the elevation angle of
//      the horizon (the highest point seen so far relative to the surface plane).
//   4. Integrates the horizon angles over the upper hemisphere, weighted by the
//      projected dot product with the surface normal.
//   5. Outputs a linear occlusion factor in [0,1] (1 = fully lit, 0 = fully occluded).
//
// Descriptor bindings (set 0):
//   0 — depthTex  : depth buffer (D32/D24, sampled as FLOAT in R component)
//   1 — noiseTex  : 4x4 random rotation texture (R8_UNORM, tiled)
//
// Push constants (fragment stage):
//   zNearFar        — (near, far) clip distances
//   fieldOfView     — (tan(fovX/2), tan(fovY/2))
//   pixelShift      — (1/width, 1/height)
//   worldRadius     — AO sphere radius in world units
//   angleBias       — horizon angle bias in radians (~5-10°) to suppress self-occlusion
//   strength        — effect multiplier (>1 darkens more)
//
// Input image layout: N/A (reads depth only)
// Output: R8_UNORM single-channel occlusion factor

#version 450

layout(set = 0, binding = 0) uniform sampler2D depthTex;
layout(set = 0, binding = 1) uniform sampler2D noiseTex;

layout(push_constant) uniform PC {
    vec2  zNearFar;     // (near, far)
    vec2  fieldOfView;  // (tan(fovX/2), tan(fovY/2))
    vec2  pixelShift;   // (1/width, 1/height)
    float worldRadius;  // AO kernel world-space radius
    float angleBias;    // horizon angle bias (radians)
    float strength;     // multiplier
} pc;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out float outOcclusion;

// Hyperbolically-encoded depth → linear view-space depth (positive = away from camera)
float linearDepth(float w) {
    return pc.zNearFar.x * pc.zNearFar.y / mix(pc.zNearFar.y, pc.zNearFar.x, w);
}

// Reconstruct view-space position from UV and linear depth.
// View space: X right, Y up, Z away from camera (+Z = into scene).
vec3 viewPos(vec2 uv, float z) {
    return vec3((uv * 2.0 - 1.0) * pc.fieldOfView * z, z);
}

// Return the value with smaller absolute magnitude (for robust depth derivative).
float minabs(float a, float b) {
    return abs(a) < abs(b) ? a : b;
}

void main() {
    float depth0 = texture(depthTex, texCoord).r;

    // Skip background / sky
    if (depth0 >= 0.999999) {
        outOcclusion = 1.0;
        return;
    }

    float z0  = linearDepth(depth0);
    vec3  pos = viewPos(texCoord, z0);

    // ── View-space normal reconstructed from depth derivatives ────────────
    float dx = texelFetch(depthTex, ivec2(gl_FragCoord.xy) + ivec2(1, 0), 0).r;
    float mx = texelFetch(depthTex, ivec2(gl_FragCoord.xy) + ivec2(-1, 0), 0).r;
    float dy = texelFetch(depthTex, ivec2(gl_FragCoord.xy) + ivec2(0, 1), 0).r;
    float my = texelFetch(depthTex, ivec2(gl_FragCoord.xy) + ivec2(0, -1), 0).r;

    // Choose the shallower neighbor on each axis to avoid bleed across depth edges.
    float ddx = minabs(dx - depth0, depth0 - mx);
    float ddy = minabs(dy - depth0, depth0 - my);

    // Convert raw depth derivative to a view-space depth derivative.
    // d(linearDepth)/d(rawDepth) = near*far*(far-near) / mix(far,near,w)^2
    float invMix  = 1.0 / mix(pc.zNearFar.y, pc.zNearFar.x, depth0);
    float diffScale = pc.zNearFar.x * pc.zNearFar.y * (pc.zNearFar.y - pc.zNearFar.x) * invMix * invMix;

    vec3 tX = vec3(pc.pixelShift.x * pc.fieldOfView.x * z0, 0.0, diffScale * ddx);
    vec3 tY = vec3(0.0, pc.pixelShift.y * pc.fieldOfView.y * z0, diffScale * ddy);
    vec3 normal = normalize(cross(tX, tY));

    // ── Per-pixel random rotation from tiled 4x4 noise texture ───────────
    float noise = texture(noiseTex, gl_FragCoord.xy * 0.25).r;
    float rotAngle = noise * (2.0 * 3.14159265358979);

    // ── HBAO horizon integration ──────────────────────────────────────────
    const int NUM_DIRS  = 8;
    const int NUM_STEPS = 4;

    // Step size in UV space that corresponds to worldRadius at this depth.
    // stepRadius = worldRadius / (z * fieldOfView * 2)
    vec2 stepRadius = (pc.worldRadius / z0) / (pc.fieldOfView * 2.0) / float(NUM_STEPS);

    float occlusion = 0.0;

    for (int d = 0; d < NUM_DIRS; ++d) {
        float angle = rotAngle + float(d) * (3.14159265358979 / float(NUM_DIRS));
        vec2  dir   = vec2(cos(angle), sin(angle));
        vec2  stepUV = dir * stepRadius;

        float maxSinH = sin(pc.angleBias); // start at the bias angle

        for (int s = 1; s <= NUM_STEPS; ++s) {
            vec2  sUV = texCoord + stepUV * float(s);
            float sd  = texture(depthTex, sUV).r;
            if (sd >= 0.999999) continue;

            float sz = linearDepth(sd);
            vec3  sPos = viewPos(sUV, sz);
            vec3  hVec = sPos - pos;
            float hLen = length(hVec);
            if (hLen < 0.0001) continue;

            // sin(elevation_angle) — positive means sample is above the surface plane.
            // Since view-space Z increases away from the camera, elevation toward camera
            // has negative delta Z. We want occluders that are "in front" (higher Z).
            float sinH = (sz - z0) / hLen;

            // Distance-based falloff: occlusion from very distant samples is weighted less.
            float wsDist = hLen * z0 * max(pc.fieldOfView.x, pc.fieldOfView.y) * 2.0;
            float falloff = max(0.0, 1.0 - wsDist / pc.worldRadius);
            sinH = mix(maxSinH, sinH, falloff);

            maxSinH = max(maxSinH, sinH);
        }

        // Clamp to the biased half-space and accumulate.
        float sinBias = sin(pc.angleBias);
        float h = max(maxSinH, sinBias) - sinBias;

        // Weight by the component of the slice direction along the surface tangent plane
        // projected into screen space (dot with the surface normal's XY).
        // This reduces AO on silhouette edges where the normal nearly aligns with the view.
        float w = max(0.0, dot(dir, -normal.xy));

        occlusion += h * w;
    }

    // Normalize — the sum of weights across NUM_DIRS averages to PI/4.
    occlusion = occlusion * (2.0 / float(NUM_DIRS));
    occlusion = clamp(occlusion * pc.strength, 0.0, 1.0);

    outOcclusion = 1.0 - occlusion;
}
