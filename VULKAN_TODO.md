

### TODO: Feature Parity with OpenGL

- [x] **Implement FFT wave solver** - Now uses `FFTWaveTank` with kiss_fft for realistic waves, matching OpenGL implementation.

- [x] **Support multiple wave layers** - Now uploads all 3 wave tank layers for `r_water >= 2` using 2D array textures with proper per-layer time steps.

- [ ] **Implement Water2/Water3 shaders** - Only basic Water shader is converted. OpenGL has:
  - `Shaders/Water.program` (basic)
  - `Shaders/Water2.program` (multiple wave layers)
  - `Shaders/Water3.program` (mirror reflections + SSR)

- [ ] **Add mirror reflection support** - OpenGL Water3 uses mirror texture ([GLWaterRenderer.cpp:754-768](Sources/Draw/GLWaterRenderer.cpp#L754-L768)). Vulkan water renderer doesn't bind mirror textures.

- [x] **Implement partial texture updates** - Now performs efficient partial updates for water color texture, only uploading modified 32-pixel regions.

- [x] **Add occlusion query support** - Now uses `VK_QUERY_TYPE_OCCLUSION` to skip water rendering when no samples passed in previous frame.

- [x] **Generate mipmaps for wave texture** - Now calls `GenerateMipmaps()` after wave texture uploads using `vkCmdBlitImage`.

- [ ] **Verify matrix calculations match** - Compare uniform buffer contents:
  - `projectionViewModelMatrix` calculation
  - `viewModelMatrix` calculation
  - `waterPlane` calculation ([VulkanWaterRenderer.cpp:917-920](Sources/Draw/VulkanWaterRenderer.cpp#L917-L920))

- [x] **Check fovTan uniform** - [VulkanWaterRenderer.cpp:909-914](Sources/Draw/VulkanWaterRenderer.cpp#L909-L914) has different signs than OpenGL ([GLWaterRenderer.cpp:699-700](Sources/Draw/GLWaterRenderer.cpp#L699-L700)). Vulkan uses `-tanFovY` in second component but OpenGL uses `-tanFovY` in third component.
  **VERIFIED:** The fovTan values are actually identical between OpenGL and Vulkan. This TODO was incorrect.

### Files to Examine
- [Sources/Draw/VulkanWaterRenderer.cpp](Sources/Draw/VulkanWaterRenderer.cpp)
- [Sources/Draw/GLWaterRenderer.cpp](Sources/Draw/GLWaterRenderer.cpp) - Reference
- [Resources/Shaders/Water.vk.vs](Resources/Shaders/Water.vk.vs) (if exists)
- `Resources/Shaders/Water2.program`, `Water3.program` - Need Vulkan ports

---

## 4. Vulkan Renderer Optimizations

### Memory and Resource Management

- [ ] **Use memory aliasing for transient render targets** - Render targets that aren't used simultaneously can share the same memory allocation. The `VulkanTemporaryImagePool` ([Sources/Draw/VulkanTemporaryImagePool.h](Sources/Draw/VulkanTemporaryImagePool.h)) exists but could be expanded.

- [ ] **Batch descriptor set updates** - [VulkanWaterRenderer.cpp:436-536](Sources/Draw/VulkanWaterRenderer.cpp#L436-L536) updates descriptor sets every frame. Pre-allocate and reuse descriptor sets instead of updating each frame.

- [ ] **Use push constants for frequently changing uniforms** - Water renderer uses UBOs for per-frame data. Push constants (like sky renderer) would be faster for small, frequently updated data.

- [x] **Pool staging buffers** - Now pre-allocates and reuses staging buffers for wave texture uploads instead of creating new ones each frame.

### Synchronization

- [x] **Use proper frame-in-flight tracking** - [VulkanWaterRenderer.cpp:414](Sources/Draw/VulkanWaterRenderer.cpp#L414) hardcodes `frameIndex = 0`. Should track actual frame index for proper double/triple buffering.
  **FIXED:** Now uses `renderer.GetCurrentFrameIndex()` for proper tracking.

- [ ] **Batch queue submissions** - Multiple `vkQueueSubmit` calls in [VulkanWaterRenderer.cpp:681-682](Sources/Draw/VulkanWaterRenderer.cpp#L681-L682) and similar locations. Batch into single submission where possible.

- [x] **Remove vkQueueWaitIdle calls** - Now uses fences (`waveUploadFence`, `textureUploadFence`) for proper async synchronization instead of blocking queue waits.

### Pipeline Optimization

- [ ] **Use pipeline cache** - Verify all pipeline creation uses `renderer.GetPipelineCache()` for faster loading on subsequent runs.

- [ ] **Consider pipeline derivatives** - Water/Water2/Water3 shaders could use pipeline derivatives since they share most state.

- [ ] **Use specialization constants** - Replace runtime conditionals in shaders with specialization constants for better optimization.

### Render Pass Optimization

- [ ] **Merge compatible render passes** - If multiple passes have compatible attachments, consider merging into subpasses.

- [ ] **Use transient attachments** - Mark framebuffer attachments that don't need to persist as `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT`.

- [ ] **Optimize load/store ops** - Review all render passes for unnecessary `LOAD_OP_LOAD` where `DONT_CARE` would suffice, and `STORE_OP_STORE` where `DONT_CARE` is acceptable.

### Draw Call Optimization

- [ ] **Use indirect drawing** - For terrain/world rendering, consider `vkCmdDrawIndirect` to reduce CPU overhead.

- [ ] **Instance similar objects** - If drawing many similar objects, use instancing instead of separate draw calls.

- [ ] **Implement GPU culling** - Use compute shaders for frustum culling instead of CPU-side culling.

### Texture Streaming

- [ ] **Implement sparse textures** - For large textures, use `VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT` for virtual texturing.

- [ ] **Async texture uploads** - Use transfer queue for texture uploads to overlap with graphics work.

---

## Priority Order

1. **High** - Lens flare Y-axis flip (most visible bug)
2. **High** - Sky viewport/coordinate consistency
3. ~~**Medium** - Water FFT wave solver (visual quality)~~ **DONE**
4. ~~**Medium** - Frame-in-flight tracking (correctness)~~ **DONE**
5. **Low** - Other optimizations (performance)
