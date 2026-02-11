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

#include "VulkanShader.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Core/Exception.h>

#ifdef HAVE_GLSLANG
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#endif

namespace spades {
	namespace draw {

#ifdef HAVE_GLSLANG
		static bool glslangInitialized = false;

		static void InitializeGlslang() {
			if (!glslangInitialized) {
				glslang::InitializeProcess();
				glslangInitialized = true;
			}
		}

		static EShLanguage GetGlslangShaderType(VulkanShader::Type type) {
			switch (type) {
				case VulkanShader::VertexShader: return EShLangVertex;
				case VulkanShader::FragmentShader: return EShLangFragment;
				case VulkanShader::GeometryShader: return EShLangGeometry;
				case VulkanShader::ComputeShader: return EShLangCompute;
				default: return EShLangVertex;
			}
		}

		static const TBuiltInResource DefaultTBuiltInResource = {
			/* .MaxLights = */ 32,
			/* .MaxClipPlanes = */ 6,
			/* .MaxTextureUnits = */ 32,
			/* .MaxTextureCoords = */ 32,
			/* .MaxVertexAttribs = */ 64,
			/* .MaxVertexUniformComponents = */ 4096,
			/* .MaxVaryingFloats = */ 64,
			/* .MaxVertexTextureImageUnits = */ 32,
			/* .MaxCombinedTextureImageUnits = */ 80,
			/* .MaxTextureImageUnits = */ 32,
			/* .MaxFragmentUniformComponents = */ 4096,
			/* .MaxDrawBuffers = */ 32,
			/* .MaxVertexUniformVectors = */ 128,
			/* .MaxVaryingVectors = */ 8,
			/* .MaxFragmentUniformVectors = */ 16,
			/* .MaxVertexOutputVectors = */ 16,
			/* .MaxFragmentInputVectors = */ 15,
			/* .MinProgramTexelOffset = */ -8,
			/* .MaxProgramTexelOffset = */ 7,
			/* .MaxClipDistances = */ 8,
			/* .MaxComputeWorkGroupCountX = */ 65535,
			/* .MaxComputeWorkGroupCountY = */ 65535,
			/* .MaxComputeWorkGroupCountZ = */ 65535,
			/* .MaxComputeWorkGroupSizeX = */ 1024,
			/* .MaxComputeWorkGroupSizeY = */ 1024,
			/* .MaxComputeWorkGroupSizeZ = */ 64,
			/* .MaxComputeUniformComponents = */ 1024,
			/* .MaxComputeTextureImageUnits = */ 16,
			/* .MaxComputeImageUniforms = */ 8,
			/* .MaxComputeAtomicCounters = */ 8,
			/* .MaxComputeAtomicCounterBuffers = */ 1,
			/* .MaxVaryingComponents = */ 60,
			/* .MaxVertexOutputComponents = */ 64,
			/* .MaxGeometryInputComponents = */ 64,
			/* .MaxGeometryOutputComponents = */ 128,
			/* .MaxFragmentInputComponents = */ 128,
			/* .MaxImageUnits = */ 8,
			/* .MaxCombinedImageUnitsAndFragmentOutputs = */ 8,
			/* .MaxCombinedShaderOutputResources = */ 8,
			/* .MaxImageSamples = */ 0,
			/* .MaxVertexImageUniforms = */ 0,
			/* .MaxTessControlImageUniforms = */ 0,
			/* .MaxTessEvaluationImageUniforms = */ 0,
			/* .MaxGeometryImageUniforms = */ 0,
			/* .MaxFragmentImageUniforms = */ 8,
			/* .MaxCombinedImageUniforms = */ 8,
			/* .MaxGeometryTextureImageUnits = */ 16,
			/* .MaxGeometryOutputVertices = */ 256,
			/* .MaxGeometryTotalOutputComponents = */ 1024,
			/* .MaxGeometryUniformComponents = */ 1024,
			/* .MaxGeometryVaryingComponents = */ 64,
			/* .MaxTessControlInputComponents = */ 128,
			/* .MaxTessControlOutputComponents = */ 128,
			/* .MaxTessControlTextureImageUnits = */ 16,
			/* .MaxTessControlUniformComponents = */ 1024,
			/* .MaxTessControlTotalOutputComponents = */ 4096,
			/* .MaxTessEvaluationInputComponents = */ 128,
			/* .MaxTessEvaluationOutputComponents = */ 128,
			/* .MaxTessEvaluationTextureImageUnits = */ 16,
			/* .MaxTessEvaluationUniformComponents = */ 1024,
			/* .MaxTessPatchComponents = */ 120,
			/* .MaxPatchVertices = */ 32,
			/* .MaxTessGenLevel = */ 64,
			/* .MaxViewports = */ 16,
			/* .MaxVertexAtomicCounters = */ 0,
			/* .MaxTessControlAtomicCounters = */ 0,
			/* .MaxTessEvaluationAtomicCounters = */ 0,
			/* .MaxGeometryAtomicCounters = */ 0,
			/* .MaxFragmentAtomicCounters = */ 8,
			/* .MaxCombinedAtomicCounters = */ 8,
			/* .MaxAtomicCounterBindings = */ 1,
			/* .MaxVertexAtomicCounterBuffers = */ 0,
			/* .MaxTessControlAtomicCounterBuffers = */ 0,
			/* .MaxTessEvaluationAtomicCounterBuffers = */ 0,
			/* .MaxGeometryAtomicCounterBuffers = */ 0,
			/* .MaxFragmentAtomicCounterBuffers = */ 1,
			/* .MaxCombinedAtomicCounterBuffers = */ 1,
			/* .MaxAtomicCounterBufferSize = */ 16384,
			/* .MaxTransformFeedbackBuffers = */ 4,
			/* .MaxTransformFeedbackInterleavedComponents = */ 64,
			/* .MaxCullDistances = */ 8,
			/* .MaxCombinedClipAndCullDistances = */ 8,
			/* .MaxSamples = */ 4,
			/* .maxMeshOutputVerticesNV = */ 256,
			/* .maxMeshOutputPrimitivesNV = */ 512,
			/* .maxMeshWorkGroupSizeX_NV = */ 32,
			/* .maxMeshWorkGroupSizeY_NV = */ 1,
			/* .maxMeshWorkGroupSizeZ_NV = */ 1,
			/* .maxTaskWorkGroupSizeX_NV = */ 32,
			/* .maxTaskWorkGroupSizeY_NV = */ 1,
			/* .maxTaskWorkGroupSizeZ_NV = */ 1,
			/* .maxMeshViewCountNV = */ 4,
			/* .maxDualSourceDrawBuffersEXT = */ 1,
		};
#endif

		VulkanShader::VulkanShader(Handle<gui::SDLVulkanDevice> dev, Type t, const std::string& n)
		: device(std::move(dev)),
		  shaderModule(VK_NULL_HANDLE),
		  type(t),
		  name(n),
		  compiled(false) {
			SPADES_MARK_FUNCTION();
#ifdef HAVE_GLSLANG
			InitializeGlslang();
#endif
		}

		VulkanShader::~VulkanShader() {
			SPADES_MARK_FUNCTION();

			if (shaderModule != VK_NULL_HANDLE && device) {
				vkDestroyShaderModule(device->GetDevice(), shaderModule, nullptr);
			}
		}

		void VulkanShader::AddSource(const std::string& source) {
			if (compiled) {
				SPRaise("Cannot add source to already compiled shader");
			}
			sources.push_back(source);
		}

		void VulkanShader::SetSource(const std::string& source) {
			if (compiled) {
				SPRaise("Cannot set source on already compiled shader");
			}
			sources.clear();
			sources.push_back(source);
		}

		bool VulkanShader::CompileGLSLToSPIRV(const std::string& glslSource) {
#ifdef HAVE_GLSLANG
			EShLanguage stage = GetGlslangShaderType(type);

			glslang::TShader shader(stage);
			const char* sourcePtr = glslSource.c_str();
			shader.setStrings(&sourcePtr, 1);

			// Set environment for Vulkan 1.0
			shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
			shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
			shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);

			// Parse shader
			// Use GLSL 450 for Vulkan (not OpenGL 330)
			const int defaultVersion = 450;
			EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

			if (!shader.parse(&DefaultTBuiltInResource, defaultVersion, false, messages)) {
				SPLog("GLSL compilation failed for shader '%s':", name.c_str());
				SPLog("%s", shader.getInfoLog());
				SPLog("%s", shader.getInfoDebugLog());
				return false;
			}

			// Link
			glslang::TProgram program;
			program.addShader(&shader);

			if (!program.link(messages)) {
				SPLog("GLSL linking failed for shader '%s':", name.c_str());
				SPLog("%s", program.getInfoLog());
				SPLog("%s", program.getInfoDebugLog());
				return false;
			}

			// Convert to SPIR-V
			glslang::GlslangToSpv(*program.getIntermediate(stage), spirvCode);

			SPLog("Successfully compiled shader '%s' to SPIR-V (%zu bytes)",
			      name.c_str(), spirvCode.size() * sizeof(uint32_t));

			return true;
#else
			SPLog("GLSL to SPIR-V compilation not available for shader '%s'", name.c_str());
			SPLog("Please recompile with HAVE_GLSLANG enabled");
			return false;
#endif
		}

		void VulkanShader::Compile() {
			SPADES_MARK_FUNCTION();

			if (compiled) {
				SPLog("Warning: Shader '%s' already compiled", name.c_str());
				return;
			}

			if (sources.empty()) {
				SPRaise("No source code provided for shader '%s'", name.c_str());
			}

			// Concatenate all sources
			std::string combinedSource;
			for (const auto& src : sources) {
				combinedSource += src;
				combinedSource += "\n";
			}

			// Compile GLSL to SPIR-V
			if (!CompileGLSLToSPIRV(combinedSource)) {
				SPRaise("Failed to compile shader '%s'", name.c_str());
			}

			// Create shader module
			VkShaderModuleCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			createInfo.codeSize = spirvCode.size() * sizeof(uint32_t);
			createInfo.pCode = spirvCode.data();

			VkResult result = vkCreateShaderModule(device->GetDevice(), &createInfo, nullptr, &shaderModule);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create shader module for '%s' (error code: %d)", name.c_str(), result);
			}

			compiled = true;
			SPLog("Compiled Vulkan shader: %s (%zu SPIR-V words)", name.c_str(), spirvCode.size());
		}

		void VulkanShader::LoadSPIRV(const std::vector<uint32_t>& spirv) {
			SPADES_MARK_FUNCTION();

			if (compiled) {
				SPRaise("Shader '%s' already compiled", name.c_str());
			}

			spirvCode = spirv;

			VkShaderModuleCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			createInfo.codeSize = spirvCode.size() * sizeof(uint32_t);
			createInfo.pCode = spirvCode.data();

			VkResult result = vkCreateShaderModule(device->GetDevice(), &createInfo, nullptr, &shaderModule);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create shader module from SPIR-V (error code: %d)", result);
			}

			compiled = true;
			SPLog("Loaded Vulkan shader from SPIR-V: %s (%zu words)", name.c_str(), spirvCode.size());
		}

		VkShaderStageFlagBits VulkanShader::GetVkStage() const {
			switch (type) {
				case VertexShader: return VK_SHADER_STAGE_VERTEX_BIT;
				case FragmentShader: return VK_SHADER_STAGE_FRAGMENT_BIT;
				case GeometryShader: return VK_SHADER_STAGE_GEOMETRY_BIT;
				case ComputeShader: return VK_SHADER_STAGE_COMPUTE_BIT;
				default: return VK_SHADER_STAGE_VERTEX_BIT;
			}
		}

	} // namespace draw
} // namespace spades
