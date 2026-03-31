# Vulkan Renderer — TODO

## ★ HIGH PRIORITY — Antialiasing

### AA-1: FXAA (`r_fxaa`)

**Status:** Not implemented. Blocked by PP-B (ping-pong wiring not actually present in `RecordCommandBuffer` despite being marked done — see PP-B note below).

- [ ] **[AA-1a] Write FXAA Vulkan shader** — port `Resources/Shaders/OpenGL/PostFilters/FXAA.fs` to Vulkan GLSL 450; create `Resources/Shaders/Vulkan/PostFilters/FXAA.vk.fs`; reuse `PassThrough.vk.vs` (fullscreen triangle, no vertex buffer needed). The `inverseVP` uniform becomes a push constant (`layout(push_constant) uniform PC { vec2 inverseVP; };`). Compile to SPIR-V: `glslc FXAA.vk.fs -o FXAA.vk.fs.spv`.
  - Reference: [Resources/Shaders/OpenGL/PostFilters/FXAA.fs](../../../Resources/Shaders/OpenGL/PostFilters/FXAA.fs), [PassThrough.vk.vs](../../../Resources/Shaders/Vulkan/PostFilters/PassThrough.vk.vs)

- [ ] **[AA-1b] Implement `VulkanFXAAFilter` class** — write `VulkanFXAAFilter.h/.cpp`; follows the same pattern as `VulkanFogFilter` (single render pass, single pipeline, per-frame descriptor pool + framebuffers); push constant contains `vec2 inverseVP = {1/w, 1/h}` filled from `input->GetWidth()/Height()`; single sampler binding (binding 0 = color texture). Condition: skip if `(int)r_fxaa == 0`.
  - Reference: [VulkanFogFilter.h](VulkanFogFilter.h), [VulkanFogFilter.cpp](VulkanFogFilter.cpp)
  - Files: new `VulkanFXAAFilter.h`, `VulkanFXAAFilter.cpp`

- [ ] **[AA-1c] Wire FXAA in `RecordCommandBuffer`** — after PP-B wiring is in place, add `fxaaFilter` as `std::unique_ptr<VulkanFXAAFilter>` member in `VulkanRenderer.h`; construct in `Init()` (no condition); in `RecordCommandBuffer` call last in post chain, **only** when `(int)r_fxaa != 0`. Also add `SPADES_SETTING(r_fxaa)` near other cvar declarations in `VulkanRenderer.cpp`. Do **not** call when `r_multisamples > 0` (mutually exclusive — the setup UI enforces this but guard defensively). Swap.
  - Files: [VulkanRenderer.h](VulkanRenderer.h), [VulkanRenderer.cpp](VulkanRenderer.cpp)

---

### AA-2: MSAA (`r_multisamples` = 0 / 2 / 4)

**Status:** Not implemented. All scene pipelines hardcode `VK_SAMPLE_COUNT_1_BIT`. `r_multisamples` cvar is completely ignored by the Vulkan renderer.

**Design:** MSAA is resolved at render-pass end (Vulkan native resolve attachment). The resolved 1-sample image is what the post-process chain, fog filter, and final blit see — no changes needed in PP filters. Sample count is determined once at `VulkanFramebufferManager` construction time (latch cvar — requires restart, same as GL). MSAA and FXAA are mutually exclusive (setup UI enforces `r_multisamples > 0 → r_fxaa=0`).

- [ ] **[AA-2a] MSAA support in `VulkanFramebufferManager`** — when `r_multisamples ∈ {2,4}`, create an additional multisampled color image (`VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`, no sampler needed) and multisampled depth image at the same resolution; update `CreateRenderPass()` to add a resolve attachment (the existing 1-sample `renderColorImage` becomes the resolve target); `colorAttachment.samples` / `depthAttachment.samples` → MSAA count; add `VkAttachmentDescription resolveAttachment` (`loadOp=DONT_CARE`, `storeOp=STORE`, `finalLayout=SHADER_READ_ONLY_OPTIMAL`); update `VkSubpassDescription` to use `pResolveAttachments`; update `renderFramebuffer` creation to include the MSAA images; add `GetSampleCount() → VkSampleCountFlagBits` accessor. When `r_multisamples == 0` behaviour is unchanged.
  - Note: read `r_multisamples` value in constructor; clamp to `{0,2,4}` and verify device support via `vkGetPhysicalDeviceProperties` (fall back to 1 if unsupported).
  - Files: [VulkanFramebufferManager.h](VulkanFramebufferManager.h), [VulkanFramebufferManager.cpp](VulkanFramebufferManager.cpp)

- [ ] **[AA-2b] Propagate sample count to all scene pipelines** — add `SPADES_SETTING(r_multisamples)` to `VulkanRenderer.cpp`; pass `framebufferManager->GetSampleCount()` wherever a scene-rendering pipeline sets `multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT`:
  - `VulkanRenderer.cpp` lines ~2004, ~2200, ~2469 (sky, outline, debug line pipelines — created inside `RecreateSwapchainDependencies` or dedicated Create* methods)
  - `VulkanMapRenderer.cpp` line ~605
  - `VulkanOptimizedVoxelModel.cpp` line ~1013 (all pipeline variants in `BuildPipeline`)
  - `VulkanSpriteRenderer.cpp` line ~198
  - `VulkanLongSpriteRenderer.cpp` line ~179
  - **Do NOT change** post-process filter pipelines (Bloom, AutoExposure, Fog, DoF, FXAA) — they render to 1-sample temp images.
  - **Do NOT change** shadow map pipeline, image renderer pipeline (UI), or swapchain render pass pipeline — also 1-sample.
  - Pipelines are created after `framebufferManager` is initialised; make sure `GetSampleCount()` is accessible at pipeline-creation time.
  - Files: [VulkanRenderer.cpp](VulkanRenderer.cpp), [VulkanMapRenderer.cpp](VulkanMapRenderer.cpp), [VulkanOptimizedVoxelModel.cpp](VulkanOptimizedVoxelModel.cpp), [VulkanSpriteRenderer.cpp](VulkanSpriteRenderer.cpp), [VulkanLongSpriteRenderer.cpp](VulkanLongSpriteRenderer.cpp)

---

## Post-processing Infrastructure (sequential, complete in order)

- [x] **[PP-A] Instantiate filter members in `VulkanRenderer`** — done per-filter alongside each PP-N implementation task below (each filter adds its own forward decl + member + destructor reset).
  - Files: [VulkanRenderer.h](VulkanRenderer.h), [VulkanRenderer.cpp](VulkanRenderer.cpp)

- [ ] **[PP-B] Wire ping-pong post-process chain in `RecordCommandBuffer`** — **marked done prematurely**. The `temporaryImagePool` is allocated but filters are never called. In the section after the offscreen blit barriers (~line 1800), before the `vkCmdBlitImage` to swapchain: (1) allocate `tempImage = temporaryImagePool->Acquire(renderWidth, renderHeight, framebufferManager->GetMainColorFormat())`; (2) declare `VulkanImage* currentInput = offscreenColor.GetPointerOrNull()` and `VulkanImage* currentOutput = tempImage.GetPointerOrNull()`; (3) for each enabled filter call `filter->Filter(cmd, currentInput, currentOutput)` then `std::swap(currentInput, currentOutput)`; (4) transition `currentInput` (the last written image) from `SHADER_READ_ONLY_OPTIMAL` → `TRANSFER_SRC_OPTIMAL` for the final blit (replace the current `barrier1` which transitions `offscreenColor` directly). The existing `vkCmdBlitImage` call uses `currentInput->GetImage()` instead of `offscreenColor`. Return the temp image to pool after blit.
  - **Filter call order**: autoExposureFilter (if `r_hdr`), bloomFilter (if `r_bloom`), fogFilter (if `r_fogShadow`), depthOfFieldFilter (if `r_depthOfField`), …, fxaaFilter (if `r_fxaa && r_multisamples==0`) — last.
  - Files: [VulkanRenderer.cpp](VulkanRenderer.cpp) (blit path ~line 1800)

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

- [x] **[PP-2a] Write Bloom Vulkan shaders** — port `OpenGL/PostFilters/Gauss1D.fs`, downsample and composite passes to Vulkan GLSL; create `Shaders/Vulkan/PostFilters/Gauss1D.vk.*`, `BloomDownsample.vk.*`, `BloomComposite.vk.*`; compile to SPIR-V.
  - Reference: `Resources/Shaders/OpenGL/PostFilters/Gauss1D.*`

- [x] **[PP-2b] Implement `VulkanBloomFilter` and wire** — write `VulkanBloomFilter.h/.cpp` following `GLBloomFilter`; multi-level downsample/blur/composite internally; add member to `VulkanRenderer`; call `bloomFilter->Filter(cmd, currentInput, currentOutput)` after auto-exposure (or first in chain if HDR off). Swap.
  - Reference: [Sources/Draw/OpenGL/GLBloomFilter.h](../OpenGL/GLBloomFilter.h), [GLBloomFilter.cpp](../OpenGL/GLBloomFilter.cpp)
  - Files: [VulkanRenderer.h](VulkanRenderer.h), [VulkanRenderer.cpp](VulkanRenderer.cpp)

---

### PP-3: Fog (`r_fogShadow`)

- [x] **[PP-3a] Write Fog Vulkan shaders** — port `OpenGL/PostFilters/Fog.fs`/`Fog.vs` and `Fog2.fs`/`Fog2.vs` to Vulkan GLSL; create `Shaders/Vulkan/PostFilters/Fog.vk.*`, `Fog2.vk.*`; compile to SPIR-V. Depth buffer binding must match whatever descriptor slot the filter uses.
  - Reference: `Resources/Shaders/OpenGL/PostFilters/Fog.*`, `Fog2.*`

- [x] **[PP-3b] Implement `VulkanFogFilter` and wire** — write `VulkanFogFilter.h/.cpp` following `GLFogFilter` / `GLFogFilter2`; add member to `VulkanRenderer`; call early in post chain (before tone mapping). Swap.
  - Reference: [Sources/Draw/OpenGL/GLFogFilter.h](../OpenGL/GLFogFilter.h), [GLFogFilter.cpp](../OpenGL/GLFogFilter.cpp), [GLFogFilter2.cpp](../OpenGL/GLFogFilter2.cpp)
  - Files: [VulkanRenderer.h](VulkanRenderer.h), [VulkanRenderer.cpp](VulkanRenderer.cpp)

---

### PP-4: Depth of Field (`r_depthOfField`)

- [x] **[PP-4a] Write DoF Vulkan shaders** — port `OpenGL/PostFilters/DoFCoCGen.fs`, `DoFBlur.fs`/`DoFBlur2.fs`, `DoFMix.fs` to Vulkan GLSL; create `Shaders/Vulkan/PostFilters/DoFCoCGen.vk.*`, `DoFBlur.vk.*`, `DoFMix.vk.*`; compile to SPIR-V.
  - Reference: `Resources/Shaders/OpenGL/PostFilters/DoFCoCGen.*`, `DoFBlur.*`, `DoFMix.*`

- [x] **[PP-4b] Implement `VulkanDepthOfFieldFilter` and wire** — write `VulkanDepthOfFieldFilter.h/.cpp` following `GLDepthOfFieldFilter`; add member; call after fog, before color correction: `depthOfFieldFilter->Filter(cmd, input, output, blurDepthRange, vignetteBlur, globalBlur, nearBlur, farBlur)` — params from `sceneDef` or cvars. Swap.
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

- [x] **[PP-7a] Write ColorCorrection Vulkan shaders** — port `OpenGL/PostFilters/ColorCorrection.fs`/`ColorCorrection.vs` to Vulkan GLSL; create `Shaders/Vulkan/PostFilters/ColorCorrection.vk.*`; compile to SPIR-V.
  - Reference: `Resources/Shaders/OpenGL/PostFilters/ColorCorrection.*`

- [x] **[PP-7b] Implement `VulkanColorCorrectionFilter` and wire** — write `VulkanColorCorrectionFilter.h/.cpp` following `GLColorCorrectionFilter`; add member; call near end of chain after DoF, before FXAA.  Tint + fogLuminance + exposure computed internally from `renderer.GetFogColor()` and settings with temporal smoothing (mirrors `smoothedFogColor` in GL renderer).  Sharpening pre-pass (`r_sharpen`) omitted — add when needed.
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

- [x] **[BUG-2] UI appears too small at higher resolutions**

  **Symptom**: When the window/display resolution is increased, all 2D UI elements (HUD, menus, crosshair) render much smaller than expected. The 3D world renders correctly (fills the screen) because it is composited via a scaled blit, but the 2D pass is not correctly adapted to the new resolution.

  **Preliminary investigation**:

  The 2D UI pipeline and the 3D scene pipeline are separated:
  - 3D scene → offscreen framebuffer (`framebufferManager`) at `renderWidth × renderHeight` → final blit scales it to fill the swapchain image (`device->GetSwapchainExtent()`). The blit always fills the full swapchain, so 3D looks correct regardless of resolution.
  - 2D UI → swapchain render pass (`renderPass` / `swapchainFramebuffers`) at `device->GetSwapchainExtent()`. `VulkanImageRenderer::Flush()` sets a dynamic viewport of `renderer.ScreenWidth() × renderer.ScreenHeight()`.

  **Root cause A — stale `invScreenSizeFactored` in `VulkanImageRenderer`** (primary):
  `VulkanImageRenderer` computes its NDC transform factors once in its constructor (`VulkanImageRenderer.cpp:40-41`):
  ```cpp
  invScreenWidthFactored(2.0f / r.ScreenWidth()),
  invScreenHeightFactored(-2.0f / r.ScreenHeight()),
  ```
  These are pushed verbatim to the vertex shader as push constants on every `Flush()` call (`VulkanImageRenderer.cpp:577-578`). They are **never updated** after construction. When the swapchain is recreated at a new resolution (`RecreateSwapchainDependencies()`, `VulkanRenderer.cpp:~588`), `renderWidth`/`renderHeight` are updated but `imageRenderer` is **not** recreated. The stale factors then transform UI vertex positions as if the screen were still the old (smaller) size, so all UI elements shrink to the top-left corner of the larger framebuffer.
  **Fix**: compute the factors dynamically inside `Flush()` from `renderer.ScreenWidth()`/`renderer.ScreenHeight()` rather than caching them in the constructor.

  **Root cause B — potential HiDPI mismatch** (secondary, device-dependent):
  `VulkanRenderer::ScreenWidth()` returns `renderWidth` which is assigned from `device->ScreenWidth()` (SDL logical pixel size). The swapchain framebuffers are created from `device->GetSwapchainExtent()` (physical pixel size). On HiDPI/Retina displays these can be a 2× multiple. The dynamic viewport set in `VulkanImageRenderer::Flush()` uses `renderer.ScreenWidth()` (logical) but the render pass framebuffer is `GetSwapchainExtent()` (physical), so the UI is confined to a sub-region of the screen. The OpenGL renderer avoids this because GL abstracts HiDPI at the SDL level and `device->ScreenWidth()` always matches the GL drawable size.
  **Fix**: use `device->GetSwapchainExtent().width/.height` as the authoritative UI surface dimensions, both for the dynamic viewport in `Flush()` and for computing `invScreenSizeFactored`. Expose a `SwapchainWidth()`/`SwapchainHeight()` accessor on `VulkanRenderer` (or inline `device->GetSwapchainExtent()` in `VulkanImageRenderer`).

  **Reference**:
  - `VulkanImageRenderer.cpp` lines 37–44 (constructor), lines 495–507 (viewport setup in Flush), lines 575–583 (push constants in Flush)
  - `VulkanRenderer.cpp` `RecreateSwapchainDependencies()` ~line 586 — note `imageRenderer` is absent from the rebuild list
  - `VulkanRenderer.cpp` `ScreenWidth()`/`ScreenHeight()` ~line 1430 — returns `renderWidth`, not swapchain extent
  - `GLRenderer.cpp` `ScreenWidth()`/`ScreenHeight()` ~line 345 — returns `device->ScreenWidth()` (always current physical screen size, independent of render scale)
  - Files: [VulkanImageRenderer.cpp](VulkanImageRenderer.cpp), [VulkanImageRenderer.h](VulkanImageRenderer.h), [VulkanRenderer.cpp](VulkanRenderer.cpp)

- [ ] **[BUG-1] All player shadows missing** — not yet investigated. Suspected causes: player (and all) models may be excluded from the shadow caster pass, or shadow map sampling is broken in the map/model shaders. Investigate whether models are submitted to `VulkanShadowMapRenderer`, and check for self-shadow bias or missing shadow texture bindings.
  - Files: [VulkanRenderer.cpp](VulkanRenderer.cpp) (shadow pass), [VulkanModelRenderer.cpp](VulkanModelRenderer.cpp), [VulkanShadowMapRenderer.cpp](VulkanShadowMapRenderer.cpp) (if exists)

## Deferred Features (removed for minimum-quality PR)

These were removed from the vulkan-mini branch to keep the initial PR small and correct.
Restore from git history (`VulkanWaterRenderer.*`, mirror pass in `RecordCommandBuffer`).

- [ ] **[DEFER-1] Water Renderer (`r_water` 1/2/3)** — Re-implement `VulkanWaterRenderer`; restore mirror
  framebuffer + screen-copy images in `VulkanFramebufferManager`; restore mirror render pass and
  refraction copy blocks in `RecordCommandBuffer`; re-enable `r_water > 0` guard in
  `VulkanMapChunk::IsSolid`; unlock Shader Effects presets in `ConfigViewTabs.as`.
  - References removed from: [VulkanRenderer.h](VulkanRenderer.h), [VulkanRenderer.cpp](VulkanRenderer.cpp),
    [VulkanFramebufferManager.h](VulkanFramebufferManager.h), [VulkanFramebufferManager.cpp](VulkanFramebufferManager.cpp),
    [VulkanOptimizedVoxelModel.cpp](VulkanOptimizedVoxelModel.cpp), [VulkanMapChunk.cpp](VulkanMapChunk.cpp)

---

## Cvar / Setup-Menu Inconsistencies

These cvars appear in the setup menu but are silently ignored or partially supported by the Vulkan renderer. They should either be implemented or capped with a warning so the menu reflects reality.

- [ ] **[CVAR-1] `r_fogShadow` levels 1 and 2 are identical** — `VulkanRenderer.cpp` treats any non-zero `r_fogShadow` value as "enable cascade shadow fog"; level 2 does nothing extra. In the GL renderer level 2 uses a higher-quality ambient shadow texture lookup. Either (a) implement the level-2 distinction, or (b) add a comment and cap `r_fogShadow` to 1 inside the Vulkan init path, and register an `incapableConfigs` entry for value "2" in `StartupScreenHelper` when the Vulkan renderer is active.
  - Files: [VulkanRenderer.cpp](VulkanRenderer.cpp)

- [ ] **[CVAR-2] `r_softParticles` level 2 not implemented** — `VulkanSpriteRenderer` checks `(int)r_softParticles != 0` and treats levels 1 and 2 as identical soft-particle mode. Level 2 in the GL renderer adds a gaussian depth-fade. Either implement it or cap to 1 for Vulkan (add clamp + `incapableConfigs` entry for "2").
  - Files: [VulkanSpriteRenderer.cpp](VulkanSpriteRenderer.cpp)

- [ ] **[CVAR-3] Unimplemented post-process cvars show no warning in setup menu** — the following cvars have no effect in the Vulkan renderer but the menu shows them as available (no `incapableConfigs` entry for the Vulkan renderer path):
  - `r_radiosity` — GL-only radiosity GI; not referenced in Vulkan renderer at all
  - `r_cameraBlur` — camera motion blur (PP-10d, not yet implemented); see [TODO PP-10d](TODO.md)
  - `r_lensFlare` / `r_lensFlareDynamic` — lens flare filter (PP-6, not yet implemented); see [TODO PP-6](TODO.md)
  - `r_ssao` — screen-space AO (PP-9, not yet implemented); see [TODO PP-9](TODO.md)
  - Once each filter is implemented, remove its entry from this list. Until then, consider registering `incapableConfigs` entries in `StartupScreenHelper` when `r_renderer=vulkan` (or equivalent Vulkan-renderer detection) so the menu shows a "not supported" tooltip.
  - Files: [Sources/Gui/StartupScreenHelper.cpp](../../Gui/StartupScreenHelper.cpp)

---

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
