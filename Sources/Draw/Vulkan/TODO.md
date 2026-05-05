# Vulkan Renderer — TODO

Base class for filters: [VulkanPostProcessFilter.h](VulkanPostProcessFilter.h).
GL renderer in `Sources/Draw/OpenGL/` is the reference for everything below.

## Anti-aliasing gap

The most visible difference vs OpenGL today: distant geometry edges look
rough/aliased because the Vulkan path has **no AA at all**.

- [ ] **FXAA** — OpenGL applies it by default (`r_fxaa = 1`). Vulkan has no
      filter and no shader. Single fullscreen pass; cheapest first win.
- [ ] **MSAA** — OpenGL respects `r_multisamples`. Vulkan hardcodes
      `VK_SAMPLE_COUNT_1_BIT` at every render-pass attachment, framebuffer
      image, and pipeline `rasterizationSamples` site. Wiring this up
      requires a multisampled color/depth attachment, a resolve attachment,
      and `rasterizationSamples` plumbed through every pipeline.
- [ ] **Temporal AA** — not ported.

## Post-processing filters

Auto Exposure, Bloom, Fog and Depth of Field are done.

Still to port:

- [ ] Lens Flare
- [ ] Color Correction
- [ ] SSAO
- [ ] Gamma Correction
- [ ] Camera Blur
- [ ] Bicubic Resample

## Bugs

- [ ] Player shadows are missing — models reach the shadow pass but never show up. Probably a missing per-instance push constant in [VulkanOptimizedVoxelModel.cpp](VulkanOptimizedVoxelModel.cpp) `RenderShadowMapPass`, or a bad bias.

## Stubs to flesh out

- [ ] [VulkanWaterRenderer.cpp](VulkanWaterRenderer.cpp) `RenderDynamicLightPass` reuses the sunlight pipeline as a placeholder — water doesn't react to dynamic lights.
- [ ] [VulkanOptimizedVoxelModel.cpp](VulkanOptimizedVoxelModel.cpp) `PreloadShaders` is empty — pipelines build on first use, so the first frame stutters.
- [ ] [VulkanMapRenderer.cpp](VulkanMapRenderer.cpp) `PreloadShaders` is empty — same story, on map chunk pipelines.
