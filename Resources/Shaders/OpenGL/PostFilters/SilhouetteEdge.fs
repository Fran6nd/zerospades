/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

varying vec2 texCoord;

uniform sampler2D maskTexture;
uniform vec2 texelSize;
uniform float thickness;

/**
 * Draws the contour of whatever the mask pass covered.
 *
 * A pixel belongs to the contour when it is outside the mask but within
 * `thickness` texels of something inside it. Growing outwards rather than
 * eating into the shape keeps the player's own outline the size they are, and
 * keeps the line whole where the body is only a few pixels wide.
 */
void main() {
	if (texture2D(maskTexture, texCoord).w > 0.5)
		discard; // inside the shape, not on its edge

	vec3 edgeColor = vec3(0.0);
	float coverage = 0.0;

	// A square neighbourhood is enough: the mask is a filled shape, so any
	// nearby covered texel means this pixel sits on its border. The loop bound
	// is a constant because GLSL ES 1.0 requires it; `thickness` masks the
	// samples that fall outside the requested radius.
	for (int y = -3; y <= 3; y++) {
		for (int x = -3; x <= 3; x++) {
			vec2 offset = vec2(float(x), float(y));
			if (dot(offset, offset) > thickness * thickness)
				continue;

			vec4 sampled = texture2D(maskTexture, texCoord + offset * texelSize);
			if (sampled.w > 0.5) {
				edgeColor = sampled.xyz;
				coverage = 1.0;
			}
		}
	}

	if (coverage < 0.5)
		discard; // neither inside the shape nor beside it

	gl_FragColor = vec4(edgeColor, 1.0);
}
