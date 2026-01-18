/*
 Copyright (c) 2016 yvt

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

layout(set = 0, binding = 0) uniform Uniforms {
    vec2 unitShift;
    vec2 zNearFar;
    vec4 pixelShift;
    int isUpsampling;
    float _pad0;
    float _pad1;
    float _pad2;
};

layout(set = 0, binding = 1) uniform sampler2D inputTexture;
layout(set = 0, binding = 2) uniform sampler2D depthTexture;

layout(location = 0) in vec2 texCoord;

layout(location = 0) out vec4 fragColor;

float decodeDepth(float w, float near, float far) {
    return far * near / mix(far, near, w);
}

void main() {
    float centerDepthRaw = texture(depthTexture, texCoord).x;

    // A tangent at `texCoord` in `depthTexture`
    float centerDepthRawDfdx = dFdx(centerDepthRaw);
    float centerDepthRawDfdy = dFdy(centerDepthRaw);
    float centerDepthRawDfdi =
        dot(vec2(centerDepthRawDfdx, centerDepthRawDfdy), unitShift / vec2(dFdx(texCoord.x), dFdy(texCoord.y)));

    if (centerDepthRaw >= 0.999999) {
        // skip background
        fragColor = vec4(1.0);
        return;
    }

    vec2 sum = vec2(0.0000001);
    if (isUpsampling != 0) {
        vec2 inputOriginCoord = floor(texCoord * pixelShift.zw * 0.5) - 0.25;
        inputOriginCoord *= pixelShift.xy * 2.0;

        for (float i = -4.0; i <= 4.0; i += 2.0) {
            // Extrapolate the depth value using the tangent
            float centerDepthRawInterpolated = centerDepthRaw + centerDepthRawDfdi * i;
            float centerDepth = decodeDepth(centerDepthRawInterpolated, zNearFar.x, zNearFar.y);

            vec2 sampledCoord = inputOriginCoord + unitShift * i;
            float sampledDepth = texture(depthTexture, sampledCoord).x;
            sampledDepth = decodeDepth(sampledDepth, zNearFar.x, zNearFar.y);

            float depthDifference = (sampledDepth - centerDepth) * 8.0;
            float weight = exp2(-depthDifference * depthDifference - i * i * 0.2);

            float sampledValue = texture(inputTexture, sampledCoord).x;
            sampledValue = sqrt(sampledValue); // gamma correction: reduces artifacts seen on corners
            sum += vec2(sampledValue, 1.0) * weight;
        }
    } else {
        for (float i = -4.0; i <= 4.0; i += 1.0) {
            // Extrapolate the depth value using the tangent
            float centerDepthRawInterpolated = centerDepthRaw + centerDepthRawDfdi * i;
            float centerDepth = decodeDepth(centerDepthRawInterpolated, zNearFar.x, zNearFar.y);

            vec2 sampledCoord = texCoord + unitShift * i;
            float sampledDepth = texture(depthTexture, sampledCoord).x;
            sampledDepth = decodeDepth(sampledDepth, zNearFar.x, zNearFar.y);

            float depthDifference = (sampledDepth - centerDepth) * 8.0;
            float weight = exp2(-depthDifference * depthDifference - i * i * 0.2);

            float sampledValue = texture(inputTexture, sampledCoord).x;
            sampledValue = sqrt(sampledValue); // gamma correction: reduces artifacts seen on corners
            sum += vec2(sampledValue, 1.0) * weight;
        }
    }

    float weightedAverage = sum.x / sum.y;
    weightedAverage = pow(weightedAverage, 2.0);
    fragColor.xyz = vec3(weightedAverage);
    fragColor.w = 1.0;
}
