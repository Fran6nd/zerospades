
## Vulkan Renderer Optimizations

### Memory and Resource Management

- [ ] **Use memory aliasing for transient render targets** - Render targets that aren't used simultaneously can share the same memory allocation. The `VulkanTemporaryImagePool` ([Sources/Draw/VulkanTemporaryImagePool.h](Sources/Draw/VulkanTemporaryImagePool.h)) exists but could be expanded.

- [x] **Batch descriptor set updates** - Pre-bind static descriptors (water color texture, wave texture, UBOs) during initialization. Only update dynamic descriptors (screen/depth/mirror textures) each frame.

- [x] **Use push constants for frequently changing uniforms** - Water renderer now uses push constants for per-frame data (fog color, sky color, z near/far, fov tan, water plane, view origin, displace scale).

### Synchronization

- [x] **Batch queue submissions** - Water renderer now uses a single command buffer and fence for all texture uploads (wave and water color) instead of separate submissions.

### Pipeline Optimization

- [ ] **Consider pipeline derivatives** - Water/Water2/Water3 shaders could use pipeline derivatives since they share most state.

- [ ] **Use specialization constants** - Replace runtime conditionals in shaders with specialization constants for better optimization.

### Render Pass Optimization

- [ ] **Merge compatible render passes** - If multiple passes have compatible attachments, consider merging into subpasses.

- [x] **Use transient attachments** - Depth buffer now uses `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT` with lazily allocated memory where supported.

- [ ] **Optimize load/store ops** - Review all render passes for unnecessary `LOAD_OP_LOAD` where `DONT_CARE` would suffice, and `STORE_OP_STORE` where `DONT_CARE` is acceptable.

### Draw Call Optimization

- [ ] **Use indirect drawing** - For terrain/world rendering, consider `vkCmdDrawIndirect` to reduce CPU overhead.

- [ ] **Instance similar objects** - If drawing many similar objects, use instancing instead of separate draw calls.

- [ ] **Implement GPU culling** - Use compute shaders for frustum culling instead of CPU-side culling.

### Texture Streaming

- [ ] **Implement sparse textures** - For large textures, use `VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT` for virtual texturing.

- [ ] **Async texture uploads** - Use transfer queue for texture uploads to overlap with graphics work.
