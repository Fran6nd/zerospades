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
- [ ] **Fog is broken** (ZeroG) — currently the highest-priority visual bug. Compare against GL fog math + uniforms.
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
