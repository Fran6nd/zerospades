# Vulkan Renderer — TODO

Base class for filters: [VulkanPostProcessFilter.h](VulkanPostProcessFilter.h).
GL renderer in `Sources/Draw/OpenGL/` is the reference for everything below.

## Anti-aliasing

MSAA is done (`r_multisamples`, incl. water + soft particles + setup-menu
capability). Remaining:

- [ ] **Temporal AA** — `GLTemporalAAFilter` not ported.

## Post-processing filters

Wired into [VulkanRenderer.cpp](VulkanRenderer.cpp) pp-chain:
Fog → DoF → CameraBlur → Bloom → FXAA → LensFlare → AutoExposure → ColorCorrection
→ CavityOutline.

| GL filter | Vulkan equivalent | Status |
|---|---|---|
| `GLAutoExposureFilter` | `VulkanAutoExposureFilter` | wired (`r_hdr`) |
| `GLBloomFilter` | — | dead code in GL (instantiated nowhere) |
| `GLLensDustFilter` (the real `r_bloom`) | `VulkanBloomFilter` | wired incl. dust texture + per-frame noise grain; remaining delta: GL runs an extra Gauss1D H+V blur on the first downsample level |
| `GLCameraBlurFilter` | `VulkanCameraBlurFilter` | wired (`r_cameraBlur` + `sceneDef.radialBlur`), between DoF and Bloom like GL |
| `GLColorCorrectionFilter` | `VulkanColorCorrectionFilter` | wired (`r_colorCorrection`) |
| `GLDepthOfFieldFilter` | `VulkanDepthOfFieldFilter` | wired (`r_depthOfField`) |
| `GLFXAAFilter` | `VulkanFXAAFilter` | wired (`r_fxaa`) |
| `GLFogFilter` / `GLFogFilter2` | `VulkanFogFilter` | wired (`r_fogShadow`) — see follow-ups below |
| `GLLensFilter` | — | dead code in GL (unused) |
| `GLLensFlareFilter` | `VulkanLensFlareFilter` | wired sun path (`r_lensFlare`) + per-light flares (`r_lensFlareDynamic`, capped at 8 per frame for descriptor budget) |
| `GLNonlinearizeFilter` | — | not needed — sRGB swapchain blit encodes for display |
| `GLResampleBicubicFilter` | `VulkanResampleBicubicFilter` | wired (`r_scaleFilter == 2`); `r_scaleFilter == 0` now also honored via nearest blit |
| `GLSSAOFilter` | — | **missing** (`r_ssao`) — big: needs full map+model depth prepass, mid-frame depth resolve/pass split, and an SSAO sampler binding (= new set layout) in every lit map/model pipeline + shader |
| `GLTemporalAAFilter` | — | **missing** (see AA gap above) |
| (n/a — cavity is Vulkan-only) | `VulkanCavityOutlineFilter` | wired (`r_outlines`) |

## Shadows — follow-ups

Ground model shadows (player / grenade / other-players' weapons) are done:
models render into a models-only cascaded shadow map and the map lit shader
samples it (`BasicMap.frag` `EvaluteModelShadow()`). Remaining polish:

- [ ] **Model self-shadowing** — wire the same cascade sampling into the
      shared `BasicModelVertexColor.vert/frag`. Note that vert/frag is used by
      the sunlight, prerender and both ghost pipelines, so all of them must
      bind the sampling set (set 1) or break. Low value: models already receive
      terrain shadows via `mapShadowTexture`; this only adds model-on-model.
- [ ] **Phys lit variants** — `BasicMapPhys`, `BasicModelVertexColorPhys`
      (only active under `r_physicalLighting`).

## Movable sun — follow-up

`VulkanRenderer::GetSunDirection()` is the single source of truth for the sun.
The shadow projection (`BuildMatrix`), the lens flare, the **map** lambert
(`BasicMap.vert`) and now the **model** lambert (`BasicModelVertexColor.vert` +
Phys vert/frag + ghost variants, via the `ModelSolidPushConstants.sunDirection`
push field) all read it, so changing that one method moves the sun + its shadows
+ ground/model lighting together. Still hardcoded `(0,-1,-1)`:

The **Phys** map lambert (`BasicMapPhys.vert/frag` via
`MapSolidPushConstants.sunDirection`) now reads it too.

- [x] **Water + Fog2** — already wired: `Water/Water2/Water3.vk.fs` read
      `WaterPushConstants.sunDirection` and `Fog2.vk.fs` reads the sun packed
      into the scale `.w` slots, both fed from `GetSunDirection()`. The old
      TODO pointed at `Water.frag`, which is a dead file (only `*.vk.fs` are
      loaded via `Water*.vk.program`).
- [x] Delete dead `Water.frag` / `Water.vert` — removed.

## Stubs

- [x] Map/model `PreloadShaders` — no longer empty; both warm the SPIR-V
      cache. TODO was stale.
- [ ] `PreloadShaders` only preloads SPIR-V blobs, not `VkPipeline`s —
      pipelines still compile lazily on first draw. If first-frame stutter
      persists, pre-create the pipelines (needs render pass compat) or use
      `VK_EXT_graphics_pipeline_library` / warm pipeline cache from disk.
- [x] `VulkanWaterRenderer::RenderDynamicLightPass` — already an
      intentional no-op in the code; GL water has no dynamic light pass
      either, so "no reaction to dynamic lights" *is* parity. TODO was
      stale (claimed it re-drew with the sunlight pipeline).

## Fog / sky parity follow-ups

`VulkanFogFilter` covers Fog1 and Fog2; the items below are the
remaining deltas vs GL.

### `BasicMap.frag` (non-physical lighting)

- [x] Missing terminal gamma encoding — sidestepped: the framebuffer
      manager now prefers `A2B10G10R10_UNORM` whenever the hardware
      supports it (attachment-blend + sampled), even with `r_highPrec=0`.
      `R8G8B8A8_UNORM` remains only as a last-resort fallback on hardware
      without 10-bit render targets.

### Other

- [ ] **Fog2 in-scatter dimmer than GL.** The flat `Sky.frag` fog-colour
      fill is still drawn under Fog2 as a workaround. Drop once Fog2's
      push-constant scales / integration curve match GL. Possibly caused
      by the same depth-read bug fixed for the lens flare scanner (see
      `VulkanFramebufferManager::sceneDepthSampleImage`): Fog2 samples
      the same depth texture, and a D32 depth image read through
      `sampler2D` silently returns 0 on MoltenVK — worth re-checking
      after that fix before spending more time here.
- [x] **Fog filter view ray glitches looking straight down** — fixed:
      degenerate near-vertical rays now early-out (fog integral is ~0
      there anyway) instead of snapping `dir.xy`, which made adjacent
      fragments flip between real and snapped directions.
- [x] **`VulkanMapShadowRenderer::Update` sub-rect upload** — dirty 32-texel
      words coalesce into per-row spans, packed into staging and copied via
      one multi-region `vkCmdCopyBufferToImage`. Falls back to full upload
      when >25% dirty or >256 spans.

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

- [x] **Committed `.spv` files drift from the GLSL** — deleted from the
      tree and gitignored; `glslangValidator` is a hard build requirement
      so CMake always regenerates them.
