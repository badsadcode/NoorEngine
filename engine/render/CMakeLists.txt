noor_add_module(render
    SOURCES
        src/vulkan_impl.h
        src/vulkan_renderer.cpp
        src/vulkan_resources.cpp
        src/vulkan_water.cpp
        src/vulkan_capture.cpp
        src/ibl.cpp
        src/shadows.cpp
        src/vulkan_frame.cpp
        src/vulkan_shadows.cpp
        src/vulkan_instancing.cpp
        src/vulkan_environment.cpp
        src/vulkan_atmosphere.cpp
        src/vulkan_clouds.cpp
        src/vulkan_textures.cpp
        src/vulkan_overlay.cpp
        src/vulkan_ui.cpp
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
# Game UI (6c): geometry, gradients, the clip mask, and passes over whole layers (copies, filters, blur, the finish).
foreach(entry IN ITEMS VSMain:vs:ui_vs PSDraw:ps:ui_draw_ps PSGradient:ps:ui_gradient_ps PSClip:ps:ui_clip_ps VSFull:vs:ui_full_vs
                      VSFullPlain:vs:ui_full_plain_vs PSPass:ps:ui_pass_ps PSOpaque:ps:ui_opaque_ps PSColorMatrix:ps:ui_color_matrix_ps
                      PSBlur:ps:ui_blur_ps PSDropShadow:ps:ui_drop_shadow_ps PSMask:ps:ui_mask_ps PSFinal:ps:ui_final_ps)
    string(REPLACE ":" ";" parts "${entry}")
    list(GET parts 0 fn)
    list(GET parts 1 stage)
    list(GET parts 2 name)
    noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui.hlsl ${fn} ${stage}_6_0 ${name})
endforeach()
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.hlsl VSMain vs_6_0 sky_vs)
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.hlsl PSMain ps_6_0 sky_ps)
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.hlsl VSMain vs_6_0 shadow_vs)
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.hlsl PSMain ps_6_0 shadow_ps)
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tonemap.hlsl VSMain vs_6_0 tonemap_vs)
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tonemap.hlsl PSMain ps_6_0 tonemap_ps)
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/particle.hlsl VSMain vs_6_0 particle_vs)
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/particle.hlsl PSMain ps_6_0 particle_ps)
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water.hlsl VSMain vs_6_0 water_vs)
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water.hlsl PSMain ps_6_0 water_ps)
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water.hlsl VSWet vs_6_0 water_wet_vs)
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water.hlsl PSWet ps_6_0 water_wet_ps)
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water.hlsl VSFog vs_6_0 water_fog_vs)
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water.hlsl PSFogMul ps_6_0 water_fog_mul_ps)
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water.hlsl PSFogAdd ps_6_0 water_fog_add_ps)
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.hlsl VSMain vs_6_0 weather_vs)
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.hlsl PSMain ps_6_0 weather_ps)
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/clouds_volume.hlsl CSNoise cs_6_0 clouds_volume_noise)
noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/clouds_volume.hlsl CSMarch cs_6_0 clouds_volume_march)
foreach(pass IN ITEMS Transmittance Multiple Sky Downsample Cube Irradiance Aerial CloudLight)
    string(TOLOWER ${pass} lower)
    noor_add_shader(noor_render ${CMAKE_CURRENT_SOURCE_DIR}/shaders/atmosphere.hlsl CS${pass} cs_6_0 atmosphere_${lower})
endforeach()
