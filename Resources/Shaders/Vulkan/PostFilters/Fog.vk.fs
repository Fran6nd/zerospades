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

// Fog filter (Fog1 style) fragment shader.
// Port of OpenGL/PostFilters/Fog.fs.
// Uses 32 fine-level shadow-map steps (the GL version used up to 64×64
// coarse+fine steps, which is impractical on GPU without the coarse map).
// Depth is reconstructed from the hardware depth buffer.
//
// Descriptor set layout (set 0):
//   binding 0 — colorTexture   (sampler2D)
//   binding 1 — depthTexture   (sampler2D, depth aspect)
//   binding 2 — shadowMapTexture (sampler2D)

#version 450

layout(binding = 0) uniform sampler2D colorTexture;
layout(binding = 1) uniform sampler2D depthTexture;
layout(binding = 2) uniform sampler2D shadowMapTexture;

layout(push_constant) uniform Params {
    vec4 viewOriginPad;  // xyz = viewOrigin
    vec4 viewAxisUp;     // xyz
    vec4 viewAxisSide;   // xyz
    vec4 viewAxisFront;  // xyz
    vec4 fovZNearFar;    // xy = fov (tan half-angle), z = zNear, w = zFar
    vec4 fogColorDist;   // xyz = fogColor (linear), w = fogDistance
} pc;

layout(location = 0) in  vec2 texCoord;
layout(location = 1) in  vec3 viewTan;
layout(location = 2) in  vec3 viewDir;
layout(location = 3) in  vec3 shadowOrigin;
layout(location = 4) in  vec3 shadowRayDirection;
layout(location = 0) out vec4 outColor;

float decodeDepth(float w, float near, float far) {
    return far * near / mix(far, near, w);
}

float fogDensFunc(float t) {
    return t;
}

void main() {
    float fogDistance = pc.fogColorDist.w;
    float zNear       = pc.fovZNearFar.z;
    float zFar        = pc.fovZNearFar.w;

    float voxelDistanceFactor = length(shadowRayDirection) / length(viewTan);
    voxelDistanceFactor *= length(viewDir.xy) / length(viewDir); // remove vertical fog

    float w             = texture(depthTexture, texCoord).r;
    float screenDepth   = decodeDepth(w, zNear, zFar);
    float screenDistance = screenDepth * length(viewTan);
    screenDistance = min(screenDistance, fogDistance);
    float screenVoxelDistance = screenDistance * voxelDistanceFactor;

    float fogDistanceTime = fogDistance * voxelDistanceFactor;
    float zMaxTime        = min(screenVoxelDistance, fogDistanceTime);
    float maxTime         = zMaxTime;
    float ceilTime        = 10000000000.0;

    vec3 startPos = shadowOrigin;
    vec3 dir      = shadowRayDirection;
    if (length(dir.xy) < 0.0001) dir.xy = vec2(0.0001);
    if (dir.x == 0.0) dir.x = 0.00001;
    if (dir.y == 0.0) dir.y = 0.00001;
    dir = normalize(dir);

    if (dir.z < -0.000001) {
        ceilTime = -startPos.z / dir.z - 0.0001;
        maxTime  = min(maxTime, ceilTime);
    }

    float time  = 0.0;
    float total = 0.0;

    if (startPos.z > (63.0 / 255.0) && dir.z < 0.0) {
        time   = ((63.0 / 255.0) - startPos.z) / dir.z;
        total += fogDensFunc(time);
        startPos += time * dir;
    }

    const vec2 voxels     = vec2(512.0);
    const vec2 voxelSize  = 1.0 / voxels;
    const int  maxSteps   = 32;

    vec3  pos             = startPos + dir * 0.0001;
    vec2  voxelIndex      = floor(pos.xy);
    if (pos.xy == voxelIndex) pos += 0.001;

    vec2 dirSign              = sign(dir.xy);
    vec2 dirSign2             = dirSign * 0.5 + 0.5; // 0 or 1
    vec2 timePerVoxel         = 1.0 / dir.xy;
    vec2 timePerVoxelAbs      = abs(timePerVoxel);
    vec2 timeToNextVoxel      = (voxelIndex + dirSign2 - pos.xy) * timePerVoxel;

    if (ceilTime <= 0.0) {
        total = fogDensFunc(zMaxTime);
    } else {
        for (int i = 0; i < maxSteps; ++i) {
            float val      = texture(shadowMapTexture, voxelIndex * voxelSize).w;
            val            = step(pos.z, val);
            float diffTime = min(timeToNextVoxel.x, timeToNextVoxel.y);

            if (timeToNextVoxel.x < timeToNextVoxel.y) {
                voxelIndex.x         += dirSign.x;
                timeToNextVoxel.y    -= diffTime;
                timeToNextVoxel.x     = timePerVoxelAbs.x;
            } else {
                voxelIndex.y         += dirSign.y;
                timeToNextVoxel.x    -= diffTime;
                timeToNextVoxel.y     = timePerVoxelAbs.y;
            }

            pos += dir * diffTime;
            float nextTime  = min(time + diffTime, maxTime);
            float diffDens  = fogDensFunc(nextTime) - fogDensFunc(time);
            diffTime        = nextTime - time;
            time            = nextTime;

            total += val * diffDens;

            if (diffTime <= 0.0) {
                if (nextTime >= ceilTime) {
                    diffDens = fogDensFunc(zMaxTime) - fogDensFunc(time);
                    total   += val * max(diffDens, 0.0);
                }
                break;
            }
        }
    }

    total = mix(total, fogDensFunc(zMaxTime), 0.04);
    total /= fogDensFunc(fogDistanceTime + 1e-10);

    vec3 sunDir = normalize(vec3(0.0, -1.0, -1.0));
    float bright = dot(sunDir, normalize(viewDir));
    total *= 0.8 + bright * 0.3;
    bright = exp2(bright * 16.0 - 15.0);
    total *= bright + 1.0;

    outColor      = texture(colorTexture, texCoord);
    outColor.xyz += total * pc.fogColorDist.xyz;
}
