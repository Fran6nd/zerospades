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

#pragma once

#include <string>
#include <vector>
#include <vulkan/vulkan.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {

		class VulkanShader : public RefCountedObject {
		public:
			enum Type {
				VertexShader,
				FragmentShader,
				GeometryShader,
				ComputeShader
			};

		private:
			Handle<gui::SDLVulkanDevice> device;
			VkShaderModule shaderModule;
			Type type;
			std::string name;
			std::vector<std::string> sources;
			std::vector<uint32_t> spirvCode;
			bool compiled;

			// Convert GLSL to SPIR-V
			bool CompileGLSLToSPIRV(const std::string& glslSource);

		protected:
			~VulkanShader();

		public:
			VulkanShader(Handle<gui::SDLVulkanDevice> device, Type type, const std::string& name = "");

			void AddSource(const std::string& source);
			void SetSource(const std::string& source);

			// Compile GLSL sources to SPIR-V and create shader module
			void Compile();

			// Create shader module from pre-compiled SPIR-V
			void LoadSPIRV(const std::vector<uint32_t>& spirv);

			VkShaderModule GetShaderModule() const { return shaderModule; }
			Type GetType() const { return type; }
			const std::string& GetName() const { return name; }
			bool IsCompiled() const { return compiled; }
			const std::vector<uint32_t>& GetSPIRV() const { return spirvCode; }

			// Get Vulkan shader stage flag
			VkShaderStageFlagBits GetVkStage() const;
		};

	} // namespace draw
} // namespace spades
