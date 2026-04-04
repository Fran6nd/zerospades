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

// Default values for all Vulkan-specific cvars.
// Mirrors the GL renderer's GLSettings.cpp convention.

#include <Core/Settings.h>

namespace spades {

DEFINE_SPADES_SETTING(r_vk_bloom,                "1");
DEFINE_SPADES_SETTING(r_vk_colorCorrection,      "1");
DEFINE_SPADES_SETTING(r_vk_depthOfField,         "0");
DEFINE_SPADES_SETTING(r_vk_depthOfFieldMaxCoc,   "0.01");
DEFINE_SPADES_SETTING(r_vk_dlights,              "1");
DEFINE_SPADES_SETTING(r_vk_exposureValue,        "0");
DEFINE_SPADES_SETTING(r_vk_fogShadow,            "0");
DEFINE_SPADES_SETTING(r_vk_fxaa,                 "1");
DEFINE_SPADES_SETTING(r_vk_hdr,                  "0");
DEFINE_SPADES_SETTING(r_vk_hdrAutoExposureMax,   "0.5");
DEFINE_SPADES_SETTING(r_vk_hdrAutoExposureMin,   "-1.5");
DEFINE_SPADES_SETTING(r_vk_hdrAutoExposureSpeed, "1");
DEFINE_SPADES_SETTING(r_vk_highPrec,             "1");
DEFINE_SPADES_SETTING(r_vk_multisamples,         "0");
DEFINE_SPADES_SETTING(r_vk_outlines,             "0");
DEFINE_SPADES_SETTING(r_vk_physicalLighting,     "0");
DEFINE_SPADES_SETTING(r_vk_saturation,           "1");
DEFINE_SPADES_SETTING(r_vk_shadowMapSize,        "2048");
DEFINE_SPADES_SETTING(r_vk_softParticles,        "1");
DEFINE_SPADES_SETTING(r_vk_srgb,                 "0");
DEFINE_SPADES_SETTING(r_vk_ssao,                 "0");

} // namespace spades
