noor_add_module(render
    SOURCES
        src/vulkan_impl.h
        src/vulkan_renderer.cpp
        src/vulkan_resources.cpp
        src/vulkan_capture.cpp
        src/vulkan_overlay.cpp
        src/vma_impl.cpp
    PUBLIC_DEPS
        noor::foundation
        noor::platform
    PRIVATE_DEPS
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
)

noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.hlsl VSMain vs_6_0 mesh_vs)
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.hlsl PSMain ps_6_0 mesh_ps)
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/overlay.hlsl VSMain vs_6_0 overlay_vs)
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/overlay.hlsl PSMain ps_6_0 overlay_ps)
