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

#version 450

layout(set = 0, binding = 0) uniform Uniforms {
    vec2 zNearFar;
    vec2 pixelShift;
    float depthScale;
    float maxVignetteBlur;
    vec2 vignetteScale;
    float globalBlur;
    float nearBlur;
    float farBlur;
    float _pad0;
};

layout(set = 0, binding = 1) uniform sampler2D depthTexture;

layout(location = 0) in vec2 texCoord;

layout(location = 0) out vec4 fragColor;

float decodeDepth(float w, float near, float far) {
    return far * near / mix(far, near, w);
}

float depthAt(vec2 pt) {
    float w = texture(depthTexture, pt).x;
    return decodeDepth(w, zNearFar.x, zNearFar.y);
}

float CoCAt(vec2 pt) {
    float depth = depthAt(pt);
    float blur = 1.0 - depth * depthScale;
    return blur * (blur > 0.0 ? nearBlur : farBlur);
}

void main() {
    float val = 0.0;

    val += CoCAt(texCoord);
    val += CoCAt(texCoord + pixelShift * vec2(1.0, 0.0));
    val += CoCAt(texCoord + pixelShift * vec2(2.0, 0.0));
    val += CoCAt(texCoord + pixelShift * vec2(3.0, 0.0));
    val += CoCAt(texCoord + pixelShift * vec2(0.0, 1.0));
    val += CoCAt(texCoord + pixelShift * vec2(1.0, 1.0));
    val += CoCAt(texCoord + pixelShift * vec2(2.0, 1.0));
    val += CoCAt(texCoord + pixelShift * vec2(3.0, 1.0));
    val += CoCAt(texCoord + pixelShift * vec2(0.0, 2.0));
    val += CoCAt(texCoord + pixelShift * vec2(1.0, 2.0));
    val += CoCAt(texCoord + pixelShift * vec2(2.0, 2.0));
    val += CoCAt(texCoord + pixelShift * vec2(3.0, 2.0));
    val += CoCAt(texCoord + pixelShift * vec2(0.0, 3.0));
    val += CoCAt(texCoord + pixelShift * vec2(1.0, 3.0));
    val += CoCAt(texCoord + pixelShift * vec2(2.0, 3.0));
    val += CoCAt(texCoord + pixelShift * vec2(3.0, 3.0));

    fragColor.x = val * (1.0 / 16.0);

    float sq = length((texCoord - 0.5) * vignetteScale);
    float sq2 = sq * sq * maxVignetteBlur;
    fragColor.x += sq2;

    // don't blur the center
    float scl = min(1.0, sq * 10.0);
    fragColor.x *= scl;

    fragColor.x = min(fragColor.x + globalBlur, 1.0);
    fragColor.yzw = vec3(0.0);
}
