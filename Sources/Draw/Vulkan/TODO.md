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

## Medium

- [ ] Handle swapchain resize. `SDLVulkanDevice::RecreateSwapchain()` is declared but never called. On window resize, `VK_ERROR_OUT_OF_DATE_KHR` from `vkQueuePresentKHR` is silently unhandled. The GL renderer calls `UpdateRenderSize()` every `Flip()`.

- [ ] Implement `ReadBitmap()`. Currently returns a null handle (`VulkanRenderer.cpp:1156–1167`). Requires a staging buffer, `vkCmdCopyImageToBuffer`, and a synchronous map/read sequence.

- [ ] Honor `sceneDef.skipWorld`. The Vulkan `RecordCommandBuffer` at lines 1440–1462 always renders the map. Add the `if (!sceneDef.skipWorld && mapRenderer)` guard present in the GL renderer, used for main menu and loading screens.

- [ ] Wire SSAO. `VulkanSSAOFilter` is not instantiated in `VulkanRenderer`. The GL renderer triggers a depth prepass and binds an occlusion texture during the sunlight pass. Vulkan needs a depth prepass and the SSAO texture bound to model/map shaders.