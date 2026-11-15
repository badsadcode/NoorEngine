# Shared compiler settings for NoorEngine's own targets (not third-party code).
# Usage: noor_set_target_options(<target>)

function(noor_set_target_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /MP)
        target_compile_definitions(${target} PRIVATE
            NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror -Wconversion -Wno-sign-conversion)
    endif()
    set_target_properties(${target} PROPERTIES FOLDER "NoorEngine")
endfunction()

# Declares a NoorEngine library module living in engine/<name> with
# public headers in include/noor/<name>/.
function(noor_add_module name)
    cmake_parse_arguments(ARG "" "" "SOURCES;PUBLIC_DEPS;PRIVATE_DEPS" ${ARGN})
    add_library(noor_${name} STATIC ${ARG_SOURCES})
    add_library(noor::${name} ALIAS noor_${name})
    target_include_directories(noor_${name} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
    target_link_libraries(noor_${name}
        PUBLIC ${ARG_PUBLIC_DEPS}
        PRIVATE ${ARG_PRIVATE_DEPS})
    noor_set_target_options(noor_${name})
endfunction()
