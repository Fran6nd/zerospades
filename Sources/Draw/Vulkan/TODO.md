# Vulkan Renderer — TODO

Base class for filters: [VulkanPostProcessFilter.h](VulkanPostProcessFilter.h).
GL renderer in `Sources/Draw/OpenGL/` is the reference for everything below.

## Anti-aliasing gap

The most visible difference vs OpenGL today: distant geometry edges look
rough/aliased because the Vulkan path has **no AA at all** beyond FXAA.

- [ ] **MSAA** — OpenGL respects `r_multisamples`. Vulkan hardcodes
      `VK_SAMPLE_COUNT_1_BIT` at every render-pass attachment, framebuffer
      image, and pipeline `rasterizationSamples` site. Wiring this up
      requires a multisampled color/depth attachment, a resolve attachment,
      and `rasterizationSamples` plumbed through every pipeline.
- [ ] **Temporal AA** — not ported.

## Post-processing filters

Auto Exposure, Bloom, Fog, Depth of Field, FXAA and Cavity Outline are done.

Still to port:

- [ ] Lens Flare
- [ ] Color Correction
- [ ] SSAO — depends on the AO texture from Ambient Shadow Renderer.
- [ ] Gamma Correction
- [ ] Camera Blur
- [ ] Bicubic Resample

## Bugs

- [ ] Player shadows are missing — models reach the shadow pass but never show up. Probably a missing per-instance push constant in [VulkanOptimizedVoxelModel.cpp](VulkanOptimizedVoxelModel.cpp) `RenderShadowMapPass`, or a bad bias.
- [ ] **Fog parity follow-ups** — directional shadow shafts now render
      (Fog1 variant selected correctly, DDA steps bumped, solid pass
      fades to black, Sky.frag suppressed when Fog1 is active,
      ReadBitmap mirrors the post-process result). Remaining gaps
      documented in `## Fog / sky parity follow-ups` below.
- [ ] **Screenshots flipped vertically** — Vulkan-native NDC y-flip vs SDL/PNG row order. There is now a manual flip in the readback path ([VulkanRenderer.cpp:755](VulkanRenderer.cpp#L755)); confirm it produces correctly-oriented output before closing this item.
- [ ] **Mirrored model culling uses `VK_CULL_MODE_FRONT_BIT`** to flip handedness on negative-scale model matrices (e.g. [VulkanOptimizedVoxelModel.cpp:1055](VulkanOptimizedVoxelModel.cpp#L1055) and similar mirrored pipeline variants). GL flips winding order via `glFrontFace(GL_CCW)`; Vulkan equivalent is toggling `VkPipelineRasterizationStateCreateInfo::frontFace` between `CW` / `COUNTER_CLOCKWISE`, not swapping cull mode. Current approach silently culls visible faces in pathological cases. Refactor to flip winding instead.
- [ ] **PBR (`BasicMapPhys` / `BasicModelVertexColorPhys`) glitch** — visible artifact distinct from AA aliasing. Reproduce with `r_fxaa = 0` and `r_multisamples = 0` before debugging so AA isn't a confound.

## Stubs to flesh out

- [ ] [VulkanWaterRenderer.cpp](VulkanWaterRenderer.cpp) `RenderDynamicLightPass` reuses the sunlight pipeline as a placeholder — water doesn't react to dynamic lights.
- [ ] [VulkanOptimizedVoxelModel.cpp](VulkanOptimizedVoxelModel.cpp) `PreloadShaders` is empty — pipelines build on first use, so the first frame stutters.
- [ ] [VulkanMapRenderer.cpp](VulkanMapRenderer.cpp) `PreloadShaders` is empty — same story, on map chunk pipelines.

## Outline tuning (future work)

The cavity threshold (`thresholdsScale.x` in
[VulkanCavityOutlineFilter.cpp](VulkanCavityOutlineFilter.cpp)) and
edge strength (`invViewportFog.w`) are currently constants. Promote
to `r_outlinesDepthThreshold` / `r_outlinesStrength` cvars once the
defaults are confirmed across maps. A second tap pattern using
reconstructed-normal differences (instead of depth-only) would
catch interior creases on coplanar voxel arrangements; not needed
for the current voxel geometry, where cardinal neighbours always
straddle a depth jump at any visually meaningful edge.

## Performance / optimization

### Memory and resource management

- [ ] **Expand transient render-target aliasing** — [VulkanTemporaryImagePool](VulkanTemporaryImagePool.h) currently backs Bloom and DoF intermediates; render targets that aren't used simultaneously could share the same allocation more aggressively.

### Pipeline

- [ ] **Pipeline derivatives** — Water/Water2/Water3 shaders share most state and are good candidates.
- [ ] **Specialization constants** — replace runtime conditionals in shaders with specialization constants for better optimization.

### Render passes

- [ ] **Merge compatible render passes into subpasses** where attachments allow it.
- [ ] **Audit load/store ops** — flag `LOAD_OP_LOAD` where `DONT_CARE` would suffice and `STORE_OP_STORE` where `DONT_CARE` is acceptable.

### Draw calls

- [ ] **Indirect drawing** — `vkCmdDrawIndirect` for terrain/world to cut CPU overhead.
- [ ] **Instancing** for repeated similar objects.
- [ ] **GPU culling** via compute shaders instead of CPU-side frustum culling.

### Texture streaming

- [ ] **Sparse textures** — `VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT` for large virtual textures.
- [ ] **Async texture uploads** on the transfer queue to overlap with graphics work.

## Fog / sky parity follow-ups

Surfaced while restoring directional shadow shafts (`r_fogShadow:2`,
`r_radiosity:0`) on 2026-05-27. The audit settings deliberately
exercised the minimum-feature path; everything below was left alone
because the active config doesn't reach it.

### `GetFogColor()` vs `GetFogColorForSolidPass()`

GL uses `GetFogColorForSolidPass()` in every opaque-scene pass so
distant geometry fades to BLACK when `r_fogShadow` is on, and the
fog post-process can re-add the in-scattered light (so the
directional shadow shaft is visible). Fixed for
`VulkanMapChunk::RenderSunlightPass` and
`VulkanOptimizedVoxelModel::RenderSunlightPass`. Still using the
unconditional `GetFogColor()`:

- [ ] [VulkanLongSpriteRenderer.cpp:413](VulkanLongSpriteRenderer.cpp#L413)
      — tracers / long sprites.
- [ ] [VulkanSpriteRenderer.cpp:476](VulkanSpriteRenderer.cpp#L476)
      — generic sprites (gibs, particles).
- [ ] [VulkanWaterRenderer.cpp:1384](VulkanWaterRenderer.cpp#L1384)
      — water surface (only active under `r_water > 0`).
- [ ] [VulkanOptimizedVoxelModel.cpp:430](VulkanOptimizedVoxelModel.cpp#L430)
      — `Prerender` (depth-only, no colour write — currently harmless
      but inconsistent).

`VulkanFogFilter.cpp` and `VulkanColorCorrectionFilter.cpp` both
call `GetFogColor()` for internal scale derivations, NOT for
scene-pass fading — those are correct as-is.

### `BasicMap.frag` (non-physical) shader differences

[Resources/Shaders/Vulkan/BasicMap.frag](../../../Resources/Shaders/Vulkan/BasicMap.frag)
ports `BasicBlock.fs` + the `*shadow*` permutation but doesn't honour
`r_radiosity` the way GL does:

- [ ] Always samples four 3D radiosity textures (`radiosityTextureFlat/X/Y/Z`)
      even when `r_radiosity == 0`. GL selects `MapRadiosityNull.fs`
      in that case and computes ambient purely from
      `mix(fogColor, vec3(1.0), 0.5) * 0.5 * ao * hemisphere`.
- [ ] Uses the 3D `ambientShadowTexture` (the volumetric AO used by
      Fog2), whereas GL `BasicBlock.fs` reads the per-chunk 2D
      `ambientOcclusionTexture` baked into vertex attributes. The
      3D AO gives flatter detail at voxel corners — visible on
      tree foliage and block crevices.
- [ ] Missing terminal gamma encoding. GL `BasicBlock.fs:47` does
      `sqrt(gl_FragColor.xyz)` when `!LINEAR_FRAMEBUFFER`. Vulkan's
      offscreen colour is `A2B10G10R10_UNORM` (linear) when
      `r_highPrec:1`, so no encode is needed *here* — but if the
      offscreen format ever falls back to `R8G8B8A8_UNORM` the
      linear-light values would be stored as if sRGB and the scene
      would come out ~2× too bright. Either branch on the FB format
      or always render to a linear-precision FB.

Cleanest port is two `BasicMap` permutations selected by
`r_radiosity`, matching the `*shadow*` macro expansion in
`GLShadowShader::RegisterShader`.

### Other items

- [ ] **Fog2 in-scatter is dimmer than GL.** Documented at the
      `RenderSky` call site in [VulkanRenderer.cpp](VulkanRenderer.cpp):
      Vulkan Fog2 on a black clear leaves a near-black sky, so the
      `Sky.frag` flat fog-colour fill is still drawn under the Fog2
      path as a workaround. Once Fog2's push-constant scales /
      integration curve match GL, drop the workaround so Fog2 also
      goes through the GL-equivalent code path
      (no flat sky → post-process paints everything).
- [ ] **Fog1 has 512-step uncached DDA.** Matches the GL non-coarse
      fallback. GL itself uses a coarse+fine traversal (`USE_COARSE_SHADOWMAP`,
      8×8 mip min/max shadow map) for the same visual result at a
      fraction of the per-pixel cost. Port if it shows up in profiling.
- [ ] **Fog filter view ray breaks down looking straight down.**
      Reported visible glitch when the camera is at top-centre of the
      map looking perfectly down. Likely the `if (length(dir.xy) <
      0.0001) dir.xy = vec2(0.0001)` guard in
      [Fog.vk.fs](../../../Resources/Shaders/Vulkan/PostFilters/Fog.vk.fs)
      / [Fog.vk.vs](../../../Resources/Shaders/Vulkan/PostFilters/Fog.vk.vs)
      producing a degenerate shadow ray.
- [ ] **`VulkanMapShadowRenderer::Update` re-uploads the entire 512×512
      shadow bitmap on any change.** GL does the equivalent of
      `glTexSubImage2D` over only the touched rows. Not a correctness
      issue, but a perf cliff in build-heavy games.
- [ ] **Committed `.spv` files drift from the GLSL.** The CMake build
      regenerates them on every change, so the checked-in copies
      become misleading.
