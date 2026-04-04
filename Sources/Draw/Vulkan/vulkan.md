# Vulkan Renderer

## Shader Compilation

Vulkan shaders are written in GLSL and live under `Resources/Shaders/Vulkan/`. They are compiled to SPIR-V at build time by `glslangValidator` (located via `find_program` in `CMakeLists.txt`). Each source file (`.vert`, `.frag`, `.vk.vs`, `.vk.fs`) produces a `.spv` file next to it in the source tree.

- **Build:** `make` (or the CMake build target `compile_vulkan_shaders`) compiles all shaders automatically before the pak files are assembled.
- **Clean:** `make clean` removes all `.spv` files. They are registered as `ADDITIONAL_CLEAN_FILES` on the `compile_vulkan_shaders` target in `Resources/CMakeLists.txt`.
- **Git:** `.spv` files are gitignored (`*.spv` in `.gitignore`). Never commit them.

---

## Cvar Naming Convention

All Vulkan-specific cvars use the `r_vk_` prefix (e.g. `r_vk_bloom`, `r_vk_hdr`). This keeps Vulkan settings fully independent from the OpenGL renderer, which uses the plain `r_` prefix (e.g. `r_bloom`, `r_hdr`). Changing a setting in the Vulkan setup tab has no effect on the OpenGL renderer and vice versa.

The prefix applies to every renderer-specific cvar: quality toggles, post-process filters, MSAA, framebuffer format, shadow map size, and HDR tuning parameters. The only shared cvars are windowing-level settings that are renderer-agnostic (`r_vsync`, `r_renderer`).

---

## Supported Features

| Feature | Cvar | Notes |
|---------|------|-------|
| Basic scene rendering (map, models, sprites) | — | Fully functional |
| Cascaded shadow maps | — | 3 cascade levels |
| Dynamic lighting | `r_vk_dlights` | Point, spot, and linear lights |
| Auto-exposure (HDR) | `r_vk_hdr` | 4-pass: preprocess → downsample → gain → apply |
| Bloom | `r_vk_bloom` | 6-level downsample / Gaussian blur / composite |
| Volumetric fog | `r_vk_fogShadow` | Levels 1 and 2 are currently identical (see below) |
| Depth of field | `r_vk_depthOfField` | CoC generation, separable blur, hexagonal bokeh |
| Color correction | `r_vk_colorCorrection` | Tint, saturation, ACES tone mapping, temporal fog smoothing |
| FXAA | `r_vk_fxaa` | Disabled automatically when MSAA is active |
| SSAO | `r_vk_ssao` | HBAO with bilateral filter; disabled when MSAA is active |
| Soft particles | `r_vk_softParticles` | Levels 1 and 2 are currently identical (see below) |
| MSAA | `r_vk_multisamples` | 2× and 4×; requires restart; mutually exclusive with FXAA and SSAO |
| Physically-based lighting | `r_vk_physicalLighting` | Per-fragment energy-conserving shading |
| Model outlines | `r_vk_outlines` | Silhouette pass |
| Screenshot capture | — | GPU→CPU readback via staging buffer |
| `skipWorld` rendering | — | Guard in `RecordCommandBuffer` |
| Post-process ping-pong chain | — | `temporaryImagePool`-backed, all filters wired |

---

## Unsupported Features

These features are present in the OpenGL renderer but not yet implemented in the Vulkan renderer.

| Feature | Cvar(s) |
|---------|---------|
| Water rendering | `r_vk_water` |
| Temporal AA | `r_vk_temporalAA` |
| Lens flare | `r_vk_lensFlare`, `r_vk_lensFlareDynamic` | 
| Camera motion blur | `r_vk_cameraBlur` |
| Gamma correction filter | — |
| Bicubic resampling | `r_vk_scaleFilter` |
| Radiosity / global illumination | `r_radiosity` (GL only) |

---

## Known Limitations

- **`r_vk_fogShadow` level 2 = level 1** — The GL renderer uses a radiosity ambient lookup for level 2. The Vulkan renderer applies the same shadow-fog path for both values.
- **`r_vk_softParticles` level 2 = level 1** — The GL renderer adds a Gaussian depth-fade for level 2. The Vulkan renderer uses a linear depth-fade for both values.
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

- **Temporal AA (`r_vk_temporalAA`)** — Port `OpenGL/PostFilters/TemporalAA.fs/.vs` to Vulkan GLSL. Implement `VulkanTemporalAAFilter`: (1) call `GetProjectionMatrixJitter()` before scene rendering; (2) call `Filter()` in the post chain. Wire after color correction, before FXAA. When TAA is active, FXAA should be skipped.

- **Lens flare (`r_vk_lensFlare`, `r_vk_lensFlareDynamic`)** — Port `OpenGL/PostFilters/Lens.fs/.vs` and `LensDust.fs/.vs`. Implement `VulkanLensFlareFilter`: (1) call `Draw()` per sun/light during the scene pass; (2) call `Filter()` in the post chain.

- **Camera blur (`r_vk_cameraBlur`)** — Port `OpenGL/PostFilters/CameraBlur.fs/.vs`. Implement `VulkanCameraBlurFilter`; wire in post chain when active.

- **Gamma correction** — Port `OpenGL/PostFilters/GammaMix.fs/.vs`. Implement `VulkanGammaFilter` (equivalent to `GLNonlinearizeFilter`); wire after color correction.

- **Bicubic resampling (`r_vk_scaleFilter`)** — Port `OpenGL/PostFilters/ResampleBicubic.fs/.vs`. Implement `VulkanBicubicResampleFilter`; wire as the final step when render resolution ≠ display resolution.

### Cvar quality levels

- **`r_vk_fogShadow` level 2** — Implement the GI ambient lookup from `GLFogFilter2` to match the GL renderer's level-2 quality.
- **`r_vk_softParticles` level 2** — Implement the Gaussian depth-fade from the GL renderer's level-2 path.
