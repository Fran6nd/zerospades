# Vulkan Renderer — TODO

## Post-processing Infrastructure (sequential, complete in order)

- [ ] **[PP-A] Instantiate filter members in `VulkanRenderer`** — add one `std::unique_ptr<VulkanXxxFilter>` per filter to `VulkanRenderer.h`; construct each (passing `*this`) at the end of `VulkanRenderer::Init()`, same pattern as `mapRenderer`/`modelRenderer`. Filters to add: `VulkanAutoExposureFilter`, `VulkanBloomFilter`, `VulkanFXAAFilter`, `VulkanColorCorrectionFilter`, `VulkanDepthOfFieldFilter`, `VulkanFogFilter`, `VulkanTemporalAAFilter`, `VulkanLensFlareFilter`, `VulkanSSAOFilter`. No rendering logic yet.
  - Files: [VulkanRenderer.h](VulkanRenderer.h), [VulkanRenderer.cpp](VulkanRenderer.cpp)

- [ ] **[PP-B] Replace direct blit with ping-pong post-process infrastructure** — in `RecordCommandBuffer()` (~line 2046), instead of blitting `offscreenColor` directly to the swapchain: allocate a second temp image via `temporaryImagePool`; introduce a `currentInput`/`currentOutput` pointer pair initialized to `offscreenColor`/tempImage; swap after each filter call; final blit uses `currentInput`. No filters called yet — pure plumbing.
  - Files: [VulkanRenderer.cpp](VulkanRenderer.cpp) (blit path ~line 2046)

## Post-processing Filters (each independent; all require PP-A + PP-B)

Suggested wiring order: 1 → 2 → 7 → 8 (minimal viable chain), then 3–6, then 9 and 10.

- [ ] **[PP-1] Wire `VulkanAutoExposureFilter`** (`r_hdr`) — call `autoExposureFilter->Filter(cmd, currentInput, currentOutput, dt)` after the water pass; `dt` from game timer. Swap. Input must be in `SHADER_READ_ONLY_OPTIMAL`, output in `COLOR_ATTACHMENT_OPTIMAL`. Vulkan shaders already exist (`PostFilters/AutoExposure.vk.*`).
  - Files: [VulkanRenderer.cpp](VulkanRenderer.cpp), [VulkanAutoExposureFilter.h](VulkanAutoExposureFilter.h)

- [ ] **[PP-2] Wire `VulkanBloomFilter`** (`r_bloom`) — call `bloomFilter->Filter(cmd, currentInput, currentOutput)` after auto-exposure (or first if HDR off). Swap. Filter manages its own multi-level downsample/composite passes internally. Vulkan shaders already exist (`PostFilters/BloomDownsample.vk.*`, `BloomComposite.vk.*`).
  - Files: [VulkanRenderer.cpp](VulkanRenderer.cpp), [VulkanBloomFilter.h](VulkanBloomFilter.h)

- [ ] **[PP-3] Port fog shaders to Vulkan and wire `VulkanFogFilter`** (`r_fogShadow`) — `VulkanFogFilter.cpp` currently loads `"Shaders/OpenGL/PostFilters/Fog.program"` (wrong). No Vulkan fog shaders exist. Steps: (1) port `OpenGL/PostFilters/Fog.fs`/`Fog2.fs`/`Fog.vs` to Vulkan GLSL, create `Shaders/Vulkan/PostFilters/Fog.vk.*`, compile to SPIR-V; (2) fix `VulkanFogFilter.cpp` to load the Vulkan program; (3) wire `fogFilter->Filter(cmd, currentInput, currentOutput)` early in the post chain (before tone mapping). Swap. Verify which descriptor binding the filter uses to sample the depth buffer.
  - Files: [VulkanFogFilter.cpp](VulkanFogFilter.cpp), [VulkanRenderer.cpp](VulkanRenderer.cpp), `Resources/Shaders/Vulkan/PostFilters/` (new)

- [ ] **[PP-4] Wire `VulkanDepthOfFieldFilter`** (`r_depthOfField`) — run after fog, before color correction. Call the extended overload: `depthOfFieldFilter->Filter(cmd, input, output, blurDepthRange, vignetteBlur, globalBlur, nearBlur, farBlur)`; retrieve parameters from `sceneDef` or cvars. Swap. Vulkan shaders already exist (`PostFilters/DoFCoCGen.vk.*`, `DoFBlur.vk.*`, `DoFMix.vk.*`).
  - Files: [VulkanRenderer.cpp](VulkanRenderer.cpp), [VulkanDepthOfFieldFilter.h](VulkanDepthOfFieldFilter.h)

- [ ] **[PP-5] Wire `VulkanTemporalAAFilter`** (`r_temporalAA`) — two-part: (1) call `temporalAAFilter->GetProjectionMatrixJitter()` **before** scene rendering to offset the projection matrix (camera setup, not inside `RecordCommandBuffer`); (2) call `temporalAAFilter->Filter(cmd, currentInput, currentOutput, useFxaa)` in the post chain and swap. Vulkan shaders already exist (`PostFilters/TemporalAA.vk.*`).
  - Files: [VulkanRenderer.cpp](VulkanRenderer.cpp), [VulkanTemporalAAFilter.h](VulkanTemporalAAFilter.h)

- [ ] **[PP-6] Wire `VulkanLensFlareFilter`** — two-part: (1) call `lensFlareFilter->Draw(cmd, direction, reflections, color, infinityDistance)` per light source during the scene pass (find sun/light direction from `sceneDef`); (2) call `lensFlareFilter->Filter(cmd, currentInput, currentOutput)` in the post chain and swap. Vulkan shaders already exist (`Shaders/Vulkan/LensFlare/Scanner.vk.*`, `Draw.vk.*`; blur reuses `Gauss1D.vk.*`).
  - Files: [VulkanRenderer.cpp](VulkanRenderer.cpp), [VulkanLensFlareFilter.h](VulkanLensFlareFilter.h)

- [ ] **[PP-7] Wire `VulkanColorCorrectionFilter`** (`r_colorCorrection`) — run near end of chain (after tone mapping). Call `colorCorrectionFilter->Filter(cmd, currentInput, currentOutput, tint, fogLuminance)`; `tint`/`fogLuminance` from fog color and scene params. Swap. Confirm whether gamma correction is embedded in this filter or needs a separate step (see PP-10). Vulkan shaders already exist (`PostFilters/ColorCorrection.vk.*`).
  - Files: [VulkanRenderer.cpp](VulkanRenderer.cpp), [VulkanColorCorrectionFilter.h](VulkanColorCorrectionFilter.h)

- [ ] **[PP-8] Wire `VulkanFXAAFilter`** (`r_fxaa`) — run last, or second-to-last if bicubic resample is active. If `r_fxaa` is on and TAA is not active: call `fxaaFilter->Filter(cmd, currentInput, currentOutput)`. Swap. Vulkan shaders already exist (`PostFilters/FXAA.vk.*`).
  - Files: [VulkanRenderer.cpp](VulkanRenderer.cpp), [VulkanFXAAFilter.h](VulkanFXAAFilter.h)

- [ ] **[PP-9] Wire `VulkanSSAOFilter`** — `VulkanSSAOFilter.h/.cpp` and Vulkan shaders (`PostFilters/SSAO.vk.*`) already exist. Needs three steps in `RecordCommandBuffer`: (1) add a depth prepass before the main render; (2) call the SSAO filter after the depth prepass to produce an occlusion texture; (3) bind the occlusion texture to the model/map shader descriptor sets during the main render pass. Refer to the GL renderer for the prepass trigger and binding convention.
  - Files: [VulkanRenderer.cpp](VulkanRenderer.cpp), [VulkanSSAOFilter.h](VulkanSSAOFilter.h), [VulkanModelRenderer.cpp](VulkanModelRenderer.cpp), [VulkanMapRenderer.cpp](VulkanMapRenderer.cpp)

- [ ] **[PP-10] Port and wire Gamma Correction, Camera Blur (`r_cameraBlur`), Bicubic Resample (`r_scaleFilter`)** — no Vulkan shaders or filter classes exist for any of these. For each: (1) port the OpenGL shader (`PostFilters/GammaMix.fs`, `CameraBlur.fs`, `ResampleBicubic.fs`) to Vulkan GLSL, compile to SPIR-V; (2) write a filter class following the `VulkanPostProcessFilter` pattern; (3) add to PP-A instantiation list; (4) wire in the post chain. Bicubic resample must be the very last step before the final blit, and only active when render resolution ≠ swapchain resolution.
  - Files: `Resources/Shaders/Vulkan/PostFilters/` (new), new `VulkanGammaFilter.cpp/.h`, `VulkanCameraBlurFilter.cpp/.h`, `VulkanBicubicResampleFilter.cpp/.h`, [VulkanRenderer.cpp](VulkanRenderer.cpp)

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
