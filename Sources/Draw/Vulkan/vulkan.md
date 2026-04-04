# Vulkan Renderer

## Supported Features

| Feature | Cvar | Notes |
|---------|------|-------|
| Basic scene rendering (map, models, sprites) | — | Fully functional |
| Cascaded shadow maps | — | 3 cascade levels |
| Dynamic lighting | `r_dlights` | Point, spot, and linear lights |
| Auto-exposure (HDR) | `r_hdr` | 4-pass: preprocess → downsample → gain → apply |
| Bloom | `r_bloom` | 6-level downsample / Gaussian blur / composite |
| Volumetric fog | `r_fogShadow` | Levels 1 and 2 are currently identical (see below) |
| Depth of field | `r_depthOfField` | CoC generation, separable blur, hexagonal bokeh |
| Color correction | `r_colorCorrection` | Tint, saturation, ACES tone mapping, temporal fog smoothing |
| FXAA | `r_fxaa` | Disabled automatically when MSAA is active |
| SSAO | `r_ssao` | HBAO with bilateral filter; disabled when MSAA is active |
| Soft particles | `r_softParticles` | Levels 1 and 2 are currently identical (see below) |
| MSAA | `r_multisamples` | 2× and 4×; requires restart; mutually exclusive with FXAA and SSAO |
| Physically-based lighting | `r_physicalLighting` | Per-fragment energy-conserving shading |
| Model outlines | `r_outlines` | Silhouette pass |
| Screenshot capture | — | GPU→CPU readback via staging buffer |
| `skipWorld` rendering | — | Guard in `RecordCommandBuffer` |
| Post-process ping-pong chain | — | `temporaryImagePool`-backed, all filters wired |

---

## Unsupported Features

These features are present in the OpenGL renderer but not yet implemented in the Vulkan renderer.

| Feature | Cvar(s) | Status |
|---------|---------|--------|
| Water rendering | `r_water` | Deferred. `VulkanWaterRenderer` was removed from this branch. Mirror pass, refraction copy, and screen-copy images in `VulkanFramebufferManager` need to be restored from git history. |
| Temporal AA | `r_temporalAA` | Not started. Requires projection-matrix jitter before scene render and a history-buffer accumulation pass after. |
| Lens flare | `r_lensFlare`, `r_lensFlareDynamic` | Not started. Requires a scanner pass during the scene pass and a composite pass in the post chain. |
| Camera motion blur | `r_cameraBlur` | Not started. |
| Gamma correction filter | — | Not started. Equivalent to GL `GLNonlinearizeFilter`. |
| Bicubic resampling | `r_scaleFilter` | Not started. Only relevant when render resolution differs from display resolution. |
| Radiosity / global illumination | `r_radiosity` | GL-only feature. No Vulkan implementation planned in the near term. |

---

## Known Limitations

- **`r_fogShadow` level 2 = level 1** — The GL renderer uses a radiosity ambient lookup for level 2. The Vulkan renderer applies the same shadow-fog path for both values.
- **`r_softParticles` level 2 = level 1** — The GL renderer adds a Gaussian depth-fade for level 2. The Vulkan renderer uses a linear depth-fade for both values.
- **Shader preloading is stubbed** — `VulkanOptimizedVoxelModel::PreloadShaders()` and `VulkanMapRenderer::PreloadShaders()` are empty. Pipelines compile on first use, causing a stutter on the first rendered frame.

---

## Known Bugs

### BUG-1 — All player (model) shadows missing

Player models and other dynamic objects cast no shadow. The shadow map infrastructure (`VulkanShadowMapRenderer`) is present, but models do not appear in the shadow depth pass.

**Investigation needed:**
- Confirm whether `VulkanModelRenderer` submits models to the shadow caster pass.
- Verify shadow map sampling in `BasicModel.frag` / `BasicModelVertexColor*.frag` (check that the shadow texture binding and depth-comparison sampler are correct).
- Check self-shadow depth bias — an incorrect bias can cause shadows to be clipped entirely.

**Files:** [VulkanRenderer.cpp](VulkanRenderer.cpp) (shadow pass), [VulkanModelRenderer.cpp](VulkanModelRenderer.cpp), [VulkanShadowMapRenderer.cpp](VulkanShadowMapRenderer.cpp)

---

## Remaining Work

Items are ordered roughly by priority.

### High priority

- **[BUG-1] Investigate and fix missing model shadows** — see bug section above.

- **[STUB] Implement shader preloading** — both `VulkanOptimizedVoxelModel::PreloadShaders()` (`VulkanOptimizedVoxelModel.cpp:44`) and `VulkanMapRenderer::PreloadShaders()` (`VulkanMapRenderer.cpp:126`) are empty stubs. Build all pipeline variants eagerly at startup to eliminate first-frame stutter.

### Post-processing

- **Temporal AA (`r_temporalAA`)** — Port `OpenGL/PostFilters/TemporalAA.fs/.vs` to Vulkan GLSL. Implement `VulkanTemporalAAFilter`: (1) call `GetProjectionMatrixJitter()` before scene rendering; (2) call `Filter()` in the post chain. Wire after color correction, before FXAA. When TAA is active, FXAA should be skipped.

- **Lens flare (`r_lensFlare`, `r_lensFlareDynamic`)** — Port `OpenGL/PostFilters/Lens.fs/.vs` and `LensDust.fs/.vs`. Implement `VulkanLensFlareFilter`: (1) call `Draw()` per sun/light during the scene pass; (2) call `Filter()` in the post chain.

- **Camera blur (`r_cameraBlur`)** — Port `OpenGL/PostFilters/CameraBlur.fs/.vs`. Implement `VulkanCameraBlurFilter`; wire in post chain when active.

- **Gamma correction** — Port `OpenGL/PostFilters/GammaMix.fs/.vs`. Implement `VulkanGammaFilter` (equivalent to `GLNonlinearizeFilter`); wire after color correction.

- **Bicubic resampling (`r_scaleFilter`)** — Port `OpenGL/PostFilters/ResampleBicubic.fs/.vs`. Implement `VulkanBicubicResampleFilter`; wire as the final step when render resolution ≠ display resolution.

### Deferred features

- **Water renderer (`r_water` 0–3)** — Restore `VulkanWaterRenderer` from git history. Restore the mirror framebuffer and screen-copy images in `VulkanFramebufferManager`, mirror render pass, refraction copy blocks in `RecordCommandBuffer`, and the `r_water > 0` guard in `VulkanMapChunk::IsSolid`. Re-enable water presets in the setup screen's Shader Effects tab.

### Cvar quality levels

- **`r_fogShadow` level 2** — Implement the GI ambient lookup from `GLFogFilter2` to match the GL renderer's level-2 quality.
- **`r_softParticles` level 2** — Implement the Gaussian depth-fade from the GL renderer's level-2 path.
