
## Vulkan Renderer Optimizations

### Memory and Resource Management

- [ ] **Use memory aliasing for transient render targets** - Render targets that aren't used simultaneously can share the same memory allocation. The `VulkanTemporaryImagePool` ([Sources/Draw/VulkanTemporaryImagePool.h](Sources/Draw/VulkanTemporaryImagePool.h)) exists but could be expanded.

- [ ] **Batch descriptor set updates** - [VulkanWaterRenderer.cpp:436-536](Sources/Draw/VulkanWaterRenderer.cpp#L436-L536) updates descriptor sets every frame. Pre-allocate and reuse descriptor sets instead of updating each frame.

- [ ] **Use push constants for frequently changing uniforms** - Water renderer uses UBOs for per-frame data. Push constants (like sky renderer) would be faster for small, frequently updated data.

### Synchronization

- [ ] **Batch queue submissions** - Multiple `vkQueueSubmit` calls in [VulkanWaterRenderer.cpp:681-682](Sources/Draw/VulkanWaterRenderer.cpp#L681-L682) and similar locations. Batch into single submission where possible.

### Pipeline Optimization

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
