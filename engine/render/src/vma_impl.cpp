// The single translation unit that compiles the Vulkan Memory Allocator implementation.
// VMA is third-party code: its warnings are not ours to fix, so they are silenced here only.

#if defined(_MSC_VER)
#pragma warning(push, 0)
#pragma warning(disable : 4100 4127 4189 4324 4505)
#endif

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include <vk_mem_alloc.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
