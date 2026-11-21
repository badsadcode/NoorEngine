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
    unit/test_fog.cpp
    unit/test_clouds.cpp
    unit/test_night.cpp
    unit/test_weather.cpp
    unit/test_particles.cpp
    unit/test_particle_collisions.cpp
    unit/test_sprites.cpp
    unit/test_sprite_animation.cpp
    unit/test_skinning.cpp
    unit/test_animation.cpp
    unit/test_anim_controller.cpp
    unit/test_root_motion.cpp
    unit/test_retarget.cpp
    unit/test_fbx.cpp
    unit/test_water.cpp
    unit/test_water_physics.cpp
    unit/test_csg.cpp
    unit/test_terrain.cpp
    unit/test_sculpt.cpp
    unit/test_procgraph.cpp
    unit/test_interchange.cpp
    unit/test_lod.cpp
    unit/test_scatter.cpp
    unit/test_tilemap.cpp
    unit/test_display.cpp
    unit/test_templates.cpp
    unit/test_spawn.cpp
)
# ctest splits test names on ';', so a doctest case with one in its name silently runs nothing.
file(GLOB noor_unit_sources ${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp)
foreach(source IN LISTS noor_unit_sources)
    file(STRINGS ${source} bad_names REGEX "TEST_CASE[(]\"[^\"]*;")
    if(bad_names)
        message(FATAL_ERROR "${source}: a TEST_CASE name contains ';' (ctest would run nothing for it): use a comma")
    endif()
endforeach()
target_link_libraries(noor_unit_tests PRIVATE noor::foundation noor::platform noor::asset noor::scene noor::runtime noor::physics noor::audio noor::scripting noor::game noor::csg noor::terrain noor::terrain_edit noor::procgraph noor::fx doctest::doctest stb::image)
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
    add_executable(noor_editor_api_tests api/test_document_api.cpp api/test_viewport_tools.cpp api/test_environment_api.cpp api/test_effect_api.cpp api/test_sprite_api.cpp api/test_animation_api.cpp api/test_water_api.cpp api/test_tilemap_api.cpp
                   ${PROJECT_SOURCE_DIR}/engine/editor_api/src/viewport_tools.cpp)
    target_include_directories(noor_editor_api_tests PRIVATE ${PROJECT_SOURCE_DIR}/engine/editor_api/src)
    target_link_libraries(noor_editor_api_tests PRIVATE noor_editor_api noor::scene noor::terrain noor::procgraph noor::game doctest::doctest)
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
                     Atmosphere_Noon Atmosphere_Sunset Atmosphere_Clouds Atmosphere_Overcast Atmosphere_Volume Space)
    add_test(NAME render.golden_${name}
        COMMAND ${CMAKE_COMMAND}
            -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/${name}.noorscene
            -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/${name}.png
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_${name}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
    set_tests_properties(render.golden_${name} PROPERTIES LABELS "gpu;render" TIMEOUT 60 FIXTURES_SETUP golden_${name})
endforeach()
# Particles (Milestone 8B, part 2): a campfire at noon and at night (fire and sparks additive, smoke lit and blended),
# and smoke between two glass panes (one in front, one behind: the see-through pass's order). Two seconds in, so the
# effects have built up.
foreach(name IN ITEMS Particles_Campfire Particles_Campfire_Night Particles_Glass)
    add_test(NAME render.golden_${name}
        COMMAND ${CMAKE_COMMAND}
            -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/${name}.noorscene
            -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/${name}.png -DFRAMES=120
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_${name}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
    set_tests_properties(render.golden_${name} PROPERTIES LABELS "gpu;render" TIMEOUT 60)
endforeach()
# Sprites (Milestone 8B, part 5): pixel-art billboards in sunlight casting shadows (each walk frame, tinted, flipped, a
# coin facing the camera, one animating), and in a wall's shadow (in sun, half in shade, in shade). Scenes and sheet from
# tools/samples/make_test_sprites.py. Sprites_Coth (part 6): Coth from The Uncounted in a ring, every direction at once.
foreach(name IN ITEMS Sprites_Sun Sprites_Shade Sprites_Coth)
    add_test(NAME render.golden_${name}
        COMMAND ${CMAKE_COMMAND}
            -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/${name}.noorscene
            -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/${name}.png
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_${name}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
    set_tests_properties(render.golden_${name} PROPERTIES LABELS "gpu;render" TIMEOUT 60)
endforeach()
# 2D physics (the 2D foundations, part 1, ADR-040): rectangles and circles locked to the XY plane tumbling down a
# tilted floor against a wall, seen by an orthographic camera, four seconds in (tools/samples/make_2d_test_scenes.py).
add_test(NAME render.golden_Physics2D_Pile
    COMMAND ${CMAKE_COMMAND}
        -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/Physics2D_Pile.noorscene
        -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Physics2D_Pile.png -DFRAMES=240
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Physics2D_Pile
        -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
set_tests_properties(render.golden_Physics2D_Pile PROPERTIES LABELS "gpu;render" TIMEOUT 60)
# Sprites for 2D (the 2D foundations, part 3, ADR-040; tools/samples/make_2d_test_scenes.py): walkers at one depth
# drawn by sorting layer against scene order, coins by order, soft-edged glows over them; lit and unlit coins under a
# dim sun; a top-down crowd sorted by height on screen; a magnified frame beside a solid red one (no red may bleed in).
# Tilemaps (2D part 4): square (ground, wall, one-way planks, flowers, a walker), isometric (blocks drawn back to
# front), hexagonal, and 256 x 256 tiles drawn in 64 chunks.
foreach(name IN ITEMS Sprites2D_Layers Sprites2D_Unlit Sprites2D_YSort Sprites2D_Bleed Tilemap_Square Tilemap_Iso Tilemap_Hex Tilemap_Big)
    add_test(NAME render.golden_${name}
        COMMAND ${CMAKE_COMMAND}
            -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/${name}.noorscene
            -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/${name}.png
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_${name}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
    set_tests_properties(render.golden_${name} PROPERTIES LABELS "gpu;render" TIMEOUT 60)
endforeach()
# Cameras for 2D (part 6): a ball rolling down a slope, followed by a pixel-snapped camera (dead zone, smoothing, level
# bounds) past two parallax layers, four seconds in (tools/samples/make_2d_test_scenes.py).
add_test(NAME render.golden_Camera2D_Follow
    COMMAND ${CMAKE_COMMAND}
        -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/Camera2D_Follow.noorscene
        -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Camera2D_Follow.png -DFRAMES=240
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Camera2D_Follow
        -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
set_tests_properties(render.golden_Camera2D_Follow PROPERTIES LABELS "gpu;render" TIMEOUT 60)
# Display settings (2D part 6): a base resolution scaled up in whole steps, with bars or more of the world, and a
# render scale (tests/display_check.cmake, tests/tools/display_check.cpp).
add_executable(noor_display_check tools/display_check.cpp)
target_link_libraries(noor_display_check PRIVATE stb::image)
add_test(NAME render.display_stretch
    COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> -DCHECK=$<TARGET_FILE:noor_display_check>
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DWORK=${CMAKE_CURRENT_BINARY_DIR}/display_stretch
        -P ${CMAKE_CURRENT_SOURCE_DIR}/display_check.cmake)
set_tests_properties(render.display_stretch PROPERTIES LABELS "gpu;render" TIMEOUT 120)
add_test(NAME render.tilemap_batches
    COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/tilemap_batches -P ${CMAKE_CURRENT_SOURCE_DIR}/tilemap_batches_check.cmake)
set_tests_properties(render.tilemap_batches PROPERTIES LABELS "gpu;render" TIMEOUT 120 RUN_SERIAL TRUE)
# Gameplay particles (Milestone 8B, part 7): gravel landing and settling on a terrain (never through it) and drops
# that die on it and splash (a sub-emitter); five seconds in.
add_test(NAME render.golden_Particles_Terrain
    COMMAND ${CMAKE_COMMAND}
        -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/Particles_Terrain.noorscene
        -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Particles_Terrain.png -DFRAMES=300
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Particles_Terrain
        -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
set_tests_properties(render.golden_Particles_Terrain PROPERTIES LABELS "gpu;render" TIMEOUT 60)
# Skinned meshes (Milestone 8C, part 1): Ward bound in a T-pose, Ward posed by his joints' own transforms (arms down, a
# knee and an elbow bent), and the CC0 robot (rigid parts on bones, skinned hands), all casting shadows
# (tools/samples/add_skinning_scene.py); and what 100 skinned characters cost.
add_test(NAME render.golden_Skinning
    COMMAND ${CMAKE_COMMAND}
        -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/Skinning.noorscene
        -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Skinning.png
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Skinning
        -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
set_tests_properties(render.golden_Skinning PROPERTIES LABELS "gpu;render" TIMEOUT 60)
add_test(NAME render.skinning_cost
    COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/skinning_cost -P ${CMAKE_CURRENT_SOURCE_DIR}/skinning_cost_check.cmake)
set_tests_properties(render.skinning_cost PROPERTIES LABELS "gpu;render" TIMEOUT 120 RESOURCE_LOCK gpu_timing)
# Skeletal animation (Milestone 8C, part 2): three Wards and the CC0 robot each playing a clip on the scene's clock
# (walking, a wave held at its end, a nod; the robot walking), captured at a fixed frame (0.75 s), so every bone is at a
# known time (tools/samples/add_animation_scene.py); and what 100 walking characters cost.
add_test(NAME render.golden_Animation
    COMMAND ${CMAKE_COMMAND}
        -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/Animation.noorscene
        -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Animation.png -DFRAMES=45
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Animation
        -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
set_tests_properties(render.golden_Animation PROPERTIES LABELS "gpu;render" TIMEOUT 60)
# Blending and the controller (8C part 3): four Wards on Ward.nooranim (idle-walk, walk-run, walking and waving on a
# masked layer, running and leaning on an additive one) at three moments of a sequence.
foreach(frame IN ITEMS 20 50 80)
    add_test(NAME render.golden_Blending_${frame}
        COMMAND ${CMAKE_COMMAND}
            -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/Blending.noorscene
            -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Blending_${frame}.png -DFRAMES=${frame}
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Blending_${frame}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
    set_tests_properties(render.golden_Blending_${frame} PROPERTIES LABELS "gpu;render" TIMEOUT 60)
endforeach()
# Root motion (8C part 4): Ward walking across on root motion with a sword in his hand (a prop on a bone), and Ward in a
# character controller stopped by a wall, 2.5 s in.
add_test(NAME render.golden_Root_Motion
    COMMAND ${CMAKE_COMMAND}
        -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/Root_Motion.noorscene
        -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Root_Motion.png -DFRAMES=150
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Root_Motion
        -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
set_tests_properties(render.golden_Root_Motion PROPERTIES LABELS "gpu;render" TIMEOUT 60)
# Retargeting (8C part 5): Ward's clips and controller on Ward, Mast (tall, Mixamo names, T-pose, a 0.01 armature) and
# Bollard (stout, Rigify names, A-pose) side by side, walking and waving; a Bollard walking across on root motion.
add_test(NAME render.golden_Retargeting
    COMMAND ${CMAKE_COMMAND}
        -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/Retargeting.noorscene
        -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Retargeting.png -DFRAMES=40
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Retargeting
        -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
set_tests_properties(render.golden_Retargeting PROPERTIES LABELS "gpu;render" TIMEOUT 60)
add_test(NAME render.animation_cost
    COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/animation_cost -P ${CMAKE_CURRENT_SOURCE_DIR}/animation_cost_check.cmake)
set_tests_properties(render.animation_cost PROPERTIES LABELS "gpu;render" TIMEOUT 120 RESOURCE_LOCK gpu_timing)
# Skeletal animation from scripts (8C part 2): Ward waves, nods on the wave's end event and walks on at the nod's, from
# Lua in the project and in a built game, and from C# in the built game: the same moments, the same final pose.
if(TARGET noor_scripting_managed)
    add_test(NAME cli.animation_scripts_build_identically
        COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:noor_cli> -DPLAYER=$<TARGET_FILE:noor_player>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCRIPTS=${CMAKE_CURRENT_SOURCE_DIR}/data/animation_scripts
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/animation_scripts_build -P ${CMAKE_CURRENT_SOURCE_DIR}/animation_build_check.cmake)
    set_tests_properties(cli.animation_scripts_build_identically PROPERTIES LABELS "gpu;cli" TIMEOUT 240)
    # 8C part 3: a script drives Ward's controller (speed, a trigger, a bool, a parameter on an end event).
    add_test(NAME cli.animation_controller_build_identically
        COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:noor_cli> -DPLAYER=$<TARGET_FILE:noor_player>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCRIPTS=${CMAKE_CURRENT_SOURCE_DIR}/data/animation_scripts
            -DSCENE=Ward_Controller -DEXPECT_ANIMATION=Move -DEXPECT_ENDED=2
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/animation_controller_build -P ${CMAKE_CURRENT_SOURCE_DIR}/animation_build_check.cmake)
    set_tests_properties(cli.animation_controller_build_identically PROPERTIES LABELS "gpu;cli" TIMEOUT 240)
    # 8C part 4: root motion through a character controller, footstep events, a wall.
    add_test(NAME cli.root_motion_build_identically
        COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:noor_cli> -DPLAYER=$<TARGET_FILE:noor_player>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCRIPTS=${CMAKE_CURRENT_SOURCE_DIR}/data/animation_scripts
            -DSCENE=Ward_Walk -DEXPECT_ANIMATION=WalkForward -DEXPECT_ENDED=0 -DSAME_PLACE=ON
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/root_motion_build -P ${CMAKE_CURRENT_SOURCE_DIR}/animation_build_check.cmake)
    set_tests_properties(cli.root_motion_build_identically PROPERTIES LABELS "gpu;cli" TIMEOUT 240)
    # 8C part 5: the controller script on Bollard, with Ward's controller and clips retargeted (the built game must
    # carry Ward_Anim.glb, which only "retarget_from" names).
    add_test(NAME cli.retargeting_build_identically
        COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:noor_cli> -DPLAYER=$<TARGET_FILE:noor_player>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCRIPTS=${CMAKE_CURRENT_SOURCE_DIR}/data/animation_scripts
            -DSCENE=Bollard_Controller -DEXPECT_ANIMATION=Move -DEXPECT_ENDED=2
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/retargeting_build -P ${CMAKE_CURRENT_SOURCE_DIR}/animation_build_check.cmake)
    set_tests_properties(cli.retargeting_build_identically PROPERTIES LABELS "gpu;cli" TIMEOUT 240)
endif()
# Sprite animations from scripts (Milestone 8B, part 6): Coth walks, attacks and falls down on the attack's end event,
# from Lua in the project and in a built game (the same moments), and from C# in the built game.
if(TARGET noor_scripting_managed)
    add_test(NAME cli.sprite_scripts_build_identically
        COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:noor_cli> -DPLAYER=$<TARGET_FILE:noor_player>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCRIPTS=${CMAKE_CURRENT_SOURCE_DIR}/data/sprite_scripts
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sprite_scripts_build -P ${CMAKE_CURRENT_SOURCE_DIR}/sprites_build_check.cmake)
    set_tests_properties(cli.sprite_scripts_build_identically PROPERTIES LABELS "gpu;cli" TIMEOUT 240)
endif()
add_test(NAME render.particles_cost
    COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/particles_cost -P ${CMAKE_CURRENT_SOURCE_DIR}/particles_cost_check.cmake)
set_tests_properties(render.particles_cost PROPERTIES LABELS "gpu;render" TIMEOUT 120 RESOURCE_LOCK gpu_timing)
# A moving sun (Milestone 8A, part 2): the time of day dragged from dawn to dusk, the live sky rebuilt
# on the GPU every frame, 60 FPS held (tests/day_cycle_check.cmake). And the CPU reference path (part 1)
# must still match the same goldens the GPU path does.
add_test(NAME render.day_cycle
    COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/day_cycle_check -P ${CMAKE_CURRENT_SOURCE_DIR}/day_cycle_check.cmake)
set_tests_properties(render.day_cycle PROPERTIES LABELS "gpu;render" TIMEOUT 120 RESOURCE_LOCK gpu_timing)
# Environment assets (Milestone 8A, part 6): a custom .noorenv goes into a built game and looks the same there.
add_test(NAME cli.environment_builds_identically
    COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:noor_cli> -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DWORK=${CMAKE_CURRENT_BINARY_DIR}/environment_build
        -P ${CMAKE_CURRENT_SOURCE_DIR}/environment_build_check.cmake)
set_tests_properties(cli.environment_builds_identically PROPERTIES LABELS "gpu;cli" TIMEOUT 180)
# Effect particles (Milestone 8B, part 1): a campfire's particles replay bit for bit in a built game.
add_test(NAME cli.particles_build_identically
    COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:noor_cli> -DPLAYER=$<TARGET_FILE:noor_player>
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DWORK=${CMAKE_CURRENT_BINARY_DIR}/particles_build
        -P ${CMAKE_CURRENT_SOURCE_DIR}/particles_build_check.cmake)
set_tests_properties(cli.particles_build_identically PROPERTIES LABELS "gpu;cli" TIMEOUT 180)
# Weather zones (Milestone 8A, part 7): a built game's camera walks into a fog bank; the blend is checked frame by frame.
add_executable(noor_trace_check tools/trace_check.cpp)
target_link_libraries(noor_trace_check PRIVATE nlohmann_json::nlohmann_json)
noor_set_target_options(noor_trace_check)
add_test(NAME cli.weather_walk
    COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:noor_cli> -DPLAYER=$<TARGET_FILE:noor_player> -DCHECK=$<TARGET_FILE:noor_trace_check>
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DWORK=${CMAKE_CURRENT_BINARY_DIR}/weather_walk
        -P ${CMAKE_CURRENT_SOURCE_DIR}/weather_walk_check.cmake)
set_tests_properties(cli.weather_walk PROPERTIES LABELS "gpu;cli" TIMEOUT 180 RESOURCE_LOCK gpu_timing)
# Clouds (Milestone 8A, part 4): what they cost against the same scene without them.
add_test(NAME render.clouds_cost
    COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/clouds_cost -P ${CMAKE_CURRENT_SOURCE_DIR}/clouds_cost_check.cmake)
set_tests_properties(render.clouds_cost PROPERTIES LABELS "gpu;render" TIMEOUT 120 RESOURCE_LOCK gpu_timing)
# Volumetric clouds (part 7b, ADR-034): the same measurement with the first layer a volume (the half-resolution march).
add_test(NAME render.clouds_volume_cost
    COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes
        -DCLOUDS_SCENE=Atmosphere_Volume -DBUDGET_US=2500
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/clouds_volume_cost -P ${CMAKE_CURRENT_SOURCE_DIR}/clouds_cost_check.cmake)
set_tests_properties(render.clouds_volume_cost PROPERTIES LABELS "gpu;render" TIMEOUT 120 RESOURCE_LOCK gpu_timing)
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
add_test(NAME cli.csg_shared_between_scenes
    COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:noor_cli> -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/csg_project
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/csg_shared -P ${CMAKE_CURRENT_SOURCE_DIR}/csg_shared_check.cmake)
set_tests_properties(cli.csg_shared_between_scenes PROPERTIES LABELS "cli" TIMEOUT 120)
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
# Fog and aerial perspective (Milestone 8A, part 3): Hillside in haze (tools/samples/add_hillside_skies.py), the
# far hills fading into the sky without a seam.
add_test(NAME render.golden_Hillside_Haze
    COMMAND ${CMAKE_COMMAND}
        -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
        -DPROJECT=${PROJECT_SOURCE_DIR}/samples/Hillside -DSCENE=Scenes/Hillside_Haze.noorscene
        -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Hillside_Haze.png
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Hillside_Haze
        -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
set_tests_properties(render.golden_Hillside_Haze PROPERTIES LABELS "gpu;render;sample" TIMEOUT 180 RESOURCE_LOCK hillside_cache
                     FIXTURES_REQUIRED hillside_cooked)
# Night (Milestone 8A, part 5): Hillside at 22:00 under a full moon, automatic exposure; the house and the trees must
# read against the sky.
add_test(NAME render.golden_Hillside_Night
    COMMAND ${CMAKE_COMMAND}
        -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
        -DPROJECT=${PROJECT_SOURCE_DIR}/samples/Hillside -DSCENE=Scenes/Hillside_Night.noorscene
        -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Hillside_Night.png
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Hillside_Night
        -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
set_tests_properties(render.golden_Hillside_Night PROPERTIES LABELS "gpu;render;sample" TIMEOUT 180 RESOURCE_LOCK hillside_cache
                     FIXTURES_REQUIRED hillside_cooked)
# The 8A sample (Milestone 8A, part 8): Hillside under every built-in environment preset (applied through the editor
# API by tools/samples/add_hillside_presets.py), and on an airless world with a ringed planet and two suns.
foreach(name IN ITEMS ClearNoon FairWeather GoldenEvening Dusk Overcast FoggyDawn PinkMist MoonlitNight StormyCoast Space)
    add_test(NAME render.golden_Hillside_${name}
        COMMAND ${CMAKE_COMMAND}
            -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
            -DPROJECT=${PROJECT_SOURCE_DIR}/samples/Hillside -DSCENE=Scenes/Presets/Hillside_${name}.noorscene
            -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Hillside_${name}.png
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Hillside_${name}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
    set_tests_properties(render.golden_Hillside_${name} PROPERTIES LABELS "gpu;render;sample" TIMEOUT 180 RESOURCE_LOCK hillside_cache
                         FIXTURES_REQUIRED hillside_cooked)
endforeach()
# Weather particles (Milestone 8B, part 3): Hillside in rain and in snow (tools/samples/add_hillside_weather.py), half a
# second in.
foreach(name IN ITEMS Rain Snow)
    add_test(NAME render.golden_Hillside_${name}
        COMMAND ${CMAKE_COMMAND}
            -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
            -DPROJECT=${PROJECT_SOURCE_DIR}/samples/Hillside -DSCENE=Scenes/Weather/Hillside_${name}.noorscene
            -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Hillside_${name}.png -DFRAMES=30
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Hillside_${name}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
    set_tests_properties(render.golden_Hillside_${name} PROPERTIES LABELS "gpu;render;sample" TIMEOUT 180 RESOURCE_LOCK hillside_cache
                         FIXTURES_REQUIRED hillside_cooked)
endforeach()
# The 8B sample (Milestone 8B, part 8): a stormy evening on Hillside (tools/samples/add_hillside_storm.py): the storm
# rolls in (a weather zone faded in by a script: rain, wind, low cloud, fog), a campfire (the Fire, Smoke and Sparks
# presets, the sparks landing on the hillside) and Coth walking round it (an 8-direction sprite on a character
# controller). Five and a half seconds in.
add_test(NAME render.golden_Hillside_Storm
    COMMAND ${CMAKE_COMMAND}
        -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
        -DPROJECT=${PROJECT_SOURCE_DIR}/samples/Hillside -DSCENE=Scenes/Hillside_Storm.noorscene
        -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Hillside_Storm.png -DFRAMES=330
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Hillside_Storm
        -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
set_tests_properties(render.golden_Hillside_Storm PROPERTIES LABELS "gpu;render;sample" TIMEOUT 240 RESOURCE_LOCK "hillside_cache;gpu_timing"
                     FIXTURES_REQUIRED hillside_cooked)
# The 8C sample (Milestone 8C, part 8): Alden Wayfarer wandering about Hillside on root motion, his staff on a
# socket (tools/samples/add_hillside_alden.py). Five seconds in: walking towards the camera.
# The 8D sample (tools/samples/add_hillside_water.py): the lake in light rain, a stream carved down into it, four crates
# floating, Alden swimming across. Ten seconds in: he's swimming between the crates.
add_test(NAME render.golden_Hillside_Water
    COMMAND ${CMAKE_COMMAND}
        -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
        -DPROJECT=${PROJECT_SOURCE_DIR}/samples/Hillside -DSCENE=Scenes/Hillside_Water.noorscene
        -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Hillside_Water.png -DFRAMES=600
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Hillside_Water
        -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
set_tests_properties(render.golden_Hillside_Water PROPERTIES LABELS "gpu;render;sample" TIMEOUT 240 RESOURCE_LOCK "hillside_cache;gpu_timing"
                     FIXTURES_REQUIRED hillside_cooked)
add_test(NAME render.golden_Hillside_Alden
    COMMAND ${CMAKE_COMMAND}
        -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
        -DPROJECT=${PROJECT_SOURCE_DIR}/samples/Hillside -DSCENE=Scenes/Hillside_Alden.noorscene
        -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Hillside_Alden.png -DFRAMES=300
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Hillside_Alden
        -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
set_tests_properties(render.golden_Hillside_Alden PROPERTIES LABELS "gpu;render;sample" TIMEOUT 240 RESOURCE_LOCK "hillside_cache;gpu_timing"
                     FIXTURES_REQUIRED hillside_cooked)
# Water (Milestone 8D, part 1, ADR-037): a tiled pool with a ramp floor (a ball half in it, a block on the deep end), and
# the 8D lake on Hillside (tools/samples/add_hillside_lake.py: a hollow sculpted in its own layer, the "Lake" preset).
add_test(NAME render.golden_Water_Pool
    COMMAND ${CMAKE_COMMAND}
        -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/Water_Pool.noorscene
        -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Water_Pool.png -DFRAMES=30
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Water_Pool
        -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
set_tests_properties(render.golden_Water_Pool PROPERTIES LABELS "gpu;render" TIMEOUT 60)
# An endless sea (part 1's flat surface to the horizon, fogged with distance) by a beach, a pier and a rock.
add_test(NAME render.golden_Water_Sea
    COMMAND ${CMAKE_COMMAND}
        -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/Water_Sea.noorscene
        -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Water_Sea.png -DFRAMES=30
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Water_Sea
        -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
set_tests_properties(render.golden_Water_Sea PROPERTIES LABELS "gpu;render" TIMEOUT 60)
# Waves (Milestone 8D, part 2): the Sea preset with four buoys floated by Lua on noor.water_height, at two moments (one
# and four seconds in): the waves have moved on and the buoys with them.
foreach(moment IN ITEMS 1s:60 4s:240)
    string(REPLACE ":" ";" moment "${moment}")
    list(GET moment 0 label)
    list(GET moment 1 frames)
    add_test(NAME render.golden_Water_Waves_${label}
        COMMAND ${CMAKE_COMMAND}
            -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/Water_Waves.noorscene
            -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Water_Waves_${label}.png -DFRAMES=${frames}
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Water_Waves_${label}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
    set_tests_properties(render.golden_Water_Waves_${label} PROPERTIES LABELS "gpu;render" TIMEOUT 60)
endforeach()
# Shorelines and reflections (Milestone 8D, part 3): the Sea preset round a sandy coast (a noise terrain sunk into it:
# waves calmed by the depth map, foam along the shore and round three posts, wet sand), and a calm pond with a house on
# its far bank, reflected by screen-space reflection (its roof runs off the top of the screen, where the sky takes over).
foreach(scene Water_Shore Water_Reflection)
    add_test(NAME render.golden_${scene}
        COMMAND ${CMAKE_COMMAND}
            -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/${scene}.noorscene
            -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/${scene}.png -DFRAMES=60
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_${scene}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
    set_tests_properties(render.golden_${scene} PROPERTIES LABELS "gpu;render" TIMEOUT 60)
endforeach()
# Underwater (Milestone 8D, part 4): under the pond looking up at the house (Snell's window), the deep sea from 18 m
# down, and the camera half in the pond (the waterline across the picture).
foreach(scene Water_Underwater Water_DeepSea Water_Waterline)
    add_test(NAME render.golden_${scene}
        COMMAND ${CMAKE_COMMAND}
            -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/${scene}.noorscene
            -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/${scene}.png -DFRAMES=60
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_${scene}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
    set_tests_properties(render.golden_${scene} PROPERTIES LABELS "gpu;render" TIMEOUT 60)
endforeach()
# Rivers (Milestone 8D, part 6): a river down a generated valley (Assets/Terrain/river_valley.png: a meandering channel,
# a steep stretch, a basin), a crate and a barrel carried down it, and where it runs into the lake.
foreach(scene Water_River Water_River_Mouth)
    add_test(NAME render.golden_${scene}
        COMMAND ${CMAKE_COMMAND}
            -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/${scene}.noorscene
            -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/${scene}.png -DFRAMES=180
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_${scene}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
    set_tests_properties(render.golden_${scene} PROPERTIES LABELS "gpu;render" TIMEOUT 90)
endforeach()
# A camera walked down through the pond's surface: no pop (tests/water_descent_check.cmake).
add_test(NAME render.water_descent
    COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/water_descent -P ${CMAKE_CURRENT_SOURCE_DIR}/water_descent_check.cmake)
set_tests_properties(render.water_descent PROPERTIES LABELS "gpu;render" TIMEOUT 300)
# What gameplay reads is what's drawn: the depth read back under the waves against water_height (within 1 cm, 20 m).
add_test(NAME render.water_readback
    COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/water_readback -P ${CMAKE_CURRENT_SOURCE_DIR}/water_waves_check.cmake)
set_tests_properties(render.water_readback PROPERTIES LABELS "gpu;render" TIMEOUT 60)
# 8D part 3: the same over a beach, where the depth map calms the waves (drawn and gameplay alike).
add_test(NAME render.water_readback_shore
    COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes
        -DSCENE=Scenes/Water_Shore_Probe.noorscene -DSHALLOW=2.5
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/water_readback_shore -P ${CMAKE_CURRENT_SOURCE_DIR}/water_waves_check.cmake)
set_tests_properties(render.water_readback_shore PROPERTIES LABELS "gpu;render" TIMEOUT 60)
# ... and a built game reads the same: the buoys float identically in the project and in the built render-scenes game.
add_test(NAME cli.build_render_scenes
    COMMAND $<TARGET_FILE:noor_cli> build ${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes ${CMAKE_CURRENT_BINARY_DIR}/built_render_scenes_game)
set_tests_properties(cli.build_render_scenes PROPERTIES LABELS "cli" TIMEOUT 180 FIXTURES_SETUP built_render_scenes_game)
add_test(NAME player.water_waves
    COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> "-DGAME=${CMAKE_CURRENT_BINARY_DIR}/built_render_scenes_game/Render Scenes.exe"
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DWORK=${CMAKE_CURRENT_BINARY_DIR}/player_water_waves
        -P ${CMAKE_CURRENT_SOURCE_DIR}/water_waves_check.cmake)
set_tests_properties(player.water_waves PROPERTIES LABELS "gpu;player" TIMEOUT 180 FIXTURES_REQUIRED built_render_scenes_game)
# Water in the game (Milestone 8D, part 5): floating bodies and splashes the same in the built game, and the scene as a
# golden (the Sea in the rain: rings on the water, bodies riding the waves, pebbles splashing).
add_test(NAME player.water_float
    COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> "-DGAME=${CMAKE_CURRENT_BINARY_DIR}/built_render_scenes_game/Render Scenes.exe"
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DWORK=${CMAKE_CURRENT_BINARY_DIR}/player_water_float
        -P ${CMAKE_CURRENT_SOURCE_DIR}/water_float_check.cmake)
set_tests_properties(player.water_float PROPERTIES LABELS "gpu;player" TIMEOUT 180 FIXTURES_REQUIRED built_render_scenes_game)
add_test(NAME render.golden_Water_Float
    COMMAND ${CMAKE_COMMAND}
        -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/render_scenes -DSCENE=Scenes/Water_Float.noorscene
        -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Water_Float.png -DFRAMES=300
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Water_Float
        -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
set_tests_properties(render.golden_Water_Float PROPERTIES LABELS "gpu;render" TIMEOUT 90)
add_test(NAME render.golden_Hillside_Lake
    COMMAND ${CMAKE_COMMAND}
        -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
        -DPROJECT=${PROJECT_SOURCE_DIR}/samples/Hillside -DSCENE=Scenes/Hillside_Lake.noorscene
        -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/Hillside_Lake.png -DFRAMES=30
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_Hillside_Lake
        -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
set_tests_properties(render.golden_Hillside_Lake PROPERTIES LABELS "gpu;render;sample" TIMEOUT 180 RESOURCE_LOCK hillside_cache
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
# The 8B sample (Milestone 8B, part 8): the stormy evening plays the same in the built game as in the project
# (particles to the bit, spark landings, Coth walking).
add_test(NAME player.hillside_storm
    COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> "-DGAME=${CMAKE_CURRENT_BINARY_DIR}/built_hillside_game/Hillside.exe"
        -DPROJECT=${PROJECT_SOURCE_DIR}/samples/Hillside -DWORK=${CMAKE_CURRENT_BINARY_DIR}/player_hillside_storm
        -P ${CMAKE_CURRENT_SOURCE_DIR}/hillside_storm_check.cmake)
set_tests_properties(player.hillside_storm PROPERTIES LABELS "gpu;player;sample" TIMEOUT 240 FIXTURES_REQUIRED "built_hillside_game;hillside_cooked"
                     RESOURCE_LOCK "hillside_cache;gpu_timing")  # heavy: not beside the timing tests
# The 8C sample (Milestone 8C, part 8): Alden plays the same in the built game as in the project (every bone, his
# place to the millimetre, fifteen seconds in).
add_test(NAME player.hillside_alden
    COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> "-DGAME=${CMAKE_CURRENT_BINARY_DIR}/built_hillside_game/Hillside.exe"
        -DPROJECT=${PROJECT_SOURCE_DIR}/samples/Hillside -DWORK=${CMAKE_CURRENT_BINARY_DIR}/player_hillside_alden
        -P ${CMAKE_CURRENT_SOURCE_DIR}/hillside_alden_check.cmake)
set_tests_properties(player.hillside_alden PROPERTIES LABELS "gpu;player;sample" TIMEOUT 240 FIXTURES_REQUIRED "built_hillside_game;hillside_cooked"
                     RESOURCE_LOCK "hillside_cache;gpu_timing")
# The 8D sample (Milestone 8D, part 8): the lake, its stream, crates and Alden swimming play the same in the built game.
add_test(NAME player.hillside_water
    COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> "-DGAME=${CMAKE_CURRENT_BINARY_DIR}/built_hillside_game/Hillside.exe"
        -DPROJECT=${PROJECT_SOURCE_DIR}/samples/Hillside -DWORK=${CMAKE_CURRENT_BINARY_DIR}/player_hillside_water
        -P ${CMAKE_CURRENT_SOURCE_DIR}/hillside_water_check.cmake)
set_tests_properties(player.hillside_water PROPERTIES LABELS "gpu;player;sample" TIMEOUT 240 FIXTURES_REQUIRED "built_hillside_game;hillside_cooked"
                     RESOURCE_LOCK "hillside_cache;gpu_timing")
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

    # Spawning, teleport, groups and require from Lua and C# (gameplay foundations, part 1), in a project and a built game.
    add_test(NAME cli.build_spawn_project
        COMMAND $<TARGET_FILE:noor_cli> build ${CMAKE_CURRENT_SOURCE_DIR}/data/spawn_project ${CMAKE_CURRENT_BINARY_DIR}/built_spawn_project)
    set_tests_properties(cli.build_spawn_project PROPERTIES LABELS "cli" TIMEOUT 180 FIXTURES_SETUP built_spawn_project
                         PASS_REGULAR_EXPRESSION "Built 'Spawn Test'")
    add_test(NAME player.spawn_lua_csharp
        COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> "-DGAME=${CMAKE_CURRENT_BINARY_DIR}/built_spawn_project/Spawn Test.exe"
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/data/spawn_project -DWORK=${CMAKE_CURRENT_BINARY_DIR}/player_spawn
            -P ${CMAKE_CURRENT_SOURCE_DIR}/spawn_check.cmake)
    set_tests_properties(player.spawn_lua_csharp PROPERTIES LABELS "gpu;player" TIMEOUT 120 FIXTURES_REQUIRED built_spawn_project)

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

# ---- Milestone 9A, part 1: the yardsticks -------------------------------------
# Milestone 9 adds big worlds; small games must not pay for them (M9_PLAN.md, principle 0). NoorPong is the 2D yardstick:
# an orthographic camera, a sprite, Lua. A golden two seconds in, and the built game plays the same game.
if(WIN32)
    add_test(NAME render.golden_NoorPong
        COMMAND ${CMAKE_COMMAND}
            -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
            -DPROJECT=${PROJECT_SOURCE_DIR}/samples/NoorPong -DSCENE=Scenes/Pong.noorscene
            -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/NoorPong.png -DFRAMES=150
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_NoorPong
            -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
    set_tests_properties(render.golden_NoorPong PROPERTIES LABELS "gpu;render;sample" TIMEOUT 120)
    add_test(NAME cli.build_noorpong
        COMMAND $<TARGET_FILE:noor_cli> build ${PROJECT_SOURCE_DIR}/samples/NoorPong ${CMAKE_CURRENT_BINARY_DIR}/built_noorpong)
    set_tests_properties(cli.build_noorpong PROPERTIES LABELS "cli;sample" TIMEOUT 120 FIXTURES_SETUP built_noorpong
                         PASS_REGULAR_EXPRESSION "Built 'NoorPong'")
    add_test(NAME player.noorpong
        COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> "-DGAME=${CMAKE_CURRENT_BINARY_DIR}/built_noorpong/NoorPong.exe"
            -DPROJECT=${PROJECT_SOURCE_DIR}/samples/NoorPong -DWORK=${CMAKE_CURRENT_BINARY_DIR}/player_noorpong
            -P ${CMAKE_CURRENT_SOURCE_DIR}/noorpong_check.cmake)
    set_tests_properties(player.noorpong PROPERTIES LABELS "gpu;player;sample" TIMEOUT 120 FIXTURES_REQUIRED built_noorpong)

    # The 2D samples (2D part 7): NoorPlatformer (Tom's own art, a tilemap level, the platformer character) mid-level,
    # and finished by its autopilot; NoorIsometric (an isometric room, Coth half behind a pillar: "y_depth").
    foreach(entry IN ITEMS "NoorPlatformer|Scenes/Level.noorscene|300" "NoorIsometric|Scenes/Room.noorscene|420")
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 name)
        list(GET parts 1 scene)
        list(GET parts 2 frames)
        add_test(NAME render.golden_${name}
            COMMAND ${CMAKE_COMMAND}
                -DPLAYER=$<TARGET_FILE:noor_player> -DCOMPARE=$<TARGET_FILE:noor_image_compare>
                -DPROJECT=${PROJECT_SOURCE_DIR}/samples/${name} -DSCENE=${scene}
                -DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/golden/${name}.png -DFRAMES=${frames} -DEXTRA=--windowed
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render_golden_${name}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/render_golden.cmake)
        set_tests_properties(render.golden_${name} PROPERTIES LABELS "gpu;render;sample" TIMEOUT 120)
    endforeach()
    add_test(NAME sample.noorplatformer_finishes
        COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> -DPROJECT=${PROJECT_SOURCE_DIR}/samples/NoorPlatformer
            -DSCENE=Scenes/Level.noorscene -DFRAMES=900 "-DEXPECT=You made it!"
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sample_noorplatformer_finishes -P ${CMAKE_CURRENT_SOURCE_DIR}/sample_finish_check.cmake)
    set_tests_properties(sample.noorplatformer_finishes PROPERTIES LABELS "gpu;player;sample" TIMEOUT 120)
    # ...and each plays exactly the same in a built game (the hero's place and the HUD, to the bit, eight seconds in).
    foreach(entry IN ITEMS "NoorPlatformer|Hero" "NoorIsometric|Coth")
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 name)
        list(GET parts 1 who)
        string(TOLOWER ${name} lower)
        add_test(NAME cli.build_${lower}
            COMMAND $<TARGET_FILE:noor_cli> build ${PROJECT_SOURCE_DIR}/samples/${name} ${CMAKE_CURRENT_BINARY_DIR}/built_${lower})
        set_tests_properties(cli.build_${lower} PROPERTIES LABELS "cli;sample" TIMEOUT 120 FIXTURES_SETUP built_${lower}
                             PASS_REGULAR_EXPRESSION "Built '${name}'")
        add_test(NAME player.${lower}_built_same
            COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> "-DGAME=${CMAKE_CURRENT_BINARY_DIR}/built_${lower}/${name}.exe"
                -DPROJECT=${PROJECT_SOURCE_DIR}/samples/${name} -DFRAMES=480 -DENTITY=${who}
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/player_${lower}_built -P ${CMAKE_CURRENT_SOURCE_DIR}/built_same_check.cmake)
        set_tests_properties(player.${lower}_built_same PROPERTIES LABELS "gpu;player;sample" TIMEOUT 120 FIXTURES_REQUIRED built_${lower})
    endforeach()
    # Project templates (2D part 7): each makes a project that plays.
    foreach(template IN ITEMS Empty2D Platformer2D TopDown2D)
        add_test(NAME cli.template_${template}
            COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:noor_cli> -DPLAYER=$<TARGET_FILE:noor_player> -DTEMPLATE=${template}
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/template_${template} -P ${CMAKE_CURRENT_SOURCE_DIR}/template_check.cmake)
        set_tests_properties(cli.template_${template} PROPERTIES LABELS "gpu;cli;template" TIMEOUT 120)
    endforeach()

    # Each yardstick against its baseline (tests/yardsticks.json, tools/samples/measure_yardsticks.py). Timing tests: run
    # alone, so a busy machine doesn't fail them.
    foreach(entry IN ITEMS "cube|tests/data/yardstick_cube|Scenes/Main.noorscene"
                           "pong|samples/NoorPong|Scenes/Pong.noorscene"
                           "fps|samples/NoorFPS-lite|Scenes/Main_Lua.noorscene"
                           "hillside|samples/Hillside|Scenes/Hillside.noorscene"
                           "platformer|samples/NoorPlatformer|Scenes/Level.noorscene"
                           "isometric|samples/NoorIsometric|Scenes/Room.noorscene")
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 name)
        list(GET parts 1 project)
        list(GET parts 2 scene)
        add_test(NAME yardstick.${name}
            COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> -DPROJECT=${PROJECT_SOURCE_DIR}/${project}
                -DSCENE=${scene} -DNAME=${name} -DBASELINE=${CMAKE_CURRENT_SOURCE_DIR}/yardsticks.json
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/yardstick_${name}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/yardstick_check.cmake)
        set_tests_properties(yardstick.${name} PROPERTIES LABELS "gpu;yardstick" TIMEOUT 120 RUN_SERIAL TRUE)
    endforeach()
    set_tests_properties(yardstick.hillside PROPERTIES FIXTURES_REQUIRED hillside_cooked RESOURCE_LOCK hillside_cache)
    # The check must catch a small game getting slower: the cube with a deliberate 1 ms stall in every update fails it.
    add_test(NAME yardstick.catches_a_stall
        COMMAND ${CMAKE_COMMAND} -DPLAYER=$<TARGET_FILE:noor_player> -DPROJECT=${PROJECT_SOURCE_DIR}/tests/data/yardstick_cube
            -DSCENE=Scenes/Main.noorscene -DNAME=cube -DBASELINE=${CMAKE_CURRENT_SOURCE_DIR}/yardsticks.json
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/yardstick_stall -DSTALL_MS=1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/yardstick_check.cmake)
    set_tests_properties(yardstick.catches_a_stall PROPERTIES LABELS "gpu;yardstick" TIMEOUT 120 RUN_SERIAL TRUE
                         PASS_REGULAR_EXPRESSION "cpu_median_ms [0-9.]+ [(]baseline [0-9.]+, limit [0-9.]+[)] OVER")
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
