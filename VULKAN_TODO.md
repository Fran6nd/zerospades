# Vulkan Renderer Bug Investigation and TODO List

## 1. Sky Glitchy When Moving Mouse

### Symptoms
The sky appears to move incorrectly or "glitch" when moving the mouse/camera.

### Possible Causes to Investigate

- [ ] **Viewport Y-axis flip inconsistency** - [Sky.vert:54](Resources/Shaders/Sky.vert#L54) negates the Y component (`-up * positionAttribute.y`), but this assumes the viewport is flipped with negative height. Verify the sky render pass uses the same flipped viewport as the main scene.
  - Check: [VulkanRenderer.cpp:1225-1227](Sources/Draw/VulkanRenderer.cpp#L1225-L1227) - Main viewport uses `y = renderHeight` and `height = -renderHeight`
  - Does the sky rendering use the same viewport setup?

- [ ] **View axis frame lag** - The `sceneDef.viewAxis[]` arrays in push constants ([VulkanRenderer.cpp:1739-1749](Sources/Draw/VulkanRenderer.cpp#L1739-L1749)) might be from the previous frame if updated at the wrong time in the render loop.

- [ ] **FOV calculation mismatch** - Compare how `fovX` and `fovY` are used in [Sky.vert](Resources/Shaders/Sky.vert) vs OpenGL. The vertex shader computes `tan(fov * 0.5)` at runtime - verify this matches the values the renderer expects.

- [ ] **Depth buffer comparison** - Sky is rendered at `z = 1.0` (far plane). Verify the depth test is configured correctly so sky doesn't fight with scene geometry.

- [ ] **NDC coordinate space** - OpenGL uses Z range [-1, 1], Vulkan uses [0, 1]. The sky shader sets `gl_Position.z = 1.0` which is correct for Vulkan, but verify the depth comparison is `LESS_OR_EQUAL` not just `LESS`.

### Files to Examine
- [Sources/Draw/VulkanRenderer.cpp](Sources/Draw/VulkanRenderer.cpp) - `RenderSky()` at line 1711
- [Resources/Shaders/Sky.vert](Resources/Shaders/Sky.vert)
- [Resources/Shaders/Sky.frag](Resources/Shaders/Sky.frag)

---

## 2. Lens Flare "Lightsaber" Artifacts

### Symptoms
Lens flares appear stretched/elongated like lightsabers instead of proper circular flares.

### Possible Causes to Investigate

- [ ] **Missing Y-axis flip in sunScreen calculation** - [VulkanLensFlareFilter.cpp:437-439](Sources/Draw/VulkanLensFlareFilter.cpp#L437-L439) calculates `sunScreen` identically to OpenGL, but Vulkan's Y-axis is flipped:
  ```cpp
  sunScreen.x = sunView.x / (sunView.z * fov.x);
  sunScreen.y = sunView.y / (sunView.z * fov.y);  // Should this be negated for Vulkan?
  ```
  The OpenGL version has Y-up in NDC, Vulkan has Y-down.

- [ ] **Viewport not flipped for lens flare pass** - [VulkanLensFlareFilter.cpp:689-696](Sources/Draw/VulkanLensFlareFilter.cpp#L689-L696) sets viewport with positive height:
  ```cpp
  viewport.y = 0.0f;
  viewport.height = (float)renderHeight;  // NOT negated!
  ```
  This is inconsistent with the main scene rendering which uses negative height.

- [ ] **Draw range coordinates in wrong space** - The `drawRange` uniform ([VulkanLensFlareFilter.cpp:706-741](Sources/Draw/VulkanLensFlareFilter.cpp#L706-L741)) uses `sunScreen` coordinates directly without Y-flip transformation.

- [ ] **Shader vertex position calculation** - [Draw.vk.vs:34](Resources/Shaders/LensFlare/Draw.vk.vs#L34) uses `mix(drawRange.xy, drawRange.zw, positionAttribute.xy)` which is identical to OpenGL. The issue is the `drawRange` values passed in, not the shader itself.

- [ ] **Visibility texture UV mismatch** - [Draw.vk.fs:38](Resources/Shaders/LensFlare/Draw.vk.fs#L38) samples visibility with `texCoord` which might have incorrect Y orientation.

- [ ] **Missing blur passes** - [VulkanLensFlareFilter.cpp:811-814](Sources/Draw/VulkanLensFlareFilter.cpp#L811-L814) - The blur function is a stub that returns input unchanged. OpenGL does 3 blur passes which softens the flare. This alone could cause sharp/stretched appearance.

### Comparison with OpenGL
| Aspect | OpenGL | Vulkan |
|--------|--------|--------|
| sunScreen.y | As-is | Should negate? |
| Viewport | Normal | Needs flip |
| Blur passes | 3 (1.0, 2.0, 4.0 spread) | Stub (none) |
| Y-axis in NDC | Up | Down |

### Files to Examine
- [Sources/Draw/VulkanLensFlareFilter.cpp](Sources/Draw/VulkanLensFlareFilter.cpp)
- [Sources/Draw/GLLensFlareFilter.cpp](Sources/Draw/GLLensFlareFilter.cpp) - Reference
- [Resources/Shaders/LensFlare/Draw.vk.vs](Resources/Shaders/LensFlare/Draw.vk.vs)
- [Resources/Shaders/LensFlare/Draw.vk.fs](Resources/Shaders/LensFlare/Draw.vk.fs)

---

## 3. Water Renderer - Matching OpenGL

### Current State
The Vulkan water renderer is a minimal port that lacks several features from the OpenGL version.

### TODO: Feature Parity with OpenGL

- [ ] **Implement FFT wave solver** - [VulkanWaterRenderer.cpp:85-221](Sources/Draw/VulkanWaterRenderer.cpp#L85-L221) only has `StandardWaveTank` (FTCS solver). OpenGL uses `FFTWaveTank` ([GLWaterRenderer.cpp:159-274](Sources/Draw/GLWaterRenderer.cpp#L159-L274)) which uses kiss_fft for more realistic waves.

- [ ] **Support multiple wave layers** - OpenGL uses 3 wave tank layers for `r_water >= 2` ([GLWaterRenderer.cpp:501-509](Sources/Draw/GLWaterRenderer.cpp#L501-L509)). Vulkan creates 3 tanks but only uploads the first one ([VulkanWaterRenderer.cpp:629-631](Sources/Draw/VulkanWaterRenderer.cpp#L629-L631)).

- [ ] **Implement Water2/Water3 shaders** - Only basic Water shader is converted. OpenGL has:
  - `Shaders/Water.program` (basic)
  - `Shaders/Water2.program` (multiple wave layers)
  - `Shaders/Water3.program` (mirror reflections + SSR)

- [ ] **Add mirror reflection support** - OpenGL Water3 uses mirror texture ([GLWaterRenderer.cpp:754-768](Sources/Draw/GLWaterRenderer.cpp#L754-L768)). Vulkan water renderer doesn't bind mirror textures.

- [ ] **Implement partial texture updates** - [VulkanWaterRenderer.cpp:781-785](Sources/Draw/VulkanWaterRenderer.cpp#L781-L785) skips partial updates with comment "partial updates omitted for now". OpenGL does efficient partial updates ([GLWaterRenderer.cpp:910-939](Sources/Draw/GLWaterRenderer.cpp#L910-L939)).

- [ ] **Add occlusion query support** - OpenGL uses `GL_SAMPLES_PASSED` occlusion query ([GLWaterRenderer.cpp:790-797](Sources/Draw/GLWaterRenderer.cpp#L790-L797)) to skip water rendering when not visible. Vulkan version doesn't have this optimization.

- [ ] **Generate mipmaps for wave texture** - OpenGL generates mipmaps ([GLWaterRenderer.cpp:848-856](Sources/Draw/GLWaterRenderer.cpp#L848-L856)). Vulkan doesn't call `vkCmdBlitImage` or similar for mipmap generation.

- [ ] **Verify matrix calculations match** - Compare uniform buffer contents:
  - `projectionViewModelMatrix` calculation
  - `viewModelMatrix` calculation
  - `waterPlane` calculation ([VulkanWaterRenderer.cpp:917-920](Sources/Draw/VulkanWaterRenderer.cpp#L917-L920))

- [ ] **Check fovTan uniform** - [VulkanWaterRenderer.cpp:909-914](Sources/Draw/VulkanWaterRenderer.cpp#L909-L914) has different signs than OpenGL ([GLWaterRenderer.cpp:699-700](Sources/Draw/GLWaterRenderer.cpp#L699-L700)). Vulkan uses `-tanFovY` in second component but OpenGL uses `-tanFovY` in third component.

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

- [ ] **Pool staging buffers** - [VulkanWaterRenderer.cpp:634-639](Sources/Draw/VulkanWaterRenderer.cpp#L634-L639) creates new staging buffers each frame. Should pool and reuse them.

### Synchronization

- [ ] **Use proper frame-in-flight tracking** - [VulkanWaterRenderer.cpp:414](Sources/Draw/VulkanWaterRenderer.cpp#L414) hardcodes `frameIndex = 0`. Should track actual frame index for proper double/triple buffering.

- [ ] **Batch queue submissions** - Multiple `vkQueueSubmit` calls in [VulkanWaterRenderer.cpp:681-682](Sources/Draw/VulkanWaterRenderer.cpp#L681-L682) and similar locations. Batch into single submission where possible.

- [ ] **Remove vkQueueWaitIdle calls** - [VulkanWaterRenderer.cpp:682](Sources/Draw/VulkanWaterRenderer.cpp#L682), [VulkanWaterRenderer.cpp:774](Sources/Draw/VulkanWaterRenderer.cpp#L774) use `vkQueueWaitIdle` which stalls the GPU. Use fences or timeline semaphores instead.

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
3. **Medium** - Water FFT wave solver (visual quality)
4. **Medium** - Frame-in-flight tracking (correctness)
5. **Low** - Other optimizations (performance)
