VULKAN RENDERER - COMPREHENSIVE UNUSED CODE ANALYSIS REPORT
================================================================

Generated: 2026-01-24
Workspace: /Users/francoisnd/Desktop/dev/zerospades
Scope: All 72 Vulkan renderer files in Sources/Draw

================================================================
EXECUTIVE SUMMARY
================================================================

Total Issues Found: 290
Files Analyzed: 72
Files with Issues: 44

SEVERITY BREAKDOWN:
  🟡 MEDIUM PRIORITY: 78 issues
  ⚪ LOW PRIORITY: 212 issues

ISSUE TYPE BREAKDOWN:
  • Unused Private Members: 78
  • Forward Declarations Only: 208
  • Commented-Out Code: 4


================================================================
DETAILED FINDINGS
================================================================

1. UNUSED PRIVATE MEMBERS (78 MEDIUM PRIORITY ISSUES)
================================================================

These are class/struct member variables that appear to be declared but never used
after their definition. Consider removing or checking if they're intentional.

VulkanAutoExposureFilter.h:
  - preprocessPipeline, preprocessLayout, preprocessDescLayout
  - downsamplePipeline, downsampleLayout, downsampleDescLayout
  - computeGainPipeline, computeGainLayout, computeGainDescLayout
  - applyPipeline, applyLayout, applyDescLayout
  - downsampleRenderPass, exposureRenderPass, exposureFramebuffer
  - descriptorPool

VulkanBloomFilter.h:
  - downsamplePipeline, downsampleLayout, compositePipeline, compositeLayout
  - finalCompositePipeline, finalCompositeLayout
  - downsampleDescLayout, compositeDescLayout, finalCompositeDescLayout
  - descriptorPool, downsampleRenderPass, compositeRenderPass, framebuffer

VulkanColorCorrectionFilter.h:
  - descriptorPool, framebuffer, gaussPipeline
  - gaussPipelineLayout, gaussDescriptorSetLayout, gaussRenderPass

VulkanDepthOfFieldFilter.h:
  - cocGenPipeline, cocGenLayout, cocGenDescLayout
  - blurPipeline, blurLayout, blurDescLayout
  - gaussPipeline, gaussLayout, gaussDescLayout
  - finalMixPipeline, finalMixLayout, finalMixDescLayout
  - cocRenderPass, blurRenderPass, descriptorPool

VulkanFXAAFilter.h:
  - descriptorPool, framebuffer

VulkanFogFilter.h:
  - descriptorPool, framebuffer

VulkanLensFlareFilter.h:
  - blurPipeline, scannerPipeline, drawPipeline
  - blurLayout, scannerLayout, drawLayout
  - blurDescLayout, scannerDescLayout, drawDescLayout
  - scannerRenderPass, drawRenderPass, descriptorPool
  - visibilityFramebuffer

VulkanModel.h:
  - renderId

VulkanPipelineCache.h:
  - cachePath, cacheVersion, CACHE_VERSION

VulkanTemporalAAFilter.h:
  - valid, prevMatrix, prevViewOrigin, jitterTableIndex
  - descriptorPool, copyRenderPass

VulkanWaterRenderer.h:
  - descriptorPool


2. FORWARD DECLARATIONS WITHOUT IMPLEMENTATIONS (208 LOW PRIORITY ISSUES)
================================================================

These are methods/functions that are declared in headers but only have their
declaration, not implementation, within the analyzed section. Most are likely
implemented in corresponding .cpp files.

COMMON PATTERNS:
  - Private helper methods in post-processing filters
  - Resource creation/destruction methods
  - Vulkan command recording methods
  - Descriptor and pipeline setup methods

Examples:
  - CreateQuadBuffers() - declared in many filter headers
  - CreateDescriptorPool() - declared in all filter headers
  - CreatePipeline(), CreateRenderPass() - common pattern
  - Various Render*Pass() methods


3. COMMENTED-OUT CODE (4 LOW PRIORITY ISSUES)
================================================================

VulkanMapRenderer.cpp:121
  - Comment with TODO marker

VulkanOptimizedVoxelModel.cpp:44
  - Comment with TODO marker

VulkanRenderer.cpp:840
  - Comment with TODO marker

VulkanRenderer.cpp:846
  - Comment with Suppress unused note


================================================================
RECOMMENDATIONS
================================================================

IMMEDIATE ACTIONS (HIGH PRIORITY):
  1. Review unused private members in filter classes
     - Check if they should be removed or if they're placeholders
     - Investigate pattern of unused descriptorPool across multiple filters

MEDIUM PRIORITY:
  2. Investigate commented-out code with TODO markers
     - Consider completing TODO items or removing dead code
     - Document why code is commented if still needed

LOW PRIORITY:
  3. Forward declarations are expected
     - These are typically implemented in .cpp files
     - Only concern if declaration suggests unused API

GENERAL CLEANUP:
  4. Many post-processing filters follow similar patterns with unused members
     - Consider refactoring common filter base class
     - Extract descriptor pool management to reduce duplication

  5. Pipeline and layout members appear unused in filter classes
     - Verify if they're actually used in implementation files
     - May indicate incomplete filter implementations


================================================================
FILES REQUIRING DETAILED REVIEW
================================================================

HIGHEST CONCERN:
  1. VulkanDepthOfFieldFilter.h - 15 unused private members
  2. VulkanLensFlareFilter.h - 13 unused private members
  3. VulkanBloomFilter.h - 13 unused private members
  4. VulkanAutoExposureFilter.h - 16 unused private members

MEDIUM CONCERN:
  5. VulkanTemporalAAFilter.h - 6 unused private members
  6. VulkanColorCorrectionFilter.h - 6 unused private members
  7. VulkanPipelineCache.h - 3 unused private members

NOTES:
  - Patterns suggest filter implementations may be incomplete
  - Descriptor pool members consistently unused across filters
  - May indicate copy-paste initialization without actual usage


================================================================
ANALYSIS METHODOLOGY
================================================================

This report uses pattern-based analysis to identify:
  1. Private member variables with no subsequent usage
  2. Method declarations in headers without visible implementations
  3. Commented-out code with TODO/FIXME markers
  4. Forward-only declarations

Note: Analysis is pattern-based and may have false positives.
Especially for cross-file dependencies. Verify findings in context
of actual implementation before making changes.


================================================================
NEXT STEPS
================================================================

1. Cross-reference with Implementation Files (.cpp)
   Some "unused" members may be accessed via indirect means
   (callbacks, virtual methods, etc.)

2. Check Build Configuration
   Some code may be compiled conditionally or in debug builds

3. Review Version Control History
   Understand when these patterns were introduced

4. Profile Runtime Behavior
   Unused members may have performance implications if they
   allocate resources

5. Consider Compiler Warnings
   Enable -Wunused-member-variable to catch these automatically
