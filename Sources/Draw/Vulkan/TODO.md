# Vulkan Renderer — TODO

## Critical

- [ ] Wire post-processing filter chain in `RecordCommandBuffer()` — all filter classes exist in `Sources/Draw/Vulkan/` but none are instantiated as members of `VulkanRenderer` and none are called. Minimum required: Bloom, FXAA, Color Correction, Tone Mapping, Gamma Correction.
  - Volumetric Fog (`r_fogShadow`)
  - Depth of Field (`r_depthOfField`)
  - Camera Blur (`r_cameraBlur`)
  - Temporal AA (`r_temporalAA`)
  - Bloom / Lens Dust (`r_bloom`)
  - FXAA (`r_fxaa`)
  - Lens Flare (static + dynamic)
  - Auto-Exposure / Tone Mapping (`r_hdr`)
  - Gamma Correction
  - Color Correction (`r_colorCorrection`)
  - Bicubic Resample (`r_scaleFilter`)
  - The blit path at `VulkanRenderer.cpp:1754–1769` must route through this chain before copying to the swapchain.

## High

- [x] Implement `MultiplyScreenColor` with a dedicated multiplicative-blend pipeline (`VK_BLEND_FACTOR_ZERO` / `VK_BLEND_FACTOR_SRC_COLOR`). Fullscreen triangle from `gl_VertexIndex`, no vertex buffer. Rendered before UI in the swapchain render pass.

- [x] Implement ghost / transparent model pass. `VulkanModelRenderer::Prerender()` exists but is never called. `RenderSunlightPass` is always called with `ghostPass=false`. Required: depth-only prepass with `Prerender(true)`, then color pass with `DepthFunc(Equal)` and alpha blending with `ghostPass=true`, matching `GLRenderer.cpp:697` and `GLRenderer.cpp:884`.

- [x] Draw debug lines. `AddDebugLine()` pushes to `debugLines` but `RecordCommandBuffer` clears the vector at line 1490 without rendering. Implement the equivalent of `GLRenderer::RenderDebugLines()`.

## Medium

- [ ] Handle swapchain resize. `SDLVulkanDevice::RecreateSwapchain()` is declared but never called. On window resize, `VK_ERROR_OUT_OF_DATE_KHR` from `vkQueuePresentKHR` is silently unhandled. The GL renderer calls `UpdateRenderSize()` every `Flip()`.

- [ ] Implement `ReadBitmap()`. Currently returns a null handle (`VulkanRenderer.cpp:1156–1167`). Requires a staging buffer, `vkCmdCopyImageToBuffer`, and a synchronous map/read sequence.

- [ ] Honor `sceneDef.skipWorld`. The Vulkan `RecordCommandBuffer` at lines 1440–1462 always renders the map. Add the `if (!sceneDef.skipWorld && mapRenderer)` guard present in the GL renderer, used for main menu and loading screens.

- [ ] Wire SSAO. `VulkanSSAOFilter` is not instantiated in `VulkanRenderer`. The GL renderer triggers a depth prepass and binds an occlusion texture during the sunlight pass. Vulkan needs a depth prepass and the SSAO texture bound to model/map shaders.

## Low / Cleanup

- [ ] Remove debug log counters from production path. `VulkanRenderer.cpp:971–1033` has static `drawCallCount` / `addCallCount` locals that log the first few `DrawImage` calls. Remove them.

- [ ] Fix hardcoded depth format. `VK_FORMAT_D32_SFLOAT` is hardcoded in `CreateDepthResources()` and `CreateRenderPass()` without calling `vkGetPhysicalDeviceFormatProperties`. Add fallbacks: `VK_FORMAT_D24_UNORM_S8_UINT`, `VK_FORMAT_D16_UNORM`.

- [ ] Fix hardcoded map chunk size. Three instances in `VulkanMapChunk.cpp:206,342,372` use `511` / `512` instead of `map->Width()` / `map->Height()`.

- [ ] Batch texture uploads. `CreateImage()` at `VulkanRenderer.cpp:659` calls `vkQueueWaitIdle()` after every upload. Replace with batched uploads or timeline semaphores / async transfer queue.

- [ ] Re-enable water occlusion early-exit. The fast path in `VulkanWaterRenderer.cpp:846` is commented out. Uncomment and validate.

- [ ] Add frustum culling at the renderer level. The GL renderer exposes `BoxFrustrumCull()` and `SphereFrustrumCull()`. The Vulkan renderer has no equivalent; chunk and sprite sub-renderers may submit draw calls for out-of-frustum geometry.

- [ ] Normalize ownership model. Some subsystems use `Handle<>` ref-counting (`programManager`, `pipelineCache`, `temporaryImagePool`), others use raw `new`/`delete` (`mapRenderer`, `modelRenderer`, `waterRenderer`). Unify to reduce lifetime ambiguity and leak risk on early-exit paths.
