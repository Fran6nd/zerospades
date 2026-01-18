## 🟠 HIGH PRIORITY - Post-Processing Effects (Placeholder Implementations)

All post-processing filters have placeholder implementations that don't actually work.

### ~~12. Auto Exposure Filter~~ ✅
**File:** `VulkanAutoExposureFilter.cpp`
- [x] Create preprocess pipeline
- [x] Create compute gain pipeline
- [x] Implement auto exposure:
  - [x] Downsample and compute average luminance
  - [x] Calculate exposure adjustment
  - [x] Apply exposure to output

---

### 13. Lens Flare Filter
**File:** `VulkanLensFlareFilter.cpp`
- [ ] Create lens flare pipelines ([VulkanLensFlareFilter.cpp:110](Sources/Draw/VulkanLensFlareFilter.cpp#L110))
- [ ] Load flare texture images ([VulkanLensFlareFilter.cpp:119](Sources/Draw/VulkanLensFlareFilter.cpp#L119))
- [ ] Implement lens flare drawing ([VulkanLensFlareFilter.cpp:138](Sources/Draw/VulkanLensFlareFilter.cpp#L138))

---

## 🟡 MEDIUM PRIORITY - Optimization & Best Practices

### 14. GPU Device Selection
**File:** `SDLVulkanDevice.cpp`
- [ ] Implement GPU scoring system ([SDLVulkanDevice.cpp:295](Sources/Gui/SDLVulkanDevice.cpp#L295))
  - [ ] Score by device type (discrete > integrated > virtual > CPU)
  - [ ] Score by memory capacity
  - [ ] Score by feature support
  - [ ] Score by queue family capabilities
- [ ] Allow user override via settings
- [ ] Log selected GPU and reason

**Impact:** May not use discrete GPU on systems with multiple GPUs, resulting in poor performance.

---

### 15. Pipeline Cache Implementation
**Files:** New files needed
- [ ] Create VulkanPipelineCache class
- [ ] Save pipeline cache to disk on shutdown
- [ ] Load pipeline cache on startup
- [ ] Integrate with all pipeline creation calls
- [ ] Add cache versioning for validation

**Impact:** Faster startup times and reduced shader compilation hitches.

---

### 16. Code Refactoring - Reduce Redundancy

#### Render Pass Creation
**Files:** Multiple `Vulkan*Filter.cpp` files
- [ ] Extract common render pass creation logic
- [ ] Create utility function: `CreateSimpleColorRenderPass()`
- [ ] Update all filters to use common function
- [ ] Reduce code duplication across 8+ files

#### Pipeline Creation Boilerplate
**Files:** Multiple `Vulkan*Filter.cpp` files
- [ ] Create pipeline builder helper class
- [ ] Simplify pipeline creation with fluent API
- [ ] Reduce boilerplate in each filter

---

### 17. Synchronization Audit
**Files:** All `Vulkan*.cpp` files
- [ ] Audit all image layout transitions
- [ ] Ensure proper pipeline barriers between passes
- [ ] Verify queue family ownership transfers (if using multiple queues)
- [ ] Check for race conditions in multi-threaded rendering
- [ ] Ensure descriptor sets are not modified while in use
- [ ] Verify proper use of VK_PIPELINE_STAGE flags

**Impact:** Prevents validation errors, crashes, and rendering corruption.

---

### 18. Memory Management Audit
**Files:** All `Vulkan*.cpp` files
- [ ] Verify all vkDestroy* calls match vkCreate* calls
- [ ] Check for memory leaks with validation layers
- [ ] Audit deferred deletion queue behavior ([VulkanRenderer.h:94-95](Sources/Draw/VulkanRenderer.h#L94-L95))
- [ ] Ensure proper cleanup on renderer destruction
- [ ] Test cleanup during swapchain recreation

---

### 19. Descriptor Set Pool Optimization
**File:** `VulkanDescriptorPool.cpp`
- [ ] Review descriptor set allocation patterns
- [ ] Ensure sets are reused, not recreated each frame
- [ ] Implement descriptor set caching
- [ ] Monitor pool exhaustion and implement dynamic growth

---

## 🟢 LOW PRIORITY - Performance Optimizations

### 20. Push Constants
**Files:** Various
- [ ] Identify small uniform updates suitable for push constants
- [ ] Replace uniform buffer updates with push constants where appropriate
- [ ] Measure performance improvement

**Impact:** Reduces CPU overhead for small uniform updates.

---

### 21. Memory Aliasing
**Files:** Resource management classes
- [ ] Identify temporary resources (intermediate render targets)
- [ ] Implement memory aliasing for temporary resources
- [ ] Measure memory usage reduction

**Impact:** Reduces GPU memory consumption.


