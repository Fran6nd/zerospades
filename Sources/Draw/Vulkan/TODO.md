# Vulkan Renderer — TODO

## Post-processing Infrastructure (sequential, complete in order)

- [x] **[PP-A] Instantiate filter members in `VulkanRenderer`** — done per-filter alongside each PP-N implementation task below (each filter adds its own forward decl + member + destructor reset).
  - Files: [VulkanRenderer.h](VulkanRenderer.h), [VulkanRenderer.cpp](VulkanRenderer.cpp)

- [x] **[PP-B] Replace direct blit with ping-pong post-process infrastructure** — in `RecordCommandBuffer()` (~line 2046), instead of blitting `offscreenColor` directly to the swapchain: allocate a second temp image via `temporaryImagePool`; introduce a `currentInput`/`currentOutput` pointer pair initialized to `offscreenColor`/tempImage; swap after each filter call; final blit uses `currentInput`. No filters called yet — pure plumbing.
  - Files: [VulkanRenderer.cpp](VulkanRenderer.cpp) (blit path ~line 2046)

## Post-processing Filters

All filter classes were deleted and must be reimplemented from scratch. Use the GL renderer as the reference implementation (`Sources/Draw/OpenGL/GL*Filter.*`). Each filter follows two independent steps: shaders first, then class + wiring.

Base class to follow: [VulkanPostProcessFilter.h](VulkanPostProcessFilter.h), [VulkanPostProcessFilter.cpp](VulkanPostProcessFilter.cpp).

---

### PP-1: Auto Exposure (`r_hdr`)

- [x] **[PP-1a] Write AutoExposure Vulkan shaders** — port `OpenGL/PostFilters/AutoExposure.fs`, `AutoExposurePreprocess.fs` to Vulkan GLSL; create `Shaders/Vulkan/PostFilters/AutoExposure.vk.*`, `AutoExposurePreprocess.vk.*`, `AutoExposureApply.vk.*`; compile to SPIR-V.
  - Reference: `Resources/Shaders/OpenGL/PostFilters/AutoExposure.*`, `AutoExposurePreprocess.*`

- [x] **[PP-1b] Implement `VulkanAutoExposureFilter` and wire** — write `VulkanAutoExposureFilter.h/.cpp` following `GLAutoExposureFilter` logic; add `std::unique_ptr<VulkanAutoExposureFilter> autoExposureFilter` member + forward decl to `VulkanRenderer.h`; construct and `.reset()` in `VulkanRenderer.cpp`; call `autoExposureFilter->Filter(cmd, currentInput, currentOutput, dt)` early in `RecordCommandBuffer` post chain (after water pass). Swap.
  - Reference: [Sources/Draw/OpenGL/GLAutoExposureFilter.h](../OpenGL/GLAutoExposureFilter.h), [GLAutoExposureFilter.cpp](../OpenGL/GLAutoExposureFilter.cpp)
  - Files: [VulkanRenderer.h](VulkanRenderer.h), [VulkanRenderer.cpp](VulkanRenderer.cpp)

---

### PP-2: Bloom (`r_bloom`)

- [ ] **[PP-2a] Write Bloom Vulkan shaders** — port `OpenGL/PostFilters/Gauss1D.fs`, downsample and composite passes to Vulkan GLSL; create `Shaders/Vulkan/PostFilters/Gauss1D.vk.*`, `BloomDownsample.vk.*`, `BloomComposite.vk.*`; compile to SPIR-V.
  - Reference: `Resources/Shaders/OpenGL/PostFilters/Gauss1D.*`

- [ ] **[PP-2b] Implement `VulkanBloomFilter` and wire** — write `VulkanBloomFilter.h/.cpp` following `GLBloomFilter`; multi-level downsample/blur/composite internally; add member to `VulkanRenderer`; call `bloomFilter->Filter(cmd, currentInput, currentOutput)` after auto-exposure (or first in chain if HDR off). Swap.
  - Reference: [Sources/Draw/OpenGL/GLBloomFilter.h](../OpenGL/GLBloomFilter.h), [GLBloomFilter.cpp](../OpenGL/GLBloomFilter.cpp)
  - Files: [VulkanRenderer.h](VulkanRenderer.h), [VulkanRenderer.cpp](VulkanRenderer.cpp)

---

### PP-3: Fog (`r_fogShadow`)

- [ ] **[PP-3a] Write Fog Vulkan shaders** — port `OpenGL/PostFilters/Fog.fs`/`Fog.vs` and `Fog2.fs`/`Fog2.vs` to Vulkan GLSL; create `Shaders/Vulkan/PostFilters/Fog.vk.*`, `Fog2.vk.*`; compile to SPIR-V. Depth buffer binding must match whatever descriptor slot the filter uses.
  - Reference: `Resources/Shaders/OpenGL/PostFilters/Fog.*`, `Fog2.*`

- [ ] **[PP-3b] Implement `VulkanFogFilter` and wire** — write `VulkanFogFilter.h/.cpp` following `GLFogFilter` / `GLFogFilter2`; add member to `VulkanRenderer`; call early in post chain (before tone mapping). Swap.
  - Reference: [Sources/Draw/OpenGL/GLFogFilter.h](../OpenGL/GLFogFilter.h), [GLFogFilter.cpp](../OpenGL/GLFogFilter.cpp), [GLFogFilter2.cpp](../OpenGL/GLFogFilter2.cpp)
  - Files: [VulkanRenderer.h](VulkanRenderer.h), [VulkanRenderer.cpp](VulkanRenderer.cpp)

---

### PP-4: Depth of Field (`r_depthOfField`)

- [ ] **[PP-4a] Write DoF Vulkan shaders** — port `OpenGL/PostFilters/DoFCoCGen.fs`, `DoFBlur.fs`/`DoFBlur2.fs`, `DoFMix.fs` to Vulkan GLSL; create `Shaders/Vulkan/PostFilters/DoFCoCGen.vk.*`, `DoFBlur.vk.*`, `DoFMix.vk.*`; compile to SPIR-V.
  - Reference: `Resources/Shaders/OpenGL/PostFilters/DoFCoCGen.*`, `DoFBlur.*`, `DoFMix.*`

- [ ] **[PP-4b] Implement `VulkanDepthOfFieldFilter` and wire** — write `VulkanDepthOfFieldFilter.h/.cpp` following `GLDepthOfFieldFilter`; add member; call after fog, before color correction: `depthOfFieldFilter->Filter(cmd, input, output, blurDepthRange, vignetteBlur, globalBlur, nearBlur, farBlur)` — params from `sceneDef` or cvars. Swap.
  - Reference: [Sources/Draw/OpenGL/GLDepthOfFieldFilter.h](../OpenGL/GLDepthOfFieldFilter.h), [GLDepthOfFieldFilter.cpp](../OpenGL/GLDepthOfFieldFilter.cpp)
  - Files: [VulkanRenderer.h](VulkanRenderer.h), [VulkanRenderer.cpp](VulkanRenderer.cpp)

---

### PP-5: Temporal AA (`r_temporalAA`)

- [ ] **[PP-5a] Write TemporalAA Vulkan shaders** — port `OpenGL/PostFilters/TemporalAA.fs`/`TemporalAA.vs` to Vulkan GLSL; create `Shaders/Vulkan/PostFilters/TemporalAA.vk.*`; compile to SPIR-V.
  - Reference: `Resources/Shaders/OpenGL/PostFilters/TemporalAA.*`

- [ ] **[PP-5b] Implement `VulkanTemporalAAFilter` and wire** — write `VulkanTemporalAAFilter.h/.cpp` following `GLTemporalAAFilter`; two-part wire: (1) call `temporalAAFilter->GetProjectionMatrixJitter()` before scene rendering to jitter projection matrix; (2) call `temporalAAFilter->Filter(cmd, currentInput, currentOutput, useFxaa)` in post chain. Swap.
  - Reference: [Sources/Draw/OpenGL/GLTemporalAAFilter.h](../OpenGL/GLTemporalAAFilter.h), [GLTemporalAAFilter.cpp](../OpenGL/GLTemporalAAFilter.cpp)
  - Files: [VulkanRenderer.h](VulkanRenderer.h), [VulkanRenderer.cpp](VulkanRenderer.cpp)

---

### PP-6: Lens Flare

- [ ] **[PP-6a] Write LensFlare Vulkan shaders** — port `OpenGL/PostFilters/Lens.fs`/`Lens.vs`, `LensDust.fs`/`LensDust.vs` to Vulkan GLSL; create `Shaders/Vulkan/LensFlare/Scanner.vk.*`, `Draw.vk.*`; reuse or recreate `Gauss1D.vk.*` if not already done in PP-2a. Compile to SPIR-V.
  - Reference: `Resources/Shaders/OpenGL/PostFilters/Lens.*`, `LensDust.*`, `Gauss1D.*`

- [ ] **[PP-6b] Implement `VulkanLensFlareFilter` and wire** — write `VulkanLensFlareFilter.h/.cpp` following `GLLensFlareFilter`; two-part wire: (1) call `lensFlareFilter->Draw(cmd, direction, reflections, color, infinityDistance)` per light during the scene pass; (2) call `lensFlareFilter->Filter(cmd, currentInput, currentOutput)` in post chain. Swap.
  - Reference: [Sources/Draw/OpenGL/GLLensFlareFilter.h](../OpenGL/GLLensFlareFilter.h), [GLLensFlareFilter.cpp](../OpenGL/GLLensFlareFilter.cpp), [GLLensDustFilter.cpp](../OpenGL/GLLensDustFilter.cpp)
  - Files: [VulkanRenderer.h](VulkanRenderer.h), [VulkanRenderer.cpp](VulkanRenderer.cpp)

---

### PP-7: Color Correction (`r_colorCorrection`)

- [ ] **[PP-7a] Write ColorCorrection Vulkan shaders** — port `OpenGL/PostFilters/ColorCorrection.fs`/`ColorCorrection.vs` to Vulkan GLSL; create `Shaders/Vulkan/PostFilters/ColorCorrection.vk.*`; compile to SPIR-V.
  - Reference: `Resources/Shaders/OpenGL/PostFilters/ColorCorrection.*`

- [ ] **[PP-7b] Implement `VulkanColorCorrectionFilter` and wire** — write `VulkanColorCorrectionFilter.h/.cpp` following `GLColorCorrectionFilter`; add member; call near end of chain: `colorCorrectionFilter->Filter(cmd, currentInput, currentOutput, tint, fogLuminance)`. Swap.
  - Reference: [Sources/Draw/OpenGL/GLColorCorrectionFilter.h](../OpenGL/GLColorCorrectionFilter.h), [GLColorCorrectionFilter.cpp](../OpenGL/GLColorCorrectionFilter.cpp)
  - Files: [VulkanRenderer.h](VulkanRenderer.h), [VulkanRenderer.cpp](VulkanRenderer.cpp)

---

### PP-8: FXAA (`r_fxaa`)

- [ ] **[PP-8a] Write FXAA Vulkan shaders** — port `OpenGL/PostFilters/FXAA.fs`/`FXAA.vs` to Vulkan GLSL; create `Shaders/Vulkan/PostFilters/FXAA.vk.*`; compile to SPIR-V.
  - Reference: `Resources/Shaders/OpenGL/PostFilters/FXAA.*`

- [ ] **[PP-8b] Implement `VulkanFXAAFilter` and wire** — write `VulkanFXAAFilter.h/.cpp` following `GLFXAAFilter`; add member; call last in chain if `r_fxaa` on and TAA inactive: `fxaaFilter->Filter(cmd, currentInput, currentOutput)`. Swap.
  - Reference: [Sources/Draw/OpenGL/GLFXAAFilter.h](../OpenGL/GLFXAAFilter.h), [GLFXAAFilter.cpp](../OpenGL/GLFXAAFilter.cpp)
  - Files: [VulkanRenderer.h](VulkanRenderer.h), [VulkanRenderer.cpp](VulkanRenderer.cpp)

---

### PP-9: SSAO

- [ ] **[PP-9a] Write SSAO Vulkan shaders** — port `OpenGL/PostFilters/SSAO.fs` + bilateral filter to Vulkan GLSL; create `Shaders/Vulkan/PostFilters/SSAO.vk.*`, `BilateralFilter.vk.*`; compile to SPIR-V.
  - Reference: `Resources/Shaders/OpenGL/PostFilters/SSAO.*`, `BilateralFilter.*`

- [ ] **[PP-9b] Implement `VulkanSSAOFilter` and wire** — write `VulkanSSAOFilter.h/.cpp` following `GLSSAOFilter`; three steps in `RecordCommandBuffer`: (1) depth prepass before main render; (2) run SSAO filter after prepass to produce occlusion texture; (3) bind occlusion texture to model/map shader descriptor sets during main render. Refer to GL renderer for prepass trigger and binding convention.
  - Reference: [Sources/Draw/OpenGL/GLSSAOFilter.h](../OpenGL/GLSSAOFilter.h), [GLSSAOFilter.cpp](../OpenGL/GLSSAOFilter.cpp)
  - Files: [VulkanRenderer.h](VulkanRenderer.h), [VulkanRenderer.cpp](VulkanRenderer.cpp), [VulkanModelRenderer.cpp](VulkanModelRenderer.cpp), [VulkanMapRenderer.cpp](VulkanMapRenderer.cpp)

---

### PP-10: Gamma Correction, Camera Blur, Bicubic Resample

- [ ] **[PP-10a] Write Gamma Vulkan shaders** — port `OpenGL/PostFilters/GammaMix.fs`/`GammaMix.vs`; create `Shaders/Vulkan/PostFilters/GammaMix.vk.*`; compile to SPIR-V.
  - Reference: `Resources/Shaders/OpenGL/PostFilters/GammaMix.*`

- [ ] **[PP-10b] Implement `VulkanGammaFilter` and wire** — write `VulkanGammaFilter.h/.cpp` following `GLNonlinearizeFilter`; add member; wire after color correction.
  - Reference: [Sources/Draw/OpenGL/GLNonlinearizeFilter.h](../OpenGL/GLNonlinearizeFilter.h), [GLNonlinearizeFilter.cpp](../OpenGL/GLNonlinearizeFilter.cpp)
  - Files: [VulkanRenderer.h](VulkanRenderer.h), [VulkanRenderer.cpp](VulkanRenderer.cpp)

- [ ] **[PP-10c] Write CameraBlur Vulkan shaders** — port `OpenGL/PostFilters/CameraBlur.fs`/`CameraBlur.vs`; create `Shaders/Vulkan/PostFilters/CameraBlur.vk.*`; compile to SPIR-V.
  - Reference: `Resources/Shaders/OpenGL/PostFilters/CameraBlur.*`

- [ ] **[PP-10d] Implement `VulkanCameraBlurFilter` and wire** — write `VulkanCameraBlurFilter.h/.cpp` following `GLCameraBlurFilter`; add member; wire when `r_cameraBlur` active.
  - Reference: [Sources/Draw/OpenGL/GLCameraBlurFilter.h](../OpenGL/GLCameraBlurFilter.h), [GLCameraBlurFilter.cpp](../OpenGL/GLCameraBlurFilter.cpp)
  - Files: [VulkanRenderer.h](VulkanRenderer.h), [VulkanRenderer.cpp](VulkanRenderer.cpp)

- [ ] **[PP-10e] Write BicubicResample Vulkan shaders** — port `OpenGL/PostFilters/ResampleBicubic.fs`/`ResampleBicubic.vs`; create `Shaders/Vulkan/PostFilters/ResampleBicubic.vk.*`; compile to SPIR-V.
  - Reference: `Resources/Shaders/OpenGL/PostFilters/ResampleBicubic.*`

- [ ] **[PP-10f] Implement `VulkanBicubicResampleFilter` and wire** — write `VulkanBicubicResampleFilter.h/.cpp` following `GLResampleBicubicFilter`; add member; wire as the very last step before final blit, only when render resolution ≠ swapchain resolution (`r_scaleFilter`).
  - Reference: [Sources/Draw/OpenGL/GLResampleBicubicFilter.h](../OpenGL/GLResampleBicubicFilter.h), [GLResampleBicubicFilter.cpp](../OpenGL/GLResampleBicubicFilter.cpp)
  - Files: [VulkanRenderer.h](VulkanRenderer.h), [VulkanRenderer.cpp](VulkanRenderer.cpp)

---

## Bug Fixes

- [ ] **[BUG-1] All player shadows missing** — not yet investigated. Suspected causes: player (and all) models may be excluded from the shadow caster pass, or shadow map sampling is broken in the map/model shaders. Investigate whether models are submitted to `VulkanShadowMapRenderer`, and check for self-shadow bias or missing shadow texture bindings.
  - Files: [VulkanRenderer.cpp](VulkanRenderer.cpp) (shadow pass), [VulkanModelRenderer.cpp](VulkanModelRenderer.cpp), [VulkanShadowMapRenderer.cpp](VulkanShadowMapRenderer.cpp) (if exists)

## Stubs / Placeholders to Rewrite

- [ ] **[STUB-1] `VulkanWaterRenderer::RenderDynamicLightPass` uses sunlight pipeline as placeholder** — the method (`VulkanWaterRenderer.cpp:1015`) binds the sunlight pipeline instead of a dedicated water dynamic-light pipeline. Water surfaces do not respond to dynamic lights. Implement a specialized pipeline variant (and matching shader) for the water dynamic-light pass; remove the early-return guard and the placeholder comment.
  - Files: [VulkanWaterRenderer.cpp](VulkanWaterRenderer.cpp)

- [ ] **[STUB-2] `VulkanOptimizedVoxelModel::PreloadShaders()` is empty** — the method (`VulkanOptimizedVoxelModel.cpp:44`) has a body with only a comment. Shader pipelines are compiled on first use, causing a stutter on the first rendered frame. Implement shader preloading (build all pipeline variants eagerly during `Init()`/startup) and remove the stub comment.
  - Files: [VulkanOptimizedVoxelModel.cpp](VulkanOptimizedVoxelModel.cpp)

- [ ] **[STUB-3] `VulkanMapRenderer::PreloadShaders()` is empty** — same issue as STUB-2 (`VulkanMapRenderer.cpp:126`). Map chunk pipelines are compiled on demand. Implement preloading and remove the stub comment.
  - Files: [VulkanMapRenderer.cpp](VulkanMapRenderer.cpp)

## Completed

- [x] Implement `ReadBitmap()` via staging buffer copy-back (`VulkanRenderer.cpp`).
- [x] Honor `sceneDef.skipWorld` — add guard in `RecordCommandBuffer` (map outline + main map passes).
