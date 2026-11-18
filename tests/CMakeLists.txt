include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)

# ---- Unit tests (no GPU, no window) ----------------------------------------
add_executable(noor_unit_tests
    unit/main.cpp
    unit/test_log.cpp
    unit/test_error.cpp
    unit/test_assert.cpp
    unit/test_guid.cpp
    unit/test_math.cpp
    unit/test_fly_camera.cpp
    unit/test_files_hash.cpp
    unit/test_assets_scenes.cpp
    unit/test_history_migration_reload.cpp
    unit/test_prefabs.cpp
    unit/test_runtime.cpp
    unit/test_lua_scripts.cpp
    unit/test_physics.cpp
    unit/test_character.cpp
    unit/test_csharp_host.cpp
    unit/test_audio.cpp
    unit/test_native_window.cpp
    unit/test_hud.cpp
    unit/test_texture.cpp
    unit/test_material.cpp
    unit/test_shadows.cpp
    unit/test_environment.cpp
    unit/test_atmosphere.cpp
    unit/test_csg.cpp
    unit/test_terrain.cpp
    unit/test_sculpt.cpp
    unit/test_procgraph.cpp
    unit/test_interchange.cpp
    unit/test_lod.cpp
    unit/test_scatter.cpp
)
# ctest splits test names on ';', so a doctest case with one in its name silently runs nothing.
file(GLOB noor_unit_sources ${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp)
foreach(source IN LISTS noor_unit_sources)
    file(STRINGS ${source} bad_names REGEX "TEST_CASE[(]\"[^\"]*;")
    if(bad_names)
        message(FATAL_ERROR "${source}: a TEST_CASE name contains ';' (ctest would run nothing for it): use a comma")
    endif()
endforeach()
target_link_libraries(noor_unit_tests PRIVATE noor::foundation noor::platform noor::asset noor::scene noor::runtime noor::physics noor::audio noor::scripting noor::game noor::csg noor::terrain noor::terrain_edit noor::procgraph doctest::doctest stb::image)
target_compile_definitions(noor_unit_tests PRIVATE NOOR_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
# Header-only pieces of render (fly_camera.h) are tested without touching the GPU.
target_include_directories(noor_unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/engine/render/include)
noor_set_target_options(noor_unit_tests)
set_target_properties(noor_unit_tests PROPERTIES FOLDER "Tests")
# A ctest entry that matched no test case must fail, not pass. (A ';' in a TEST_CASE name splits
# it into two entries that match nothing: CMake lists use ';'. That hid 15 tests until 2026-11-16.)
set(NOOR_NOTHING_RAN "test cases: +0 ")  # no pipe character here: it breaks cmd quoting in the discovery step
doctest_discover_tests(noor_unit_tests PROPERTIES LABELS "unit" FAIL_REGULAR_EXPRESSION "${NOOR_NOTHING_RAN}")

# ---- Editor API tests: the document C ABI, as NoorEditor uses it (headless) ----
if(WIN32)
    # The viewport maths is internal to the DLL, so its source is compiled into the test too.
    add_executable(noor_editor_api_tests api/test_document_api.cpp api/test_viewport_tools.cpp
                   ${PROJECT_SOURCE_DIR}/engine/editor_api/src/viewport_tools.cpp)
    target_include_directories(noor_editor_api_tests PRIVATE ${PROJECT_SOURCE_DIR}/engine/editor_api/src)
    target_link_libraries(noor_editor_api_tests PRIVATE noor_editor_api noor::scene noor::terrain noor::procgraph doctest::doctest)
    target_compile_definitions(noor_editor_api_tests PRIVATE NOOR_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data" NOOR_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/samples")
    noor_set_target_options(noor_editor_api_tests)
    set_target_properties(noor_editor_api_tests PROPERTIES FOLDER "Tests")
    add_custom_command(TARGET noor_editor_api_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:noor_editor_api> $<TARGET_FILE_DIR:noor_editor_api_tests>)
    doctest_discover_tests(noor_editor_api_tests TEST_PREFIX "api: " PROPERTIES LABELS "api" FAIL_REGULAR_EXPRESSION "${NOOR_NOTHING_RAN}")
endif()

# ---- CLI tests (headless, run in CI) ---------------------------------------
# Full project workflow through noor_cli: new -> copy building -> scan -> add-model -> validate.
add_test(NAME cli.project_workflow
    COMMAND ${CMAKE_COMMAND}
        -DCLI=$<TARGET_FILE:noor_cli>
        -DDATA=${CMAKE_CURRENT_SOURCE_DIR}/data
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli_workflow
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_workflow.cmake)
set_tests_properties(cli.project_workflow PROPERTIES LABELS "cli" TIMEOUT 60)

# noor_cli build packs the test project with the player (headless: only copies and checks files).
add_test(NAME cli.build
    COMMAND ${CMAKE_COMMAND}
        -DCLI=$<TARGET_FILE:noor_cli>
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/player_project
        -DOUT=${CMAKE_CURRENT_BINARY_DIR}/built_game
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_build.cmake)
set_tests_properties(cli.build PROPERTIES LABELS "cli" TIMEOUT 60 FIXTURES_SETUP built_game)

# ---- GPU tests ---------------------------------------------------------------
# Need a Vulkan 1.3 GPU and a desktop. Validation layers on; any validation error fails.

# Runs 180 frames of the spinning cube.
add_test(NAME smoke.noor_diag
    COMMAND noor_diag --frames 180 --log ${CMAKE_CURRENT_BINARY_DIR}/smoke_noor_diag.log)
set_tests_properties(smoke.noor_diag PROPERTIES LABELS "gpu;smoke" TIMEOUT 60)

# Reads a rendered frame back from the GPU and checks real pixels (background color,
# cube front and top faces): catches projection, culling, depth and color-space bugs.
add_test(NAME render.cube_pixels
    COMMAND noor_diag --frames 60 --verify-render --log ${CMAKE_CURRENT_BINARY_DIR}/render_cube_pixels.log)
set_tests_properties(render.cube_pixels PROPERTIES LABELS "gpu;render" TIMEOUT 60)

# 2,500 spinning cubes + ground (2,502 draw calls per frame) for 120 frames with validation on:
# catches leaks, sync errors and slow paths under load. Frame timings go to the log.
add_test(NAME stress.cubes_2500
    COMMAND noor_diag --frames 120 --cubes 2500 --log ${CMAKE_CURRENT_BINARY_DIR}/stress_cubes_2500.log)
set_tests_properties(stress.cubes_2500 PROPERTIES LABELS "gpu;stress" TIMEOUT 120)

# A real BadBuilding block (Noor Interchange v0.2) imported and rendered with validation on.
add_test(NAME render.glb_building
    COMMAND noor_diag --frames 60 --glb ${CMAKE_CURRENT_SOURCE_DIR}/data/badbuilding/osm_w117821721.glb
            --log ${CMAKE_CURRENT_BINARY_DIR}/render_glb_building.log)
set_tests_properties(render.glb_building PROPERTIES LABELS "gpu;render" TIMEOUT 60)

# Window lifecycle: minimize, restore, resize (and a monitor move when 2+ displays exist),
# checking that rendering pauses/resumes and the swapchain follows the window.
add_test(NAME lifecycle.window
    COMMAND noor_diag --lifecycle-test --log ${CMAKE_CURRENT_BINARY_DIR}/lifecycle_window.log)
set_tests_properties(lifecycle.window PROPERTIES LABELS "gpu;lifecycle" TIMEOUT 60)

# NoorEditor end to end: builds the C# editor against this build's noor_editor_api.dll and runs
# its --smoke self-test (project, add model, inspector edits, undo/redo, save, float viewport).
find_program(NOOR_DOTNET dotnet)
if(WIN32 AND NOOR_DOTNET)
    add_test(NAME editor.smoke
        COMMAND ${CMAKE_COMMAND}
            -DDOTNET=${NOOR_DOTNET}
            -DPROJECT=${PROJECT_SOURCE_DIR}/editor/NoorEditor/NoorEditor.csproj
            -DNATIVE_DIR=$<TARGET_FILE_DIR:noor_editor_api>
            -DOUT=${CMAKE_CURRENT_BINARY_DIR}/editor_smoke/$<CONFIG>
            -DDATA=${CMAKE_CURRENT_SOURCE_DIR}/data
            -DCONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/editor_smoke.cmake)
    # gpu_timing: the smoke test measures play-mode update rates; the GPU-heavy forest tests don't run beside it.
    set_tests_properties(editor.smoke PROPERTIES LABELS "gpu;editor" TIMEOUT 300 RESOURCE_LOCK gpu_timing)
endif()

# Golden-image render tests (Milestone 5, ADR-012): scenes drawn by noor_player, compared with
# reference pictures in tests/golden by noor_image_compare (tests/tools/image_compare.cpp).
add_executable(noor_image_compare tools/image_compare.cpp)
target_link_libraries(noor_image_compare PRIVATE stb::image)
foreach(name IN ITEMS Daylight Sunset Textured Materials Lights Env_studio_small_09 Env_kloppenheim_06 Env_venice_sunset Glass Terrain TerrainPaint
                     Atmosphere_Noon Atmosphere_Sunset)
    add_test(NAME render.golden_${name}
        COMMAND ${CMAKE_COMMAND}
            -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/${name}.noorscene
            -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/${name}.png
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_${name}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
    set_tests_properties(render.golden_${name} PROPERTIES LABELS "gpu;render" TIMEOUT 60 FIXTURES_SETUP golden_${name})
endforeach()
# A moving sun (Milestone 8A, part 2): the time of day dragged from dawn to dusk, the live sky rebuilt
# on the GPU every frame, 60 FPS held (tests/day_cycle_check.cmake). And the CPU reference path (part 1)
# must still match the same goldens the GPU path does.
add_test(NAME render.day_cycle
    COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/day_cycle_check -P ${CMAKE_CURRENT_SOURCE_DIR}/day_cycle_check.cmake)
set_tests_properties(render.day_cycle PROPERTIES LABELS "gpu;render" TIMEOUT 120 RESOURCE_LOCK gpu_timing)
foreach(name IN ITEMS Atmosphere_Noon Atmosphere_Sunset)
    add_test(NAME render.golden_${name}_cpu_sky
        COMMAND ${CMAKE_COMMAND}
            -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/${name}.noorscene
            -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/${name}.png -DEXTRA=--cpu-sky
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_${name}_cpu
            -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
    set_tests_properties(render.golden_${name}_cpu_sky PROPERTIES LABELS "gpu;render" TIMEOUT 60)
endforeach()
# CSG in games (Milestone 6, part 4): noor_cli build cooks the model; a character walks through its
# doorway, another is stopped by the wall (tests/csg_check.cmake).
add_test(NAME cli.build_csg
    COMMAND $<TARGET_FILE:noor_cli> build ${CMAKE_CURRENT_SOURCE_DIR}/data/csg_project ${CMAKE_CURRENT_BINARY_DIR}/built_csg_game)
set_tests_properties(cli.build_csg PROPERTIES LABELS "cli" TIMEOUT 120 FIXTURES_SETUP built_csg_game
                     PASS_REGULAR_EXPRESSION "1 CSG model[(]s[)] cooked")
add_test(NAME player.csg_doorway
    COMMAND ${CMAKE_COMMAND} "-DPLAYER=${CMAKE_CURRENT_BINARY_DIR}/built_csg_game/CSG Test.exe" -DWORK=${CMAKE_CURRENT_BINARY_DIR}/player_csg
        -P ${CMAKE_CURRENT_SOURCE_DIR}/csg_check.cmake)
set_tests_properties(player.csg_doorway PROPERTIES LABELS "gpu;player" TIMEOUT 60 FIXTURES_REQUIRED built_csg_game)

# Procedural graphs headless (Milestone 7, part 2): noor_cli generate writes the pinned fingerprint, and the
# editor API's run of the same graph writes the same bytes (api.generate_matches_cli compares the files).
add_test(NAME cli.generate
    COMMAND $<TARGET_FILE:noor_cli> generate ${CMAKE_CURRENT_SOURCE_DIR}/data/procgraph_project Assets/Graphs/hills.noorgraph
            --out ${CMAKE_CURRENT_BINARY_DIR}/generated_cli --no-cache)
set_tests_properties(cli.generate PROPERTIES LABELS "cli" TIMEOUT 120 FIXTURES_SETUP generated_cli
                     PASS_REGULAR_EXPRESSION "fingerprint 193a309d3ece274d")
# Generated assets in (Milestone 7, part 5): noor_cli info reads a NoorForm oak's manifest and checks all four
# levels (a level that disagrees with the manifest prints "problem:" and fails).
add_test(NAME cli.info_interchange
    COMMAND $<TARGET_FILE:noor_cli> info ${CMAKE_CURRENT_SOURCE_DIR}/data/noorform_oak/Oak_01.glb)
set_tests_properties(cli.info_interchange PROPERTIES LABELS "cli" TIMEOUT 60
                     PASS_REGULAR_EXPRESSION "LOD3 Oak_01[.]lod3[.]glb +8 triangles.*no limit, billboard"
                     FAIL_REGULAR_EXPRESSION "problem:|error ")
if(WIN32)
    add_test(NAME api.generate_matches_cli COMMAND noor_editor_api_tests "--test-case=*matches the CLI*")
    set_tests_properties(api.generate_matches_cli PROPERTIES LABELS "api" FIXTURES_REQUIRED generated_cli
                         ENVIRONMENT "NOOR_CLI_GENERATED=${CMAKE_CURRENT_BINARY_DIR}/generated_cli" FAIL_REGULAR_EXPRESSION "${NOOR_NOTHING_RAN}")
endif()

# A terrain built on a procedural graph in a built game (Milestone 7, part 3): noor_cli build cooks the graph.
add_test(NAME cli.build_graph_terrain
    COMMAND $<TARGET_FILE:noor_cli> build ${CMAKE_CURRENT_SOURCE_DIR}/data/procgraph_project ${CMAKE_CURRENT_BINARY_DIR}/built_graph_game)
set_tests_properties(cli.build_graph_terrain PROPERTIES LABELS "cli" TIMEOUT 120 FIXTURES_SETUP built_graph_game
                     PASS_REGULAR_EXPRESSION "1 graph[(]s[)] cooked")
add_test(NAME player.graph_terrain
    COMMAND ${CMAKE_COMMAND} "-DPLAYER=${CMAKE_CURRENT_BINARY_DIR}/built_graph_game/Graph Test.exe" -DWORK=${CMAKE_CURRENT_BINARY_DIR}/player_graph
        -P ${CMAKE_CURRENT_SOURCE_DIR}/graph_terrain_check.cmake)
set_tests_properties(player.graph_terrain PROPERTIES LABELS "gpu;player" TIMEOUT 60 FIXTURES_REQUIRED built_graph_game)

# Terrain in games (Milestone 6, part 5): a character walks up a heightmap ramp in a built game.
add_test(NAME cli.build_terrain
    COMMAND $<TARGET_FILE:noor_cli> build ${CMAKE_CURRENT_SOURCE_DIR}/data/terrain_project ${CMAKE_CURRENT_BINARY_DIR}/built_terrain_game)
set_tests_properties(cli.build_terrain PROPERTIES LABELS "cli" TIMEOUT 120 FIXTURES_SETUP built_terrain_game
                     PASS_REGULAR_EXPRESSION "Built 'Terrain Test'")
add_test(NAME player.terrain_walk
    COMMAND ${CMAKE_COMMAND} "-DPLAYER=${CMAKE_CURRENT_BINARY_DIR}/built_terrain_game/Terrain Test.exe" -DWORK=${CMAKE_CURRENT_BINARY_DIR}/player_terrain
        -P ${CMAKE_CURRENT_SOURCE_DIR}/terrain_check.cmake)
set_tests_properties(player.terrain_walk PROPERTIES LABELS "gpu;player" TIMEOUT 60 FIXTURES_REQUIRED built_terrain_game)
add_test(NAME cli.build_sculpt
    COMMAND $<TARGET_FILE:noor_cli> build ${CMAKE_CURRENT_SOURCE_DIR}/data/sculpt_project ${CMAKE_CURRENT_BINARY_DIR}/built_sculpt_game)
set_tests_properties(cli.build_sculpt PROPERTIES LABELS "cli" TIMEOUT 120 FIXTURES_SETUP built_sculpt_game)
add_test(NAME player.sculpted_hill
    COMMAND ${CMAKE_COMMAND} "-DPLAYER=${CMAKE_CURRENT_BINARY_DIR}/built_sculpt_game/Sculpt Test.exe" -DWORK=${CMAKE_CURRENT_BINARY_DIR}/player_sculpt
        -P ${CMAKE_CURRENT_SOURCE_DIR}/sculpt_check.cmake)
set_tests_properties(player.sculpted_hill PROPERTIES LABELS "gpu;player" TIMEOUT 90 FIXTURES_REQUIRED built_sculpt_game)

# The PBR sample (Milestone 5, part 8): the same showroom under the sun and three HDRIs.
foreach(name IN ITEMS Showroom_Sun Showroom_Studio Showroom_Evening Showroom_Venice)
    add_test(NAME render.golden_${name}
        COMMAND ${CMAKE_COMMAND}
            -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
            -DPROJECT=${PROJECT_SOURCE_DIR}/samples/PBR-Showroom -DSCENE=Scenes/${name}.noorscene
            -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/${name}.png
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_${name}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
    # The first run compresses the sample's textures (cached in samples/PBR-Showroom/.noor after that).
    set_tests_properties(render.golden_${name} PROPERTIES LABELS "gpu;render;sample" TIMEOUT 180 RESOURCE_LOCK pbr_showroom_cache)
endforeach()
# The Milestone 6 sample (samples/Hillside, built by tools/samples/build_hillside.py through the editor
# API): its golden render, and a built game whose walker follows the path into the house.
add_test(NAME render.golden_Hillside
    COMMAND ${CMAKE_COMMAND}
        -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
        -DPROJECT=${PROJECT_SOURCE_DIR}/samples/Hillside -DSCENE=Scenes/Hillside.noorscene
        -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Hillside.png
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Hillside
        -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
set_tests_properties(render.golden_Hillside PROPERTIES LABELS "gpu;render;sample" TIMEOUT 180 RESOURCE_LOCK hillside_cache
                     FIXTURES_REQUIRED hillside_cooked)
# Milestone 7, part 8: Hillside's forest is a graph (paint mask + scatter); cook it before drawing the sample
# (a fresh clone has no .noor folder), and pin its result (it reads the paint layer through the registry).
add_test(NAME cli.cook_hillside COMMAND $<TARGET_FILE:noor_cli> cook ${PROJECT_SOURCE_DIR}/samples/Hillside)
set_tests_properties(cli.cook_hillside PROPERTIES LABELS "cli;sample" TIMEOUT 120 FIXTURES_SETUP hillside_cooked RESOURCE_LOCK hillside_cache
                     PASS_REGULAR_EXPRESSION "Cooked 1 graph[(]s[)]")
add_test(NAME cli.generate_hillside_forest
    COMMAND $<TARGET_FILE:noor_cli> generate ${PROJECT_SOURCE_DIR}/samples/Hillside Assets/Graphs/forest.noorgraph
            --out ${CMAKE_CURRENT_BINARY_DIR}/generated_hillside_forest --no-cache)
set_tests_properties(cli.generate_hillside_forest PROPERTIES LABELS "cli;sample" TIMEOUT 120 PASS_REGULAR_EXPRESSION "fingerprint 67a35a8fa217d253" FIXTURES_SETUP hillside_generated)
# The gate's "the CLI and the editor produce identical bytes", on the sample: generate's file = cook's file.
foreach(output forest grass)
    add_test(NAME gate.hillside_${output}_generate_equals_cook
        COMMAND ${CMAKE_COMMAND} -E compare_files ${CMAKE_CURRENT_BINARY_DIR}/generated_hillside_forest/${output}.instances.json
                ${PROJECT_SOURCE_DIR}/samples/Hillside/.noor/cooked/graphs/6ce16dc2-726d-4e70-a560-17127dc3798f/${output}.instances.json)
    set_tests_properties(gate.hillside_${output}_generate_equals_cook PROPERTIES LABELS "cli;sample" FIXTURES_REQUIRED "hillside_cooked;hillside_generated")
endforeach()
# Instancing and levels of detail (Milestone 7, part 6): the test forest (tests/data/forest_project,
# built by tools/samples/build_forest.py), its golden render and its draw/LOD numbers.
add_test(NAME render.golden_Forest
    COMMAND ${CMAKE_COMMAND}
        -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/forest_project -DSCENE=Scenes/Forest.noorscene
        -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Forest.png
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Forest
        -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
set_tests_properties(render.golden_Forest PROPERTIES LABELS "gpu;render" TIMEOUT 180 RESOURCE_LOCK "forest_cache;gpu_timing")
add_test(NAME render.forest_instancing_lod
    COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/forest_project
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/forest_check -P ${CMAKE_CURRENT_SOURCE_DIR}/forest_check.cmake)
set_tests_properties(render.forest_instancing_lod PROPERTIES LABELS "gpu;render" TIMEOUT 180 RESOURCE_LOCK "forest_cache;gpu_timing")
# Scatter (Milestone 7, part 7): cook the test forest's graph (hills + oaks), read its instance set with a
# reader that links no engine code, draw it (golden + check).
add_executable(noor_instance_reader tools/instance_reader.cpp)
target_link_libraries(noor_instance_reader PRIVATE nlohmann_json::nlohmann_json)
noor_set_target_options(noor_instance_reader)
add_test(NAME cli.cook_forest COMMAND $<TARGET_FILE:noor_cli> cook ${CMAKE_CURRENT_SOURCE_DIR}/data/forest_project)
set_tests_properties(cli.cook_forest PROPERTIES LABELS "cli" TIMEOUT 120 FIXTURES_SETUP forest_cooked RESOURCE_LOCK forest_cache
                     PASS_REGULAR_EXPRESSION "Cooked 1 graph[(]s[)]")
# The scatter graph's result is pinned: the same bytes in Debug and Release, on every run (no cache).
add_test(NAME cli.generate_forest
    COMMAND $<TARGET_FILE:noor_cli> generate ${CMAKE_CURRENT_SOURCE_DIR}/data/forest_project Assets/Graphs/forest.noorgraph
            --out ${CMAKE_CURRENT_BINARY_DIR}/generated_forest --no-cache)
set_tests_properties(cli.generate_forest PROPERTIES LABELS "cli" TIMEOUT 120 PASS_REGULAR_EXPRESSION "fingerprint 4ef35d5bf74670fb")
add_test(NAME tools.instance_reader
    COMMAND noor_instance_reader ${CMAKE_CURRENT_SOURCE_DIR}/data/forest_project/.noor/cooked/graphs/59de2071-f6e9-54bd-8215-beac941677ed/trees.instances.json)
set_tests_properties(tools.instance_reader PROPERTIES LABELS "cli" TIMEOUT 30 FIXTURES_REQUIRED forest_cooked
                     PASS_REGULAR_EXPRESSION "^[1-9][0-9][0-9]+ instances of 1 asset[(]s[)].*ids unique: yes")
add_test(NAME render.golden_Scattered
    COMMAND ${CMAKE_COMMAND}
        -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/forest_project -DSCENE=Scenes/Scattered.noorscene
        -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Scattered.png
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Scattered
        -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
set_tests_properties(render.golden_Scattered PROPERTIES LABELS "gpu;render" TIMEOUT 180 FIXTURES_REQUIRED forest_cooked
                     RESOURCE_LOCK "forest_cache;gpu_timing")
add_test(NAME render.scatter_check
    COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/forest_project
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/scatter_check -P ${CMAKE_CURRENT_SOURCE_DIR}/scatter_check.cmake)
set_tests_properties(render.scatter_check PROPERTIES LABELS "gpu;render" TIMEOUT 180 FIXTURES_REQUIRED forest_cooked
                     RESOURCE_LOCK "forest_cache;gpu_timing")
add_test(NAME cli.build_hillside
    COMMAND $<TARGET_FILE:noor_cli> build ${PROJECT_SOURCE_DIR}/samples/Hillside ${CMAKE_CURRENT_BINARY_DIR}/built_hillside_game)
set_tests_properties(cli.build_hillside PROPERTIES LABELS "cli;sample" TIMEOUT 180 FIXTURES_SETUP built_hillside_game RESOURCE_LOCK hillside_cache
                     PASS_REGULAR_EXPRESSION "1 CSG model[(]s[)] cooked")
add_test(NAME player.hillside_walk
    COMMAND ${CMAKE_COMMAND} "-DPLAYER=${CMAKE_CURRENT_BINARY_DIR}/built_hillside_game/Hillside.exe" -DWORK=${CMAKE_CURRENT_BINARY_DIR}/player_hillside
        -P ${CMAKE_CURRENT_SOURCE_DIR}/hillside_check.cmake)
set_tests_properties(player.hillside_walk PROPERTIES LABELS "gpu;player;sample" TIMEOUT 120 FIXTURES_REQUIRED built_hillside_game)
# The judge must notice a different picture: today's daylight against the sunset reference fails.
add_test(NAME render.golden_catches_a_change
    COMMAND noor_image_compare ${CMAKE_CURRENT_BINARY_DIR}/render_golden_Daylight/screenshot.bmp
                               ${CMAKE_CURRENT_SOURCE_DIR}/golden/Sunset.png)
set_tests_properties(render.golden_catches_a_change PROPERTIES LABELS "gpu;render" WILL_FAIL TRUE FIXTURES_REQUIRED golden_Daylight)

# noor_player runs a project, then the game noor_cli build made, with no editor: physics, a Lua
# script and rendering through the scene's camera (tests/player_check.cmake).
add_test(NAME player.project
    COMMAND ${CMAKE_COMMAND}
        -DPLAYER=$<TARGET_FILE:noor_player>
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/player_project
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/player_project -DHUD_VERTICES=48
        -P ${CMAKE_CURRENT_SOURCE_DIR}/player_check.cmake)
set_tests_properties(player.project PROPERTIES LABELS "gpu;player" TIMEOUT 60)
add_test(NAME player.built_game
    COMMAND ${CMAKE_COMMAND}
        "-DPLAYER=${CMAKE_CURRENT_BINARY_DIR}/built_game/Player Test.exe"
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/player_built_game -DHUD_VERTICES=48
        -P ${CMAKE_CURRENT_SOURCE_DIR}/player_check.cmake)
set_tests_properties(player.built_game PROPERTIES LABELS "gpu;player" TIMEOUT 60 FIXTURES_REQUIRED built_game)

# The same with a C# script: noor_cli build compiles it (needs the .NET SDK), the built game starts
# .NET through hostfxr (needs the .NET 10 runtime) and the script must have run.
if(TARGET noor_scripting_managed)
    add_test(NAME cli.build_csharp
        COMMAND $<TARGET_FILE:noor_cli> build ${CMAKE_CURRENT_SOURCE_DIR}/data/player_project_csharp ${CMAKE_CURRENT_BINARY_DIR}/built_csharp_game)
    set_tests_properties(cli.build_csharp PROPERTIES LABELS "cli" TIMEOUT 180 FIXTURES_SETUP built_csharp_game
                         PASS_REGULAR_EXPRESSION "C# scripts compiled into managed/Scripts.dll")
    add_test(NAME cli.build_csharp_error
        COMMAND ${CMAKE_COMMAND}
            -DCLI=$<TARGET_FILE:noor_cli>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/player_project_csharp
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli_build_csharp_error
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_build_csharp_error.cmake)
    set_tests_properties(cli.build_csharp_error PROPERTIES LABELS "cli" TIMEOUT 180)
    add_test(NAME player.built_csharp_game
        COMMAND ${CMAKE_COMMAND}
            "-DPLAYER=${CMAKE_CURRENT_BINARY_DIR}/built_csharp_game/Player Test CSharp.exe"
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/player_built_csharp_game -DCSHARP=ON -DHUD_VERTICES=60
            -P ${CMAKE_CURRENT_SOURCE_DIR}/player_check.cmake)
    set_tests_properties(player.built_csharp_game PROPERTIES LABELS "gpu;player" TIMEOUT 60 FIXTURES_REQUIRED built_csharp_game)

    # The sample game, NoorFPS-lite (Milestone 4, part 8): built like a user would, then both the
    # C# scene and its Lua twin play their demo and must score.
    add_test(NAME cli.build_sample_fps
        COMMAND $<TARGET_FILE:noor_cli> build ${PROJECT_SOURCE_DIR}/samples/NoorFPS-lite ${CMAKE_CURRENT_BINARY_DIR}/built_sample_fps)
    set_tests_properties(cli.build_sample_fps PROPERTIES LABELS "cli;sample" TIMEOUT 180 FIXTURES_SETUP built_sample_fps
                         PASS_REGULAR_EXPRESSION "Built 'NoorFPS-lite'")
    foreach(variant IN ITEMS csharp lua)
        set(scene Scenes/Main.noorscene)
        if(variant STREQUAL "lua")
            set(scene Scenes/Main_Lua.noorscene)
        endif()
        add_test(NAME sample.fps_${variant}
            COMMAND ${CMAKE_COMMAND}
                "-DPLAYER=${CMAKE_CURRENT_BINARY_DIR}/built_sample_fps/NoorFPS-lite.exe"
                -DSCENE=${scene} -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sample_fps_${variant}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/sample_fps_check.cmake)
        set_tests_properties(sample.fps_${variant} PROPERTIES LABELS "gpu;sample" TIMEOUT 90 FIXTURES_REQUIRED built_sample_fps)
    endforeach()
endif()

# The runtime boundary (Milestone 6 gate, ADR-016): games must not carry editor-only code. Walks
# the link graph of the player and the runtime modules at configure time; the test fails if the
# CSG module or Manifold is anywhere in it.
function(noor_collect_links target out_var)
    set(seen "")
    set(queue ${target})
    while(queue)
        list(POP_FRONT queue t)
        if(t IN_LIST seen OR NOT TARGET ${t})
            continue()
        endif()
        list(APPEND seen ${t})
        get_target_property(aliased ${t} ALIASED_TARGET)
        if(aliased)
            list(APPEND queue ${aliased})
            continue()
        endif()
        foreach(prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(libs ${t} ${prop})
            if(libs)
                foreach(lib IN LISTS libs)
                    string(REGEX REPLACE "^[$]<LINK_ONLY:(.*)>" "\\1" lib "${lib}")
                    list(APPEND queue ${lib})
                endforeach()
            endif()
        endforeach()
    endwhile()
    set(${out_var} ${seen} PARENT_SCOPE)
endfunction()
set(noor_boundary_report "")
foreach(runtime_target IN ITEMS noor_player noor_runtime noor_game noor_scene noor_render noor_physics)
    noor_collect_links(${runtime_target} links)
    foreach(forbidden IN ITEMS noor_csg manifold noor_terrain_edit noor_procgraph)
        if(forbidden IN_LIST links)
            string(APPEND noor_boundary_report "${runtime_target} links ${forbidden}\n")
        endif()
    endforeach()
endforeach()
# The walker must see what is there: the unit tests do link the CSG module and Manifold.
noor_collect_links(noor_unit_tests links)
if(NOT noor_csg IN_LIST links OR NOT manifold IN_LIST links OR NOT noor_terrain_edit IN_LIST links OR NOT noor_procgraph IN_LIST links)
    string(APPEND noor_boundary_report "the link walk is broken: it doesn't find noor_csg/manifold in noor_unit_tests
")
endif()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/runtime_boundary.txt "${noor_boundary_report}")
add_test(NAME boundary.runtime_has_no_editor_code
    COMMAND ${CMAKE_COMMAND} -DREPORT=${CMAKE_CURRENT_BINARY_DIR}/runtime_boundary.txt -P ${CMAKE_CURRENT_SOURCE_DIR}/runtime_boundary.cmake)
set_tests_properties(boundary.runtime_has_no_editor_code PROPERTIES LABELS "unit")
