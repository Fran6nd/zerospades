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

layout(location = 0) in vec2 positionAttribute;

layout(set = 0, binding = 0) uniform Uniforms {
    vec2 zNearFar;
    vec2 pixelShift;
    vec2 fieldOfView;
    vec2 sampleOffsetScale;
    vec4 texCoordRange;
};

layout(location = 0) out vec2 texCoord;

void main() {
    vec2 pos = positionAttribute;
    vec2 scrPos = pos * 2.0 - 1.0;

    gl_Position = vec4(scrPos, 0.5, 1.0);

    texCoord = pos * texCoordRange.zw + texCoordRange.xy;
}
