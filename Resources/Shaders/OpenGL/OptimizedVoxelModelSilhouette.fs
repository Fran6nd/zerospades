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

varying float fogDensity;

uniform sampler2D sceneDepthTexture;
uniform vec2 inverseScreenSize;
uniform vec3 silhouetteColor;

// Beyond this much fog the player is lost in it, so the contour is shown even
// with nothing solid in the way.
const float fogHiddenThreshold = 0.98;

void main() {
	// The model is drawn with the depth test off, so this fragment exists even
	// where the world covers it. Compare against the depth already in the scene
	// to find out which: a smaller stored depth means something solid is nearer
	// the camera than this fragment, i.e. the player is behind it.
	vec2 screenCoord = gl_FragCoord.xy * inverseScreenSize;
	float sceneDepth = texture2D(sceneDepthTexture, screenCoord).x;

	bool occluded = sceneDepth < gl_FragCoord.z;
	bool lostInFog = fogDensity >= fogHiddenThreshold;

	// In plain sight: contribute nothing, so no contour is drawn around a player
	// the viewer can already see.
	if (!occluded && !lostInFog)
		discard;

	gl_FragColor = vec4(silhouetteColor, 1.0);
}
