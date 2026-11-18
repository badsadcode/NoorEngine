# HLSL -> SPIR-V at build time with DXC (ships with the Vulkan SDK), embedded as C++ arrays.
#
#   noor_add_shader(<target> <file.hlsl> <entry> <profile> <name>)
#
# Produces generated/noor/shaders/<name>.h containing `k_<name>` (SPIR-V bytes), added to
# <target>'s private include path. Shader warnings are errors (-WX).

find_program(NOOR_DXC dxc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" REQUIRED)

function(noor_add_shader target hlsl entry profile name)
    set(spv_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    set(gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(spv "${spv_dir}/${name}.spv")
    set(header "${gen_dir}/noor/shaders/${name}.h")
    get_filename_component(shader_dir "${hlsl}" DIRECTORY)
    file(GLOB includes CONFIGURE_DEPENDS "${shader_dir}/*.hlsli")  # shared code (#include): a change rebuilds every shader
    add_custom_command(
        OUTPUT "${header}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${spv_dir}" "${gen_dir}/noor/shaders"
        COMMAND "${NOOR_DXC}" -spirv -fspv-target-env=vulkan1.3 -WX -O3
                -T ${profile} -E ${entry} -Fo "${spv}" "${hlsl}"
        COMMAND ${CMAKE_COMMAND} -DINPUT=${spv} -DOUTPUT=${header} -DSYMBOL=k_${name}
                -P "${PROJECT_SOURCE_DIR}/cmake/NoorEmbedBinary.cmake"
        DEPENDS "${hlsl}" ${includes} "${PROJECT_SOURCE_DIR}/cmake/NoorEmbedBinary.cmake"
        COMMENT "Compiling shader ${name} (${profile}, ${entry})"
        VERBATIM)
    target_sources(${target} PRIVATE "${header}" "${hlsl}")
    target_include_directories(${target} PRIVATE "${gen_dir}")
    set_source_files_properties("${hlsl}" PROPERTIES HEADER_FILE_ONLY TRUE)
endfunction()
