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

// Light-space matrix for this cascade slice (same UBO as map shadow pass)
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 lightSpaceMatrix;
} ubo;

// Per-instance model transform + local-space pivot
layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;
    vec3 modelOrigin;
    float _pad;
} pc;

// Model vertex: uint8 x,y,z at location 0 (stride 12)
layout(location = 0) in uvec3 inPosition;

void main() {
    vec4 localPos = vec4(vec3(inPosition) + pc.modelOrigin, 1.0);
    gl_Position = ubo.lightSpaceMatrix * (pc.modelMatrix * localPos);
}
