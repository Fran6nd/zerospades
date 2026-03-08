/*
 Copyright (c) 2021 yvt

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

// Fog2 filter fragment shader.
// Port of OpenGL/PostFilters/Fog2.fs.
// Raymarches 16 samples along the view ray to accumulate in-scattered
// sunlight and ambient light from the shadow map.
//
// Differences from the GL version:
//   - No separate dither/noise textures; per-frame noise is generated
//     analytically from gl_FragCoord and pc.ditherFrame.
//   - No ambient shadow (sampler3D) or radiosity (sampler3D) textures —
//     ambient term is approximated as full coverage (ambientFactor = weight
//     at every step) until those renderers are added to the Vulkan back-end.
//
// Descriptor set layout (set 0):
//   binding 0 — colorTexture    (sampler2D)
//   binding 1 — depthTexture    (sampler2D, depth aspect, SHADER_READ_ONLY)
//   binding 2 — shadowMapTexture (sampler2D)

#version 450

layout(binding = 0) uniform sampler2D colorTexture;
layout(binding = 1) uniform sampler2D depthTexture;
layout(binding = 2) uniform sampler2D shadowMapTexture;

layout(push_constant) uniform Params {
    mat4 viewProjectionMatrixInv; // [0..63]  UV → view-centric world
    vec4 viewOriginFogDist;       // [64..79] xyz=viewOrigin, w=fogDistance
    vec4 sunlightScale;           // [80..95] xyz
    vec4 ambientScale;            // [96..111] xyz
    vec4 ditherFrame;             // [112..127] xy=per-frame noise seed
} pc;

layout(location = 0) in  vec2 texCoord;
layout(location = 1) in  vec4 viewcentricWorldPositionPartial;
layout(location = 0) out vec4 outColor;

vec3 transformToShadow(vec3 v) {
    v.y -= v.z;
    v *= vec3(1.0 / 512.0, 1.0 / 512.0, 1.0 / 255.0);
    return v;
}

void main() {
    vec3  viewOrigin  = pc.viewOriginFogDist.xyz;
    float fogDistance = pc.viewOriginFogDist.w;

    // Reconstruct view-centric world position from hardware depth.
    float localClipZ = texture(depthTexture, texCoord).r;
    vec4  worldPos   = viewcentricWorldPositionPartial
                     + pc.viewProjectionMatrixInv * vec4(0.0, 0.0, localClipZ, 0.0);
    worldPos.xyz /= worldPos.w;

    // Clip the ray to fogDistance (VOXLAP cylindrical fog model).
    float voxlapDistanceSq = dot(worldPos.xy, worldPos.xy);
    worldPos /= max(sqrt(voxlapDistanceSq) / fogDistance, 1.0);
    voxlapDistanceSq = min(voxlapDistanceSq, fogDistance * fogDistance);

    float goalFogFactor = min(voxlapDistanceSq / (fogDistance * fogDistance), 1.0);
    // Sky pixels get full fog.
    if (localClipZ >= (1.0 - 1e-6))
        goalFogFactor = 1.0;

    // 16-sample raymarching.
    const int numSamples = 16;
    float weightDelta = sqrt(voxlapDistanceSq) / fogDistance / float(numSamples);
    float weightSum   = 0.0;

    // Analytical per-pixel temporal noise (replaces dither + noise textures).
    vec2  seed   = gl_FragCoord.xy + pc.ditherFrame.xy * vec2(127.0, 113.0);
    float dither = fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);

    float weight = 1.0 - weightDelta * dither;

    vec3 currentShadowPos = transformToShadow(viewOrigin);
    vec3 shadowDelta      = transformToShadow(worldPos.xyz / float(numSamples));
    currentShadowPos     += shadowDelta * dither;

    float sunlightFactor = 0.0;
    float ambientFactor  = 0.0;

    for (int i = 0; i < numSamples; ++i) {
        // Shadow map: .w channel = normalised height of highest opaque voxel.
        float shadowHeight = texture(shadowMapTexture, currentShadowPos.xy).w;
        float lit          = step(currentShadowPos.z, shadowHeight);

        sunlightFactor += lit    * weight;
        ambientFactor  += weight;  // full ambient (no ambientShadowTexture yet)

        currentShadowPos += shadowDelta;
        weightSum        += weight;
        weight           -= weightDelta;
    }

    // Rescale to the desired fog density.
    vec3 scale         = vec3(goalFogFactor) / (weightSum + 1.0e-4);
    vec3 sunlightContrib = sunlightFactor * pc.sunlightScale.xyz * scale;
    vec3 ambientContrib  = ambientFactor  * pc.ambientScale.xyz  * scale;

    // Directional brightness gradient (sun at (0, -1, -1)).
    vec3  sunDir = normalize(vec3(0.0, -1.0, -1.0));
    float bright = dot(sunDir, normalize(worldPos.xyz));
    sunlightContrib *= bright * 0.5 + 1.0;
    ambientContrib  *= bright * 0.5 + 1.0;

    outColor      = texture(colorTexture, texCoord);
    outColor.xyz += sunlightContrib + ambientContrib;
}
