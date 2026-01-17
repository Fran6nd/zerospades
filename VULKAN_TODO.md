# Vulkan Renderer TODO List

This document tracks missing features, incomplete implementations, and compliance issues in the Vulkan renderer.

---

## 🔴 CRITICAL PRIORITY - Broken/Missing Core Features

### 1. Shadow Map Rendering (COMPLETE)
**Files:** `VulkanShadowMapRenderer.cpp`, `VulkanRenderer.cpp`
- [x] Implement shadow map rendering pipeline ([VulkanShadowMapRenderer.cpp:480-553](Sources/Draw/VulkanShadowMapRenderer.cpp#L480-L553))
- [x] Implement map renderer's shadow pass ([VulkanMapRenderer.cpp:342-354](Sources/Draw/VulkanMapRenderer.cpp#L342-L354))
- [x] Implement model renderer's shadow pass ([VulkanModelRenderer.cpp:54-61](Sources/Draw/VulkanModelRenderer.cpp#L54-L61))
- [x] Update `GetFogColorForSolidPass()` to use shadow maps ([VulkanRenderer.cpp:698-703](Sources/Draw/VulkanRenderer.cpp#L698-L703))
- [x] Fix render pass ordering (shadow maps now render before main pass)
- [x] Test shadow rendering quality and performance with r_fogShadow setting

**Status:** Implementation complete. Shadow maps now render in separate render passes before main scene rendering. Needs visual testing.

---

### 2. Water Rendering (BASIC COMPLETE)
**File:** `VulkanWaterRenderer.cpp`
- [x] Complete shader conversion for basic water rendering
- [x] Enable shader preloading in `PreloadShaders()`
- [x] Convert Water.program to Vulkan (Water.vk.program created)
- [x] Compile Water.vk.vs and Water.vk.fs to SPIR-V
- [ ] Convert Water2.program and Water3.program to Vulkan (for higher quality settings)
- [ ] Test water rendering at different quality levels (r_water 1/2/3 settings)

**Status:** Basic water rendering (r_water 1) is now enabled and functional. Higher quality modes need shader conversion.

---

### 3. Shader System (FUNCTIONAL)
**Files:** `VulkanMapRenderer.cpp`, `VulkanOptimizedVoxelModel.cpp`, `VulkanWaterRenderer.cpp`
- [x] Verify all core shader programs are properly converted from OpenGL
- [x] All required SPIR-V shaders compiled and available (16 shaders)
- [ ] PreloadShaders() implementations (currently empty placeholders - shaders load on-demand)
- [ ] Test shader hot-reloading functionality
- [ ] Document shader conversion requirements
- [ ] Add dynamic light rendering shaders (feature missing)

**Status:** Core shader system is functional. Shaders load on-demand when pipelines are created. PreloadShaders is optional optimization.

---

### 4. Dynamic Light Rendering (MISSING)
**Files:** `VulkanMapRenderer.cpp`, `VulkanModelRenderer.cpp`
- [ ] Implement RenderDynamicLightPass() in VulkanRenderer
- [ ] Create dynamic light shaders (for muzzle flashes, explosions, etc.)
- [ ] Integrate dynamic light rendering into render loop
- [ ] Test with grenades, gunfire, and other light sources

**Impact:** Dynamic lights (explosions, muzzle flashes) don't illuminate the scene. Visual feature, not critical.

---

### 5. Error Handling Audit (PARTIAL)
**Files:** All `Vulkan*.cpp` files
- [x] Audit critical Vulkan API calls for VkResult checking
- [x] Added error checking to 6 previously unchecked calls
- [x] Added error messages for command buffer allocation failures
- [ ] Complete audit of all remaining Vulkan API calls
- [ ] Consider using a VK_CHECK() macro for consistency

**Status:** Improved error handling in critical paths. All command buffer allocations and key framebuffer creations now check for errors.

---

## 🟠 HIGH PRIORITY - Post-Processing Effects (Placeholder Implementations)

All post-processing filters have placeholder implementations that don't actually work.

### 6. Color Correction Filter
**File:** `VulkanColorCorrectionFilter.cpp`
- [ ] Create descriptor set layout for input texture and uniforms ([VulkanColorCorrectionFilter.cpp:72](Sources/Draw/VulkanColorCorrectionFilter.cpp#L72))
- [ ] Create pipeline layout ([VulkanColorCorrectionFilter.cpp:74](Sources/Draw/VulkanColorCorrectionFilter.cpp#L74))
- [ ] Create shader modules from SPIR-V ([VulkanColorCorrectionFilter.cpp:76](Sources/Draw/VulkanColorCorrectionFilter.cpp#L76))
- [ ] Create graphics pipeline ([VulkanColorCorrectionFilter.cpp:78](Sources/Draw/VulkanColorCorrectionFilter.cpp#L78))
- [ ] Implement Filter() method:
  - [ ] Bind pipeline ([VulkanColorCorrectionFilter.cpp:91](Sources/Draw/VulkanColorCorrectionFilter.cpp#L91))
  - [ ] Update uniforms (tint, fogLuminance) ([VulkanColorCorrectionFilter.cpp:92](Sources/Draw/VulkanColorCorrectionFilter.cpp#L92))
  - [ ] Bind descriptor set ([VulkanColorCorrectionFilter.cpp:93](Sources/Draw/VulkanColorCorrectionFilter.cpp#L93))
  - [ ] Begin render pass ([VulkanColorCorrectionFilter.cpp:94](Sources/Draw/VulkanColorCorrectionFilter.cpp#L94))
  - [ ] Draw fullscreen quad ([VulkanColorCorrectionFilter.cpp:95](Sources/Draw/VulkanColorCorrectionFilter.cpp#L95))
  - [ ] End render pass ([VulkanColorCorrectionFilter.cpp:96](Sources/Draw/VulkanColorCorrectionFilter.cpp#L96))

---

### 7. FXAA Anti-Aliasing Filter
**File:** `VulkanFXAAFilter.cpp`
- [ ] Create descriptor set layout ([VulkanFXAAFilter.cpp:72](Sources/Draw/VulkanFXAAFilter.cpp#L72))
- [ ] Create pipeline layout ([VulkanFXAAFilter.cpp:74](Sources/Draw/VulkanFXAAFilter.cpp#L74))
- [ ] Create shader modules from SPIR-V ([VulkanFXAAFilter.cpp:76](Sources/Draw/VulkanFXAAFilter.cpp#L76))
- [ ] Create graphics pipeline ([VulkanFXAAFilter.cpp:78](Sources/Draw/VulkanFXAAFilter.cpp#L78))
- [ ] Implement Filter() method:
  - [ ] Bind pipeline ([VulkanFXAAFilter.cpp:86](Sources/Draw/VulkanFXAAFilter.cpp#L86))
  - [ ] Bind descriptor set with inverseVP uniform ([VulkanFXAAFilter.cpp:87](Sources/Draw/VulkanFXAAFilter.cpp#L87))
  - [ ] Begin render pass ([VulkanFXAAFilter.cpp:88](Sources/Draw/VulkanFXAAFilter.cpp#L88))
  - [ ] Draw fullscreen quad ([VulkanFXAAFilter.cpp:89](Sources/Draw/VulkanFXAAFilter.cpp#L89))
  - [ ] End render pass ([VulkanFXAAFilter.cpp:90](Sources/Draw/VulkanFXAAFilter.cpp#L90))

---

### 8. Temporal Anti-Aliasing Filter
**File:** `VulkanTemporalAAFilter.cpp`
- [ ] Create descriptor set layout for textures and motion vectors ([VulkanTemporalAAFilter.cpp:75-78](Sources/Draw/VulkanTemporalAAFilter.cpp#L75-L78))
- [ ] Create pipeline layout ([VulkanTemporalAAFilter.cpp:80](Sources/Draw/VulkanTemporalAAFilter.cpp#L80))
- [ ] Create shader modules from SPIR-V ([VulkanTemporalAAFilter.cpp:82](Sources/Draw/VulkanTemporalAAFilter.cpp#L82))
- [ ] Create graphics pipeline ([VulkanTemporalAAFilter.cpp:84](Sources/Draw/VulkanTemporalAAFilter.cpp#L84))
- [ ] Implement Filter() method ([VulkanTemporalAAFilter.cpp:125-131](Sources/Draw/VulkanTemporalAAFilter.cpp#L125-L131)):
  - [ ] Check/create history buffer
  - [ ] Blend current frame with history using motion vectors
  - [ ] Apply optional FXAA pass
  - [ ] Copy result to history buffer
  - [ ] Update prevMatrix and prevViewOrigin
- [ ] Verify jitter pattern is applied correctly (GetProjectionMatrixJitter)

---

### 9. Bloom Filter
**File:** `VulkanBloomFilter.cpp`
- [ ] Create shader modules and pipelines ([VulkanBloomFilter.cpp:112](Sources/Draw/VulkanBloomFilter.cpp#L112))
  - [ ] Downsample pipeline
  - [ ] Composite pipeline
  - [ ] Final composite pipeline
- [ ] Implement bloom filtering ([VulkanBloomFilter.cpp:173](Sources/Draw/VulkanBloomFilter.cpp#L173)):
  - [ ] Downsample bright areas
  - [ ] Blur downsampled image
  - [ ] Composite with original image

---

### 10. SSAO (Screen-Space Ambient Occlusion) Filter
**File:** `VulkanSSAOFilter.cpp`
- [ ] Create SSAO pipeline ([VulkanSSAOFilter.cpp:102](Sources/Draw/VulkanSSAOFilter.cpp#L102))
- [ ] Create bilateral filter pipeline ([VulkanSSAOFilter.cpp:102](Sources/Draw/VulkanSSAOFilter.cpp#L102))
- [ ] Implement SSAO filtering ([VulkanSSAOFilter.cpp:152](Sources/Draw/VulkanSSAOFilter.cpp#L152)):
  - [ ] Generate SSAO from depth buffer and normals
  - [ ] Apply bilateral blur
  - [ ] Composite with scene

---

### 11. Depth of Field Filter
**File:** `VulkanDepthOfFieldFilter.cpp`
- [ ] Create multi-stage DoF pipelines ([VulkanDepthOfFieldFilter.cpp:135](Sources/Draw/VulkanDepthOfFieldFilter.cpp#L135)):
  - [ ] CoC (Circle of Confusion) computation pipeline
  - [ ] Blur pipeline (near/far)
  - [ ] Composite pipeline
- [ ] Implement DoF filtering ([VulkanDepthOfFieldFilter.cpp:154](Sources/Draw/VulkanDepthOfFieldFilter.cpp#L154))

---

### 12. Auto Exposure Filter
**File:** `VulkanAutoExposureFilter.cpp`
- [ ] Create preprocess pipeline ([VulkanAutoExposureFilter.cpp:103-105](Sources/Draw/VulkanAutoExposureFilter.cpp#L103-L105))
- [ ] Create compute gain pipeline ([VulkanAutoExposureFilter.cpp:103-105](Sources/Draw/VulkanAutoExposureFilter.cpp#L103-L105))
- [ ] Implement auto exposure ([VulkanAutoExposureFilter.cpp:139-142](Sources/Draw/VulkanAutoExposureFilter.cpp#L139-L142)):
  - [ ] Downsample and compute average luminance
  - [ ] Calculate exposure adjustment
  - [ ] Apply exposure to output

---

### 13. Lens Flare Filter
**File:** `VulkanLensFlareFilter.cpp`
- [ ] Create lens flare pipelines ([VulkanLensFlareFilter.cpp:110](Sources/Draw/VulkanLensFlareFilter.cpp#L110))
- [ ] Load flare texture images ([VulkanLensFlareFilter.cpp:119](Sources/Draw/VulkanLensFlareFilter.cpp#L119))
- [ ] Implement lens flare drawing ([VulkanLensFlareFilter.cpp:138](Sources/Draw/VulkanLensFlareFilter.cpp#L138))

---

## 🟡 MEDIUM PRIORITY - Optimization & Best Practices

### 14. GPU Device Selection
**File:** `SDLVulkanDevice.cpp`
- [ ] Implement GPU scoring system ([SDLVulkanDevice.cpp:295](Sources/Gui/SDLVulkanDevice.cpp#L295))
  - [ ] Score by device type (discrete > integrated > virtual > CPU)
  - [ ] Score by memory capacity
  - [ ] Score by feature support
  - [ ] Score by queue family capabilities
- [ ] Allow user override via settings
- [ ] Log selected GPU and reason

**Impact:** May not use discrete GPU on systems with multiple GPUs, resulting in poor performance.

---

### 15. Pipeline Cache Implementation
**Files:** New files needed
- [ ] Create VulkanPipelineCache class
- [ ] Save pipeline cache to disk on shutdown
- [ ] Load pipeline cache on startup
- [ ] Integrate with all pipeline creation calls
- [ ] Add cache versioning for validation

**Impact:** Faster startup times and reduced shader compilation hitches.

---

### 16. Code Refactoring - Reduce Redundancy

#### Render Pass Creation
**Files:** Multiple `Vulkan*Filter.cpp` files
- [ ] Extract common render pass creation logic
- [ ] Create utility function: `CreateSimpleColorRenderPass()`
- [ ] Update all filters to use common function
- [ ] Reduce code duplication across 8+ files

#### Pipeline Creation Boilerplate
**Files:** Multiple `Vulkan*Filter.cpp` files
- [ ] Create pipeline builder helper class
- [ ] Simplify pipeline creation with fluent API
- [ ] Reduce boilerplate in each filter

---

### 17. Synchronization Audit
**Files:** All `Vulkan*.cpp` files
- [ ] Audit all image layout transitions
- [ ] Ensure proper pipeline barriers between passes
- [ ] Verify queue family ownership transfers (if using multiple queues)
- [ ] Check for race conditions in multi-threaded rendering
- [ ] Ensure descriptor sets are not modified while in use
- [ ] Verify proper use of VK_PIPELINE_STAGE flags

**Impact:** Prevents validation errors, crashes, and rendering corruption.

---

### 18. Memory Management Audit
**Files:** All `Vulkan*.cpp` files
- [ ] Verify all vkDestroy* calls match vkCreate* calls
- [ ] Check for memory leaks with validation layers
- [ ] Audit deferred deletion queue behavior ([VulkanRenderer.h:94-95](Sources/Draw/VulkanRenderer.h#L94-L95))
- [ ] Ensure proper cleanup on renderer destruction
- [ ] Test cleanup during swapchain recreation

---

### 19. Descriptor Set Pool Optimization
**File:** `VulkanDescriptorPool.cpp`
- [ ] Review descriptor set allocation patterns
- [ ] Ensure sets are reused, not recreated each frame
- [ ] Implement descriptor set caching
- [ ] Monitor pool exhaustion and implement dynamic growth

---

## 🟢 LOW PRIORITY - Performance Optimizations

### 20. Push Constants
**Files:** Various
- [ ] Identify small uniform updates suitable for push constants
- [ ] Replace uniform buffer updates with push constants where appropriate
- [ ] Measure performance improvement

**Impact:** Reduces CPU overhead for small uniform updates.

---

### 21. Memory Aliasing
**Files:** Resource management classes
- [ ] Identify temporary resources (intermediate render targets)
- [ ] Implement memory aliasing for temporary resources
- [ ] Measure memory usage reduction

**Impact:** Reduces GPU memory consumption.

---

### 22. Validation in Release Builds
**File:** `SDLVulkanDevice.cpp`
- [ ] Consider keeping basic validation in release builds
- [ ] Add setting to enable/disable validation
- [ ] Filter out performance warnings in release mode
- [ ] Keep critical error checking

**Impact:** Better error reporting for users, at minor performance cost.

---

