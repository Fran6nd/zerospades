# Vulkan Renderer TODO List

This document lists all pending implementation work for the Vulkan renderer port.

## Recently Completed

### Shader System (Critical Priority) ✓
- [x] SPIR-V shader reflection with spirv-cross (VulkanProgram.cpp)
- [x] Automatic descriptor set layout generation
- [x] Push constant reflection and pipeline layout configuration
- Commit: `4102587 - Implement SPIR-V shader reflection system`

### Voxel Map Rendering Fix ✓
- [x] Fixed mesh holes by matching OpenGL vertex generation
- [x] Corrected winding order for Vulkan's Y-down NDC coordinate system
- [x] Changed frontFace to CLOCKWISE to match OpenGL's CCW convention
- Commits: `dbe3d00` (reverted), `04c364d` (revert), `da15150 - Fix voxel map mesh rendering`

## Post-Processing Filters

### VulkanFogFilter
**File:** [Sources/Draw/VulkanFogFilter.cpp](Sources/Draw/VulkanFogFilter.cpp)

- [ ] Line 72: Create descriptor set layout for shadowMapTexture, coarseShadowMapTexture
- [ ] Line 77: Create pipeline layout
- [ ] Line 79: Create shader modules from SPIR-V (Fog.program)
- [ ] Line 81: Create graphics pipeline with fog shader
- [ ] Lines 89-93: Implement Filter() method:
  - [ ] Bind pipeline
  - [ ] Bind descriptor sets with textures and uniforms
  - [ ] Begin render pass with output framebuffer
  - [ ] Draw fullscreen quad
  - [ ] End render pass

### VulkanAutoExposureFilter
**File:** [Sources/Draw/VulkanAutoExposureFilter.cpp](Sources/Draw/VulkanAutoExposureFilter.cpp)

- [ ] Line 103: Create pipelines for auto exposure (preprocessPipeline, downsample, compute luminance)
- [ ] Lines 139-140: Implement auto exposure filtering:
  - [ ] Preprocess: Downsample input and compute average luminance
  - [ ] Adapt exposure based on luminance history

### VulkanTemporalAAFilter
**File:** [Sources/Draw/VulkanTemporalAAFilter.cpp](Sources/Draw/VulkanTemporalAAFilter.cpp)

- [ ] Lines 75-76: Create descriptor set layout for current frame texture and history buffer
- [ ] Line 80: Create pipeline layout
- [ ] Line 82: Create shader modules from SPIR-V (TemporalAA.program)
- [ ] Line 84: Create graphics pipeline with temporal AA shader
- [ ] Lines 125-126: Implement temporal anti-aliasing with history buffer validation

### VulkanColorCorrectionFilter
**File:** [Sources/Draw/VulkanColorCorrectionFilter.cpp](Sources/Draw/VulkanColorCorrectionFilter.cpp)

- [ ] Line 72: Create descriptor set layout for input texture and uniforms (tint, fogLuminance)
- [ ] Line 74: Create pipeline layout
- [ ] Line 76: Create shader modules from SPIR-V (ColorCorrection.program)
- [ ] Line 78: Create graphics pipeline with color correction shader
- [ ] Lines 91-96: Implement Filter() method with pipeline binding and rendering

### VulkanFXAAFilter
**File:** [Sources/Draw/VulkanFXAAFilter.cpp](Sources/Draw/VulkanFXAAFilter.cpp)

- [ ] Line 72: Create descriptor set layout for input texture and inverseVP uniform
- [ ] Line 74: Create pipeline layout
- [ ] Line 76: Create shader modules from SPIR-V (FXAA.program)
- [ ] Line 78: Create graphics pipeline with FXAA shader
- [ ] Lines 86-90: Implement Filter() method with full rendering pipeline

### VulkanLensFlareFilter
**File:** [Sources/Draw/VulkanLensFlareFilter.cpp](Sources/Draw/VulkanLensFlareFilter.cpp)

- [ ] Line 110: Create pipelines for lens flare (blurPipeline, compositePipeline)
- [ ] Lines 119-120: Load flare texture images (flare1, flare2, flare3, flare4, white)
- [ ] Lines 138-139: Implement lens flare drawing:
  - [ ] Scan for bright spots in the scene
  - [ ] Render flare sprites along sun-to-center axis

### VulkanBloomFilter
**File:** [Sources/Draw/VulkanBloomFilter.cpp](Sources/Draw/VulkanBloomFilter.cpp)

- [ ] Lines 112-113: Create shader modules and pipeline with SPIR-V shaders
- [ ] Line 173: Implement actual bloom filtering with downsampling and compositing

### VulkanSSAOFilter
**File:** [Sources/Draw/VulkanSSAOFilter.cpp](Sources/Draw/VulkanSSAOFilter.cpp)

- [ ] Line 102: Create SSAO and bilateral filter pipelines (needs shader support)
- [ ] Lines 152-156: Implement SSAO filtering:
  - [ ] Generate ambient occlusion from depth buffer
  - [ ] Apply bilateral filter to smooth the result

### VulkanDepthOfFieldFilter
**File:** [Sources/Draw/VulkanDepthOfFieldFilter.cpp](Sources/Draw/VulkanDepthOfFieldFilter.cpp)

- [ ] Lines 135-136: Create multiple pipelines for DoF stages:
  - [ ] cocGenPipeline: Generate Circle of Confusion radius
  - [ ] Blur passes with varying radii
  - [ ] Composite pipeline
- [ ] Lines 154-159: Implement multi-stage depth of field filtering:
  - [ ] Generate CoC (Circle of Confusion)
  - [ ] Multi-pass blur with varying radii
  - [ ] Composite blurred and sharp layers

## Core Renderer

### VulkanRenderer
**File:** [Sources/Draw/VulkanRenderer.cpp](Sources/Draw/VulkanRenderer.cpp)

- [x] Line 653: Implement fog shadow support when VulkanMapShadowRenderer is added
- [x] Line 683: Debug lines rendering through dedicated debug line renderer
- [x] Line 735: Dynamic lights accumulation and lighting passes implementation
- [x] Line 774: Get render finished semaphore properly
- [x] Line 785: Use proper synchronization with fences instead of blocking wait
- [x] Line 792: Implement screen color multiplication
- [x] Line 918: Implement flat map update
- [x] Line 922: Implement flat map rendering
- [x] Line 1017: Implement framebuffer readback
- [x] Line 1097: Add depth prepass, shadow maps, etc.

### VulkanMapRenderer
**File:** [Sources/Draw/VulkanMapRenderer.cpp](Sources/Draw/VulkanMapRenderer.cpp)

- [ ] Line 121: Preload shaders when shader system is implemented
- [ ] Line 227: Implement dynamic light pass
- [ ] Line 232: Implement outline pass
- [ ] Line 326: Implement backface rendering

### VulkanWaterRenderer
**File:** [Sources/Draw/VulkanWaterRenderer.cpp](Sources/Draw/VulkanWaterRenderer.cpp)

- [ ] Line 334: Complete shader conversion before enabling shader preloading
- [ ] Line 434: Bind descriptor sets with textures (water color, wave, mirror)
- [ ] Line 448: Implement dynamic lights for water

### VulkanSpriteRenderer
**File:** [Sources/Draw/VulkanSpriteRenderer.cpp](Sources/Draw/VulkanSpriteRenderer.cpp)

- [x] Line 38: Initialize pipeline
- [x] Line 125: Bind texture descriptor set

### VulkanShadowMapRenderer
**File:** [Sources/Draw/VulkanShadowMapRenderer.cpp](Sources/Draw/VulkanShadowMapRenderer.cpp)

- [ ] Line 146: Create shadow map rendering pipeline (needs shader support)
- [ ] Line 231: Render shadow casters (map and models from light's perspective)

### VulkanOptimizedVoxelModel
**File:** [Sources/Draw/VulkanOptimizedVoxelModel.cpp](Sources/Draw/VulkanOptimizedVoxelModel.cpp)

- [ ] Line 40: Preload shaders when shader system is complete
- [ ] Line 292: Implement depth prerender
- [ ] Line 298: Implement shadow map rendering
- [ ] Line 353: Implement dynamic light rendering
- [ ] Line 359: Implement outline rendering

## Shader System

### VulkanProgram
**File:** [Sources/Draw/VulkanProgram.cpp](Sources/Draw/VulkanProgram.cpp)

- [x] Line 27: Add spirv-cross to CMake dependencies for shader reflection
- [x] Lines 85-87: Implement shader reflection using spirv-cross

## Summary

### Categories:
1. **Post-processing effects** - 10 filters requiring full pipeline implementation
   - Fog, Auto Exposure, Temporal AA, Color Correction, FXAA
   - Lens Flare, Bloom, SSAO, Depth of Field

2. **Core rendering features** - Shadows, dynamic lights, debug rendering
   - Shadow maps
   - Dynamic lighting system
   - Debug line rendering
   - Flat map rendering

3. **Shader system** - Reflection and descriptor set layout automation
   - ✓ SPIR-V shader reflection (COMPLETED)
   - ✓ Automatic descriptor set layout generation (COMPLETED)
   - ✓ Push constant reflection (COMPLETED)

4. **Special effects** - Water, sprites, model rendering features
   - Water shader bindings and dynamic lights
   - Sprite pipeline and texture binding
   - Model depth prepass and shadow maps

### Priority:
1. ~~**Critical**: Shader system (required for most other features)~~ **COMPLETED**
2. **High**: Shadow maps, dynamic lights, core rendering
3. **Medium**: Post-processing filters
4. **Low**: Special effects (lens flare, advanced filters)
