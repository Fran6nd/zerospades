# Vulkan Renderer — TODO

## Critical

### Post-processing filter chain — infrastructure (do these first, in order)

- [ ] **[PP-A] Instantiate filter members in `VulkanRenderer`** — add one `std::unique_ptr<VulkanXxxFilter>` per filter to `VulkanRenderer.h`; construct each (passing `*this`) at the end of `VulkanRenderer::Init()`, same pattern as `mapRenderer`/`modelRenderer`. No rendering logic yet.
  - Filters: `VulkanBloomFilter`, `VulkanFXAAFilter`, `VulkanColorCorrectionFilter`, `VulkanDepthOfFieldFilter`, `VulkanFogFilter`, `VulkanTemporalAAFilter`, `VulkanAutoExposureFilter`, `VulkanLensFlareFilter`
  - Files: `VulkanRenderer.h`, `VulkanRenderer.cpp`

- [ ] **[PP-B] Replace direct blit with ping-pong post-process infrastructure** — in `RecordCommandBuffer()` (~line 1793–1873), instead of blitting offscreenColor directly to the swapchain: allocate a second temp image via `temporaryImagePool`; introduce a `currentInput`/`currentOutput` pointer pair initialized to offscreenColor/tempImage; swap after each filter call; final blit uses `currentInput`. No filters called yet — pure plumbing.
  - Files: `VulkanRenderer.cpp` (blit path)

### Post-processing filter chain — individual filters (independent, requires PP-A + PP-B)

Suggested minimum viable order: 1 → 2 → 7 → 8, then 3–6, then 9.

- [ ] **[PP-1] Wire `VulkanAutoExposureFilter`** (`r_hdr`) — call `autoExposureFilter->Filter(cmd, currentInput, currentOutput, dt)` after water pass; `dt` from game timer. Swap. Input must be `SHADER_READ_ONLY_OPTIMAL`, output `COLOR_ATTACHMENT_OPTIMAL`.
  - Files: `VulkanRenderer.cpp`, `VulkanAutoExposureFilter.h`

- [ ] **[PP-2] Wire `VulkanBloomFilter`** (`r_bloom`) — call `bloomFilter->Filter(cmd, currentInput, currentOutput)` after auto-exposure (or first if HDR off). Swap. Filter manages its own multi-level `BloomLevel` structures internally.
  - Files: `VulkanRenderer.cpp`, `VulkanBloomFilter.h`

- [ ] **[PP-3] Wire `VulkanFogFilter`** (`r_fogShadow`) — run early in chain (before tone mapping). Call `fogFilter->Filter(cmd, currentInput, currentOutput)`. Swap. Verify which descriptor binding the filter uses to sample the depth buffer.
  - Files: `VulkanRenderer.cpp`, `VulkanFogFilter.h`

- [ ] **[PP-4] Wire `VulkanDepthOfFieldFilter`** (`r_depthOfField`) — run after fog, before bloom. Call the extended overload: `depthOfFieldFilter->Filter(cmd, input, output, blurDepthRange, vignetteBlur, globalBlur, nearBlur, farBlur)`; retrieve parameters from `sceneDef` or cvars. Swap.
  - Files: `VulkanRenderer.cpp`, `VulkanDepthOfFieldFilter.h`

- [ ] **[PP-5] Wire `VulkanTemporalAAFilter`** (`r_temporalAA`) — two-part: (1) call `temporalAAFilter->GetProjectionMatrixJitter()` **before** scene rendering to offset the projection matrix; (2) call `temporalAAFilter->Filter(cmd, currentInput, currentOutput, useFxaa)` in post chain and swap. Jitter setup must happen in camera setup, not inside `RecordCommandBuffer`.
  - Files: `VulkanRenderer.cpp`, `VulkanTemporalAAFilter.h`

- [ ] **[PP-6] Wire `VulkanLensFlareFilter`** — two-part: (1) call `lensFlareFilter->Draw(cmd, direction, reflections, color, infinityDistance)` per light source during scene pass (find sun/light direction from `sceneDef`); (2) call `lensFlareFilter->Filter(cmd, currentInput, currentOutput)` in post chain and swap.
  - Files: `VulkanRenderer.cpp`, `VulkanLensFlareFilter.h`

- [ ] **[PP-7] Wire `VulkanColorCorrectionFilter`** (`r_colorCorrection`) — near end of chain (after tone mapping). Call `colorCorrectionFilter->Filter(cmd, currentInput, currentOutput, tint, fogLuminance)`; `tint`/`fogLuminance` from fog color and scene params. Swap. Confirm whether gamma correction is embedded in this filter or needs a separate step.
  - Files: `VulkanRenderer.cpp`, `VulkanColorCorrectionFilter.h`

- [ ] **[PP-8] Wire `VulkanFXAAFilter`** (`r_fxaa`) — run last (or before bicubic resample). If `r_fxaa` and TAA not active: call `fxaaFilter->Filter(cmd, currentInput, currentOutput)`. Swap. Filter handles its own descriptor set allocation and framebuffer per call.
  - Files: `VulkanRenderer.cpp`, `VulkanFXAAFilter.h`

- [ ] **[PP-9] Investigate and wire Gamma Correction, Camera Blur (`r_cameraBlur`), Bicubic Resample (`r_scaleFilter`)** — no dedicated filter classes found for these three. For each: if a shader exists but no class, write the class following `VulkanPostProcessFilter` pattern; if nothing exists, stub a passthrough and file a sub-TODO. Bicubic resample applies only when render resolution ≠ swapchain resolution and must be the final step before the blit.
  - Files: search `Sources/Draw/Vulkan/` for `VulkanGamma*`, `VulkanCameraBlur*`, `VulkanBicubic*`; `VulkanRenderer.cpp`

## Bugs

- [ ] **[BUG-3] Player self-shadow missing when looking at ground** — not yet investigated.
  - **Needs investigation**: how shadow maps are generated; whether the local player model is excluded from the shadow caster pass (as in some engines where the first-person model is a special case); which shader samples the shadow map and whether there is a self-shadowing bias issue.
  - Files: `Sources/Draw/Vulkan/VulkanRenderer.cpp` (shadow pass), `Sources/Draw/Vulkan/VulkanModelRenderer.cpp` (shadow casting)
  All player shadow in fact are missing.

## Medium

- [ ] Implement `ReadBitmap()`. Currently returns a null handle (`VulkanRenderer.cpp:1156–1167`). Requires a staging buffer, `vkCmdCopyImageToBuffer`, and a synchronous map/read sequence.

- [x] Honor `sceneDef.skipWorld`. The Vulkan `RecordCommandBuffer` at lines 1440–1462 always renders the map. Add the `if (!sceneDef.skipWorld && mapRenderer)` guard present in the GL renderer, used for main menu and loading screens.

- [ ] Wire SSAO. `VulkanSSAOFilter` is not instantiated in `VulkanRenderer`. The GL renderer triggers a depth prepass and binds an occlusion texture during the sunlight pass. Vulkan needs a depth prepass and the SSAO texture bound to model/map shaders.