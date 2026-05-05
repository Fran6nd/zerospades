# Vulkan Renderer — TODO

Base class for filters: [VulkanPostProcessFilter.h](VulkanPostProcessFilter.h).
GL renderer in `Sources/Draw/OpenGL/` is the reference for everything below.

## Anti-aliasing gap

The most visible difference vs OpenGL today: distant geometry edges look
rough/aliased because the Vulkan path has **no AA at all**.

- [x] **FXAA** — ported in [VulkanFXAAFilter.cpp](VulkanFXAAFilter.cpp); gated on `r_fxaa`.
- [ ] **MSAA** — OpenGL respects `r_multisamples`. Vulkan hardcodes
      `VK_SAMPLE_COUNT_1_BIT` at every render-pass attachment, framebuffer
      image, and pipeline `rasterizationSamples` site. Wiring this up
      requires a multisampled color/depth attachment, a resolve attachment,
      and `rasterizationSamples` plumbed through every pipeline.
- [ ] **Temporal AA** — not ported.

## Progressive lighting gap

Without these the world looks like it "pops in" because new chunks arrive
with their final flat shading on frame 0; OpenGL refines block-level
lighting over many frames so geometry visually settles. Affects map AND
voxel models — both currently use the "null radiosity" hemisphere fallback
in [BasicMap.vert:60-64](../../../Resources/Shaders/Vulkan/BasicMap.vert) and
[BasicModelVertexColor.vert:60-64](../../../Resources/Shaders/Vulkan/BasicModelVertexColor.vert).

- [ ] **Ambient Shadow Renderer** — port
      [GLAmbientShadowRenderer.cpp](../OpenGL/GLAmbientShadowRenderer.cpp).
      Single 3D AO texture, updated incrementally each frame. Plumb the
      sampler into `BasicMap.frag` and the model frag shaders to replace
      the hard-coded hemisphere ambient. Strongest single fix.
- [ ] **Radiosity Renderer** — port
      [GLRadiosityRenderer.cpp](../OpenGL/GLRadiosityRenderer.cpp).
      Four 3D textures (`flat`, `X`, `Y`, `Z`) + per-block update queue.
      Bigger lift; depends on AO above for natural integration.

## Post-processing filters

Auto Exposure, Bloom, Fog and Depth of Field are done.

Still to port:

- [ ] Lens Flare
- [ ] Color Correction
- [ ] SSAO — depends on the AO texture from Ambient Shadow Renderer above.
- [ ] Gamma Correction
- [ ] Camera Blur
- [ ] Bicubic Resample

## Bugs

- [ ] Player shadows are missing — models reach the shadow pass but never show up. Probably a missing per-instance push constant in [VulkanOptimizedVoxelModel.cpp](VulkanOptimizedVoxelModel.cpp) `RenderShadowMapPass`, or a bad bias.

## Stubs to flesh out

- [ ] [VulkanWaterRenderer.cpp](VulkanWaterRenderer.cpp) `RenderDynamicLightPass` reuses the sunlight pipeline as a placeholder — water doesn't react to dynamic lights.
- [ ] [VulkanOptimizedVoxelModel.cpp](VulkanOptimizedVoxelModel.cpp) `PreloadShaders` is empty — pipelines build on first use, so the first frame stutters.
- [ ] [VulkanMapRenderer.cpp](VulkanMapRenderer.cpp) `PreloadShaders` is empty — same story, on map chunk pipelines.
