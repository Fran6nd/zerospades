# Vulkan Renderer — TODO

Base class for filters: [VulkanPostProcessFilter.h](VulkanPostProcessFilter.h).
GL renderer in `Sources/Draw/OpenGL/` is the reference for everything below.

## Anti-aliasing gap

The most visible difference vs OpenGL today: distant geometry edges look
rough/aliased because the Vulkan path has **no AA at all**.

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

- [ ] **Radiosity Renderer** — port
      [GLRadiosityRenderer.cpp](../OpenGL/GLRadiosityRenderer.cpp).
      Four 3D textures (`flat`, `X`, `Y`, `Z`) + per-block update queue.

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
- [ ] **Fog is broken** (ZeroG) — currently the highest-priority visual bug. Compare against GL fog math + uniforms.
- [ ] **Screenshots flipped vertically** — Vulkan-native NDC y-flip vs SDL/PNG row order, somewhere in the readback path.

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
- [ ] **Mirrored model culling uses `VK_CULL_MODE_FRONT_BIT`** to flip handedness (e.g. left-hand view at [VulkanMapRenderer.cpp:777](VulkanMapRenderer.cpp#L777)). GL flips winding order via `glFrontFace(GL_CCW)`; Vulkan equivalent is toggling `VkPipelineRasterizationStateCreateInfo::frontFace` between `CW` / `COUNTER_CLOCKWISE`, not swapping cull mode. Current approach silently culls visible faces of mirrored geometry. Refactor to flip winding instead.
- [ ] **PBR (`BasicMapPhys` / `BasicModelVertexColorPhys`) glitch** — visible artifact distinct from the FXAA regression and the AA aliasing. Reproduce with `r_fxaa = 0` and `r_multisamples = 0` (and Vulkan equivalents) before debugging so AA isn't a confound.

## Stubs to flesh out

- [ ] [VulkanWaterRenderer.cpp](VulkanWaterRenderer.cpp) `RenderDynamicLightPass` reuses the sunlight pipeline as a placeholder — water doesn't react to dynamic lights.
- [ ] [VulkanOptimizedVoxelModel.cpp](VulkanOptimizedVoxelModel.cpp) `PreloadShaders` is empty — pipelines build on first use, so the first frame stutters.
- [ ] [VulkanMapRenderer.cpp](VulkanMapRenderer.cpp) `PreloadShaders` is empty — same story, on map chunk pipelines.
