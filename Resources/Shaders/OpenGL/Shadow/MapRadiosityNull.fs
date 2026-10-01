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

uniform vec3 skyLightColor;
varying float hemisphereLighting;

uniform float sunlight;
uniform float daylight;

vec3 EvaluateRadiosity(float detailAmbientOcclusion, float ssao) {
	// With the sun below the horizon the night's light falls evenly on every face, at
	// the faces' average.
	float hemisphere = sunlight > 0.0 ? hemisphereLighting : 1.0;
	return mix(skyLightColor, vec3(1.0), 0.5) *
	       (0.5 * detailAmbientOcclusion * hemisphere * ssao * daylight);
}

vec3 EvaluateSoftReflections(float detailAmbientOcclusion, vec3 direction, float ssao) {
    float facing = sunlight > 0.0 ? direction.z * -0.5 + 0.5 : 0.5;
    return skyLightColor * (facing * detailAmbientOcclusion * ssao * daylight);
}