# CLI workflow test: drives noor_cli the way a user or CI script would.
# Inputs: CLI (noor_cli path), DATA (tests/data), WORK (scratch folder).

function(run expected)
    execute_process(COMMAND ${CLI} ${ARGN} RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
    message(STATUS "noor_cli ${ARGN} -> ${code}\n${out}${err}")
    if(NOT code EQUAL expected)
        message(FATAL_ERROR "expected exit code ${expected}, got ${code}")
    endif()
    set(LAST_OUT "${out}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${WORK}")
run(0 new "${WORK}/Game" Game)

file(COPY "${DATA}/badbuilding/osm_w117821721.glb"
          "${DATA}/badbuilding/osm_w117821721.noor.json"
          "${DATA}/badbuilding/osm_w117821721.recipe.json"
     DESTINATION "${WORK}/Game/Assets/Buildings")

run(0 info "${WORK}/Game/Assets/Buildings/osm_w117821721.glb")
if(NOT LAST_OUT MATCHES "render triangles 7432")
    message(FATAL_ERROR "info did not report 7432 render triangles")
endif()

run(0 scan "${WORK}/Game")
if(NOT LAST_OUT MATCHES "4529993a-ec0c-5a66-bc58-462a7823fbc6")
    message(FATAL_ERROR "scan did not use the Noor Interchange id")
endif()

run(0 add-model "${WORK}/Game" Assets/Buildings/osm_w117821721.glb Scenes/Main.noorscene)
run(0 validate "${WORK}/Game")

# Prefabs: turn the placed building into a prefab, place two more copies, validate the expansion.
run(0 make-prefab "${WORK}/Game" Scenes/Main.noorscene osm_w117821721 Assets/Prefabs/Block.noorprefab)
run(0 add-prefab "${WORK}/Game" Assets/Prefabs/Block.noorprefab Scenes/Main.noorscene 60 0 0 90)
run(0 add-prefab "${WORK}/Game" Assets/Prefabs/Block.noorprefab Scenes/Main.noorscene -60 0 0 -90)
run(0 validate "${WORK}/Game")
if(NOT LAST_OUT MATCHES "after expanding 3 prefab instance")
    message(FATAL_ERROR "validate did not expand 3 prefab instances")
endif()

# Move the building behind the engine's back: validate must still pass (meta follows by content).
file(MAKE_DIRECTORY "${WORK}/Game/Assets/Moved")
file(RENAME "${WORK}/Game/Assets/Buildings/osm_w117821721.glb" "${WORK}/Game/Assets/Moved/block.glb")
run(0 validate "${WORK}/Game")

# Delete it: validate must fail and say why.
file(REMOVE "${WORK}/Game/Assets/Moved/block.glb")
run(1 validate "${WORK}/Game")
if(NOT LAST_OUT MATCHES "missing asset")
    message(FATAL_ERROR "validate did not report the missing asset")
endif()

# Format migration: an old (format 1) scene is reported, upgraded with a backup, then valid.
file(WRITE "${WORK}/Game/Scenes/Old.noorscene" [=[{
  "noor_scene": 1,
  "id": "11111111-2222-4333-8444-555555555555",
  "name": "Old",
  "entities": [{"id": "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee", "name": "Box", "parent": null,
                "transform": {"position": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]},
                "components": {}}]
}
]=])
run(0 migrate "${WORK}/Game" --dry-run)
if(NOT LAST_OUT MATCHES "would upgrade Scenes/Old.noorscene")
    message(FATAL_ERROR "migrate --dry-run did not report the old scene")
endif()
run(0 migrate "${WORK}/Game")
if(NOT EXISTS "${WORK}/Game/Scenes/Old.noorscene.bak")
    message(FATAL_ERROR "migrate did not keep a backup")
endif()
file(READ "${WORK}/Game/Scenes/Old.noorscene" upgraded)
string(FIND "${upgraded}" "\"noor_scene\": 2" found)
if(found EQUAL -1)
    message(FATAL_ERROR "scene was not upgraded to format 2")
endif()

# Path escapes are refused.
run(1 add-model "${WORK}/Game" ../outside.glb Scenes/Main.noorscene)
message(STATUS "CLI workflow: all steps behaved as expected")
