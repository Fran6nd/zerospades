# Vulkan Renderer — TODO

Base class for filters: [VulkanPostProcessFilter.h](VulkanPostProcessFilter.h).
GL renderer in `Sources/Draw/OpenGL/` is the reference for everything below.

## Anti-aliasing gap

The most visible difference vs OpenGL: distant geometry edges look
rough/aliased because the Vulkan path has **no AA at all** beyond FXAA.

- [x] **MSAA** — `r_multisamples` honoured (clamped to the device's usable
      sample count). Scene colour/depth render multisampled; colour is resolved
      with `vkCmdResolveImage` and depth with `VulkanDepthResolveFilter`
      (`sampler2DMS` → R32F) before post-processing.
      - [x] **Water + MSAA** — refraction (`CopySceneForWaterSampling`) and
            reflection (`CopyToMirrorImage` / `ResolveMirrorColor` + the mirror
            depth resolve) now resolve the multisampled scene/mirror into the
            single-sample images the water shader samples (`GetWater*()`); water
            works at every `r_water` level under MSAA, and its surface is AA'd.
      - [x] **Soft particles + MSAA** — read the resolved depth (resolved once
            after the opaque pass and reused everywhere).
      - [x] **Setup-menu capability** — the startup Vulkan probe reports the
            device's max usable MSAA level and `CheckConfigCapability` greys out
            unsupported `r_multisamples` options (Vulkan only); the renderer
            clamps down at runtime as a backstop. No feature *incompatibilities*
            remain.
- [ ] **Temporal AA** — `GLTemporalAAFilter` not ported.

## Post-processing filters

Wired into [VulkanRenderer.cpp](VulkanRenderer.cpp) pp-chain:
Fog → DoF → Bloom → FXAA → LensFlare → AutoExposure → ColorCorrection
→ CavityOutline.

| GL filter | Vulkan equivalent | Status |
|---|---|---|
| `GLAutoExposureFilter` | `VulkanAutoExposureFilter` | wired (`r_hdr`) |
| `GLBloomFilter` | — | dead code in GL (instantiated nowhere) |
| `GLLensDustFilter` (the real `r_bloom`) | `VulkanBloomFilter` | wired but simplified — no dust texture / noise overlay |
| `GLCameraBlurFilter` | — | **missing** (`r_cameraBlur`) |
| `GLColorCorrectionFilter` | `VulkanColorCorrectionFilter` | wired (`r_colorCorrection`) |
| `GLDepthOfFieldFilter` | `VulkanDepthOfFieldFilter` | wired (`r_depthOfField`) |
| `GLFXAAFilter` | `VulkanFXAAFilter` | wired (`r_fxaa`) |
| `GLFogFilter` / `GLFogFilter2` | `VulkanFogFilter` | wired (`r_fogShadow`) — see follow-ups below |
| `GLLensFilter` | — | dead code in GL (unused) |
| `GLLensFlareFilter` | `VulkanLensFlareFilter` | wired sun path (`r_lensFlare`); **`r_lensFlareDynamic` per-light flares missing** |
| `GLNonlinearizeFilter` | — | not needed — sRGB swapchain blit encodes for display |
| `GLResampleBicubicFilter` | — | **missing** (`r_scaleFilter == 2`) |
| `GLSSAOFilter` | — | **missing** (`r_ssao`) |
| `GLTemporalAAFilter` | — | **missing** (see AA gap above) |
| (n/a — cavity is Vulkan-only) | `VulkanCavityOutlineFilter` | wired (`r_outlines`) |

## Bugs

- [x] **Player/grenade shadows (ground).** Dynamic models cast sun shadows
      onto the terrain. Pipeline:
      - Models render into a dedicated, models-only cascaded shadow map
        (`CreateShadowPipeline` + [ModelShadowMap.vert]) pushing the full
        per-instance light-space MVP (origin baked in). The map-chunk shadow
        pipeline couldn't be reused (chunk stride 20 B vs model `sizeof(Vertex)`;
        translation-only `vec3` push). Gated on `r_modelShadows`.
      - `BuildMatrix` rebuilt to a sun-aligned frustum-fit ortho (ported from
        `GLBasicShadowMapRenderer`) emitting Vulkan [0,1] Z, with the Z **flipped**
        so 0 = sun side (z is down in this engine, so the `LESS` test keeps the
        occluder nearest the sun).
      - Always-present sampling descriptor set on the shadow renderer: per-frame
        cascade-matrix UBO (+ `enabled` flag) + the 3 D32 depth maps + sampler.
        `enabled=0` when `r_fogShadow` off → shader returns lit.
      - `BasicMap.frag` `EvaluteModelShadow()` (first-in-bounds cascade →
        compare) multiplied into the sun term, matching the dormant
        `Shadow/Common.vk.fs` design.

      Three real bugs were fixed along the way: the shadow depth images had a
      COLOR-aspect view (unsamplable — `CreateImageView(VK_IMAGE_ASPECT_DEPTH_BIT)`
      now); the shadow render pass lacked an outgoing write→sample dependency;
      and the map-chunk-inherited depth bias caused a lateral offset (now 0 —
      no self-shadowing in the models-only cascade).

      Remaining (optional polish):
      - [ ] **Model self-shadowing** — wire the same cascade sampling into
            [BasicModelVertexColor.vert/frag] so models are shaded by other
            models (they already receive terrain shadows via `mapShadowTexture`).
      - [ ] **Phys lit variants** — `BasicMapPhys`, `BasicModelVertexColorPhys`
            (only active under `r_physicalLighting`).

## Stubs

- [ ] [VulkanMapRenderer.cpp:126](VulkanMapRenderer.cpp#L126)
      `PreloadShaders` is empty — first frame stutters as map pipelines build.
- [ ] [VulkanOptimizedVoxelModel.cpp:46](VulkanOptimizedVoxelModel.cpp#L46)
      `PreloadShaders` is empty — same story for model pipelines.
- [ ] [VulkanWaterRenderer.cpp:1067](VulkanWaterRenderer.cpp#L1067)
      `RenderDynamicLightPass` reuses the sunlight pipeline as a
      placeholder — water doesn't react to dynamic lights.

## Fog / sky parity follow-ups

`VulkanFogFilter` covers Fog1 and Fog2; the items below are the
remaining deltas vs GL.

### Scene passes still calling `GetFogColor()` instead of `GetFogColorForSolidPass()`

GL fades opaque geometry to BLACK when `r_fogShadow` is on so the
post-process can re-add in-scattered light. Done for
`VulkanMapChunk::RenderSunlightPass`,
`VulkanOptimizedVoxelModel::RenderSunlightPass` and
`VulkanOptimizedVoxelModel::Prerender`.

Tracers and sprites/particles intentionally keep `GetFogColor()`: GL's
`GLLongSpriteRenderer`, `GLSpriteRenderer` and `GLSoftSpriteRenderer`
all use the non-solid `GetFogColor()`, so the Vulkan code already
matches — switching them to the solid-pass colour would *diverge* from
GL (fade tracers/particles to black under `r_fogShadow`).

### `BasicMap.frag` (non-physical lighting)

- [ ] Missing terminal gamma encoding. Harmless under
      `A2B10G10R10_UNORM` (`r_highPrec=1`); if the offscreen format
      ever falls back to `R8G8B8A8_UNORM` the linear values would
      display ~2× too bright. Either branch on FB format or always
      render to a linear-precision FB.

### Other

- [ ] **Fog2 in-scatter dimmer than GL.** The flat `Sky.frag` fog-colour
      fill is still drawn under Fog2 as a workaround. Drop once Fog2's
      push-constant scales / integration curve match GL.
- [ ] **Fog filter view ray glitches looking straight down.** Likely
      degenerate `dir.xy` from the
      [Fog.vk.fs](../../../Resources/Shaders/Vulkan/PostFilters/Fog.vk.fs)
      / [Fog.vk.vs](../../../Resources/Shaders/Vulkan/PostFilters/Fog.vk.vs)
      `length(dir.xy) < 0.0001` guard.
- [ ] **`VulkanMapShadowRenderer::Update` re-uploads the full 512×512
      bitmap on any change.** GL does a sub-rect upload. Perf cliff in
      build-heavy games.

## Outline tuning (future work)

The cavity threshold and edge strength in
[VulkanCavityOutlineFilter.cpp](VulkanCavityOutlineFilter.cpp) are
constants — promote to `r_outlinesDepthThreshold` /
`r_outlinesStrength` once defaults are confirmed across maps.

## Performance / optimization

### Memory

- [ ] **Expand transient render-target aliasing** —
      [VulkanTemporaryImagePool](VulkanTemporaryImagePool.h) currently
      backs Bloom, DoF and LensFlare intermediates; other transient
      targets could share allocations more aggressively.

### Pipeline

- [ ] **Pipeline derivatives** — Water/Water2/Water3 share most state.
- [ ] **Specialization constants** — replace runtime conditionals in
      shaders.

### Render passes

- [ ] **Merge compatible render passes into subpasses.**
- [ ] **Audit load/store ops** — flag `LOAD_OP_LOAD` where `DONT_CARE`
      would suffice and `STORE_OP_STORE` where `DONT_CARE` is acceptable.

### Draw calls

- [ ] **Indirect drawing** (`vkCmdDrawIndirect`) for terrain/world.
- [ ] **Instancing** for repeated objects.
- [ ] **GPU culling** via compute shaders.

### Texture streaming

- [ ] **Sparse textures** (`VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT`).
- [ ] **Async texture uploads** on the transfer queue.

## Build hygiene

- [ ] **Committed `.spv` files drift from the GLSL.** CMake regenerates
      them on every build, so the checked-in copies become misleading.
