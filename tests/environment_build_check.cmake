# An environment asset goes into a built game and looks the same there (Milestone 8A, part 6): a copy of
# render_scenes gets a custom .noorenv (Golden Evening with its own haze and clouds), a scene that points at it by id
# with one field overridden, then noor_player draws that scene from the project and from a game built by noor_cli; the
# two pictures and exposures must match.
#   -DCLI=<noor_cli> -DPLAYER=<noor_player> -DCOMPARE=<noor_image_compare> -DPROJECT=<render_scenes> -DWORK=<folder>
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
file(COPY "${PROJECT}/" DESTINATION "${WORK}/project")
file(REMOVE_RECURSE "${WORK}/project/.noor")
set(project "${WORK}/project")

# The environment asset.
file(MAKE_DIRECTORY "${project}/Assets/Environments")
file(WRITE "${project}/Assets/Environments/Custom Evening.noorenv" [=[{
  "noor_environment": 1,
  "name": "Custom Evening",
  "preset": "Golden Evening",
  "environment": {
    "sky": "atmosphere", "time_of_day": 18.6, "latitude": 50, "day_of_year": 220, "north": 200,
    "turbidity": 2.5, "ground_albedo": 0.25, "stars": true, "background": true, "ev100": "auto",
    "fog_density": 0.004, "fog_height": 1, "fog_falloff": 0.1, "fog_anisotropy": 0.7, "fog_color": "#FFFFFF",
    "cloud_coverage": 0.4, "cloud_density": 5, "cloud_altitude": 1500, "cloud_scale": 2500,
    "cloud_wind_speed": 0, "cloud_wind_direction": 0, "cloud_seed": 6
  }
}
]=])
execute_process(COMMAND "${CLI}" scan "${project}" RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "noor_cli scan failed (${code}): ${out}${err}")
endif()
file(READ "${project}/Assets/Environments/Custom Evening.noorenv.noormeta" meta)
string(JSON importer GET "${meta}" importer)
string(JSON guid GET "${meta}" guid)
if(NOT importer STREQUAL "environment")
    message(FATAL_ERROR "the .noorenv was registered as '${importer}', not 'environment'")
endif()

# A scene pointing at it, with one override (the time) beside the reference.
file(READ "${project}/Scenes/Atmosphere_Noon.noorscene" scene)
string(JSON count LENGTH "${scene}" entities)
math(EXPR last "${count} - 1")
foreach(i RANGE ${last})
    string(JSON name GET "${scene}" entities ${i} name)
    if(name STREQUAL "Sky")
        string(JSON scene SET "${scene}" entities ${i} components environment "{\"asset\": \"${guid}\", \"time_of_day\": 18.8}")
    endif()
endforeach()
string(JSON scene SET "${scene}" id "\"a7e50000-0007-4000-8000-000000000000\"")
string(JSON scene SET "${scene}" name "\"Custom\"")
file(WRITE "${project}/Scenes/Custom.noorscene" "${scene}")

# Drawn from the project...
execute_process(COMMAND "${PLAYER}" "${project}" --scene Scenes/Custom.noorscene --frames 5 --no-audio --no-input --validation
                        --screenshot "${WORK}/project.bmp" --report "${WORK}/project.json" --log "${WORK}/project.log"
                RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
if(NOT code EQUAL 0 OR NOT EXISTS "${WORK}/project.bmp")
    message(FATAL_ERROR "noor_player failed on the project (${code}); see ${WORK}/project.log")
endif()
# ...and from a built game.
execute_process(COMMAND "${CLI}" build "${project}" "${WORK}/game" RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "noor_cli build failed (${code}): ${out}${err}")
endif()
file(GLOB exe "${WORK}/game/*.exe")
execute_process(COMMAND "${exe}" --scene Scenes/Custom.noorscene --frames 5 --no-audio --no-input --validation
                        --screenshot "${WORK}/game.bmp" --report "${WORK}/game.json" --log "${WORK}/game.log"
                RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
if(NOT code EQUAL 0 OR NOT EXISTS "${WORK}/game.bmp")
    message(FATAL_ERROR "the built game failed (${code}); see ${WORK}/game.log")
endif()

file(READ "${WORK}/project.json" a)
file(READ "${WORK}/game.json" b)
string(JSON ev_a GET "${a}" ev100)
string(JSON ev_b GET "${b}" ev100)
string(JSON errors_a GET "${a}" errors)
string(JSON errors_b GET "${b}" errors)
message(STATUS "exposure: project ${ev_a}, built game ${ev_b}")
if(NOT errors_a EQUAL 0 OR NOT errors_b EQUAL 0)
    message(FATAL_ERROR "errors logged (project ${errors_a}, game ${errors_b})")
endif()
if(NOT ev_a STREQUAL ev_b)
    message(FATAL_ERROR "the exposure differs: project ${ev_a}, built game ${ev_b}")
endif()
# The same picture: the project's screenshot is the reference, the game's must match it closely.
execute_process(COMMAND "${COMPARE}" "${WORK}/project.bmp" "${WORK}/project.png" --update --size 640x360 RESULT_VARIABLE code)
execute_process(COMMAND "${COMPARE}" "${WORK}/game.bmp" "${WORK}/project.png" --tolerance 3 --max-bad 0.1 --diff "${WORK}/diff.png"
                RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
message(STATUS "${out}${err}")
if(NOT code EQUAL 0)
    message(FATAL_ERROR "the built game's picture differs from the project's: see ${WORK}/diff.png")
endif()
