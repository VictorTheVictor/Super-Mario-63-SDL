cmake_minimum_required(VERSION 3.24)
project(SuperMario63Native VERSION 0.1.0 LANGUAGES CXX)

find_package(SDL3 3.4 CONFIG REQUIRED)
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" -c "from PIL import Image; assert hasattr(Image, 'Transform')"
    RESULT_VARIABLE PILLOW_RESULT OUTPUT_QUIET ERROR_QUIET)
if(NOT PILLOW_RESULT EQUAL 0)
    message(FATAL_ERROR "The XFL artwork importer needs Pillow >=9.1 in ${Python3_EXECUTABLE}. Install it for that interpreter and configure again.")
endif()

set(XFL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Super_Mario_63_2010")
set(PREVIEW_SYMBOL "${XFL_ROOT}/LIBRARY/Cinema 2 Mario (No fludd) Walking.xml")
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(GLOB_RECURSE EXPORTED_IMAGES CONFIGURE_DEPENDS "${XFL_ROOT}/LIBRARY/*.png")
add_custom_command(
    OUTPUT "${GENERATED_DIR}/assets.hpp"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_preview.py"
            "${XFL_ROOT}" "${PREVIEW_SYMBOL}" "${GENERATED_DIR}/assets.hpp"
    DEPENDS tools/import_preview.py "${XFL_ROOT}/DOMDocument.xml"
            "${PREVIEW_SYMBOL}" ${EXPORTED_IMAGES}
    VERBATIM
    COMMENT "Importing original XFL bitmap animation and PNG catalog")

set(LEVEL_AREA "${XFL_ROOT}/LIBRARY/Course Clips/1 - Bob-omb Battlefield/Areas 1-1 & 1-2")
add_custom_command(
    OUTPUT "${GENERATED_DIR}/level.hpp"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_level.py"
            "${XFL_ROOT}" "${GENERATED_DIR}/level.hpp"
    DEPENDS tools/import_level.py "${XFL_ROOT}/DOMDocument.xml"
            "${LEVEL_AREA}/1-1 Platforms.xml" "${LEVEL_AREA}/1-1 Bplatforms.xml"
    VERBATIM
    COMMENT "Importing Bob-omb Battlefield collision geometry")
add_custom_target(level_data DEPENDS "${GENERATED_DIR}/level.hpp")
file(GLOB_RECURSE AREA_ART_XML CONFIGURE_DEPENDS
    "${XFL_ROOT}/LIBRARY/Course Clips/1 - Bob-omb Battlefield/*.xml"
    "${XFL_ROOT}/LIBRARY/Course Clips/2 - Snowman's Land/*.xml"
    "${XFL_ROOT}/LIBRARY/Course Clips/8 - Storyline Stars/8 - Tutorials/*.xml")
add_custom_command(
    OUTPUT "${GENERATED_DIR}/art.hpp" "${GENERATED_DIR}/back.png" "${GENERATED_DIR}/front.png"
    BYPRODUCTS "${GENERATED_DIR}/art-report.json"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_art.py"
            "${XFL_ROOT}" "${GENERATED_DIR}"
    DEPENDS tools/import_art.py tools/import_world.py tools/import_level.py tools/import_mario.py
        "${XFL_ROOT}/DOMDocument.xml" "${XFL_ROOT}/LIBRARY/Course Items-PL/In-course Items/Transportation/Cannon/Cannon Platform.xml" ${AREA_ART_XML} ${EXPORTED_IMAGES}
    VERBATIM COMMENT "Baking original static area artwork")
add_custom_command(OUTPUT "${GENERATED_DIR}/world.hpp"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_world.py" "${XFL_ROOT}" "${GENERATED_DIR}/world.hpp"
    DEPENDS tools/import_world.py tools/import_level.py tools/import_mario.py ${AREA_ART_XML} VERBATIM)
add_custom_command(OUTPUT "${GENERATED_DIR}/hud_data.hpp"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_hud.py" "${XFL_ROOT}" "${GENERATED_DIR}/hud_data.hpp"
    DEPENDS tools/import_hud.py "${XFL_ROOT}/DOMDocument.xml" ${EXPORTED_IMAGES} VERBATIM)
file(GLOB_RECURSE SIGN_XML CONFIGURE_DEPENDS "${XFL_ROOT}/LIBRARY/*.xml")
add_custom_command(OUTPUT "${GENERATED_DIR}/signs_data.hpp"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_signs.py" "${XFL_ROOT}" "${GENERATED_DIR}/signs"
    DEPENDS tools/import_signs.py tools/import_art.py tools/import_world.py ${SIGN_XML} ${EXPORTED_IMAGES} VERBATIM)
add_custom_command(OUTPUT "${GENERATED_DIR}/enemy_art.hpp"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_enemy_art.py" "${XFL_ROOT}" "${GENERATED_DIR}/enemy-art"
    DEPENDS tools/import_enemy_art.py tools/import_signs.py tools/import_art.py ${SIGN_XML} ${EXPORTED_IMAGES} VERBATIM)
add_custom_target(enemy_art DEPENDS "${GENERATED_DIR}/enemy_art.hpp")
add_custom_command(OUTPUT "${GENERATED_DIR}/secret-background.png" "${GENERATED_DIR}/snow-background.png" "${GENERATED_DIR}/tutorial-background.png"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_backgrounds.py" "${XFL_ROOT}" "${GENERATED_DIR}/secret-background.png"
    DEPENDS tools/import_backgrounds.py tools/import_signs.py tools/import_art.py
        "${XFL_ROOT}/LIBRARY/Course Items-PL/Backgrounds/Secret Level BG.xml"
        "${XFL_ROOT}/LIBRARY/Course Clips/2 - Snowman's Land/2-1 BG.xml" VERBATIM)
add_custom_target(background_data DEPENDS "${GENERATED_DIR}/secret-background.png" "${GENERATED_DIR}/snow-background.png" "${GENERATED_DIR}/tutorial-background.png")
add_custom_target(sign_data DEPENDS "${GENERATED_DIR}/signs_data.hpp")
add_custom_target(hud_data DEPENDS "${GENERATED_DIR}/hud_data.hpp")
add_custom_target(world_data DEPENDS "${GENERATED_DIR}/world.hpp")
file(GLOB_RECURSE PLATFORM_XML CONFIGURE_DEPENDS "${XFL_ROOT}/LIBRARY/Course Items-PL/Various Platforms/*.xml")
add_custom_command(OUTPUT "${GENERATED_DIR}/platforms_data.hpp"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_platforms.py" "${XFL_ROOT}" "${GENERATED_DIR}/platforms_data.hpp"
    DEPENDS tools/import_platforms.py tools/import_world.py tools/import_mario.py ${AREA_ART_XML} ${PLATFORM_XML} VERBATIM)
add_custom_target(platform_data DEPENDS "${GENERATED_DIR}/platforms_data.hpp")
foreach(AREA_INDEX 1 2 3 4 5 6 7)
    add_custom_command(OUTPUT "${GENERATED_DIR}/area${AREA_INDEX}/back.png" "${GENERATED_DIR}/area${AREA_INDEX}/front.png"
        COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_art.py" "${XFL_ROOT}" "${GENERATED_DIR}/area${AREA_INDEX}" ${AREA_INDEX}
        DEPENDS tools/import_art.py tools/import_world.py tools/import_level.py tools/import_mario.py ${AREA_ART_XML} ${EXPORTED_IMAGES} VERBATIM)
    add_custom_target(area${AREA_INDEX}_art DEPENDS "${GENERATED_DIR}/area${AREA_INDEX}/back.png" "${GENERATED_DIR}/area${AREA_INDEX}/front.png")
endforeach()
find_program(FFMPEG_EXECUTABLE ffmpeg REQUIRED)
find_program(FONTCONFIG_EXECUTABLE fc-match REQUIRED)
file(GLOB AUDIO_SOURCE CONFIGURE_DEPENDS "${XFL_ROOT}/bin/*.dat")
set(AUDIO_WAVS)
foreach(CLIP tutorial_intro tutorial_loop snow_intro snow_loop wing_intro wing_loop boss_intro boss_loop boss_hurt boss_land cannon_enter cannon_rise cannon_ready cannon_shot star_swish get_star mario_fall star_appear secret_intro secret_loop intro loop jump double triple dive hurt coin box stomp die notice step impact nozzle switch spray nozzlebox fuse explosion)
    list(APPEND AUDIO_WAVS "${GENERATED_DIR}/audio/${CLIP}.wav")
endforeach()
add_custom_command(OUTPUT "${GENERATED_DIR}/audio/manifest.json" ${AUDIO_WAVS}
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_audio.py" "${XFL_ROOT}" "${GENERATED_DIR}/audio" "${FFMPEG_EXECUTABLE}"
    DEPENDS tools/import_audio.py "${XFL_ROOT}/DOMDocument.xml" ${AUDIO_SOURCE} VERBATIM)
add_custom_target(audio_data DEPENDS "${GENERATED_DIR}/audio/manifest.json" ${AUDIO_WAVS})
add_custom_command(OUTPUT "${GENERATED_DIR}/sequence_art.hpp"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_sequences.py" "${XFL_ROOT}" "${GENERATED_DIR}/sequence-art"
    DEPENDS tools/import_sequences.py tools/import_signs.py tools/import_mario.py ${SIGN_XML} ${EXPORTED_IMAGES} VERBATIM)
add_custom_target(sequence_data DEPENDS "${GENERATED_DIR}/sequence_art.hpp")
set(MARIO_DIR "${XFL_ROOT}/LIBRARY/Char/Mario CLIPS")
add_custom_command(
    OUTPUT "${GENERATED_DIR}/mario.hpp"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_mario.py"
            "${XFL_ROOT}" "${GENERATED_DIR}/mario.hpp"
    DEPENDS tools/import_mario.py "${XFL_ROOT}/DOMDocument.xml" ${SIGN_XML}
        "${MARIO_DIR}/Mario Slide (Hover).xml" "${MARIO_DIR}/Mario Rocket Slide.xml" "${MARIO_DIR}/Mario Slide (Turbo).xml"
        "${MARIO_DIR}/Mario (none) Spin Attack.xml" "${MARIO_DIR}/Mario Main.xml" "${MARIO_DIR}/Mario (None) Walk.xml"
        "${MARIO_DIR}/Mario (None) Dive.xml" "${MARIO_DIR}/Mario Diveflip.xml" "${MARIO_DIR}/Mario Backflip.xml"
        "${MARIO_DIR}/Mario Running.xml" "${MARIO_DIR}/Mario (None) Air - 1.xml"
        "${MARIO_DIR}/Mario (None) Air - 2.xml" "${MARIO_DIR}/Mario (none) Frontflip.xml"
        "${MARIO_DIR}/Mario Ground Pound.xml" "${MARIO_DIR}/Mario Ground Pound Blur.xml"
    VERBATIM)
file(GLOB GOOMBA_XML CONFIGURE_DEPENDS "${XFL_ROOT}/LIBRARY/Enemies/Normal/G-Z/Goomba/*.xml")
add_custom_command(OUTPUT "${GENERATED_DIR}/enemies_data.hpp"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_enemies.py" "${XFL_ROOT}" "${GENERATED_DIR}/enemies_data.hpp"
    DEPENDS tools/import_enemies.py tools/import_mario.py "${XFL_ROOT}/DOMDocument.xml"
        "${LEVEL_AREA}/1-1 Back Graphics.xml" ${GOOMBA_XML} VERBATIM)
add_custom_target(enemy_data DEPENDS "${GENERATED_DIR}/enemies_data.hpp")
file(GLOB_RECURSE ITEM_XML CONFIGURE_DEPENDS "${XFL_ROOT}/LIBRARY/Course Items-PL/In-course Items/*.xml")
add_custom_command(OUTPUT "${GENERATED_DIR}/items_data.hpp"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_items.py" "${XFL_ROOT}" "${GENERATED_DIR}/items_data.hpp"
    DEPENDS tools/import_items.py tools/import_mario.py "${XFL_ROOT}/DOMDocument.xml" ${ITEM_XML}
        "${LEVEL_AREA}/1-1 Back Graphics.xml" "${LEVEL_AREA}/1-1 Front Graphics.xml" VERBATIM)
add_custom_target(item_data DEPENDS "${GENERATED_DIR}/items_data.hpp")
add_library(sm63_physics STATIC native/src/physics.cpp native/src/enemies.cpp native/src/items.cpp)
add_dependencies(sm63_physics item_data world_data platform_data)
add_dependencies(sm63_physics enemy_data)
add_dependencies(sm63_physics level_data)
target_include_directories(sm63_physics PUBLIC native/src "${GENERATED_DIR}")
target_compile_features(sm63_physics PUBLIC cxx_std_17)
set_target_properties(sm63_physics PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
add_executable(sm63 native/src/main.cpp "${GENERATED_DIR}/assets.hpp" "${GENERATED_DIR}/mario.hpp" "${GENERATED_DIR}/art.hpp")
add_dependencies(sm63 background_data enemy_art sign_data hud_data level_data audio_data area6_art area7_art area5_art area1_art area2_art area3_art area4_art)
target_compile_features(sm63 PRIVATE cxx_std_17)
set_target_properties(sm63 PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(sm63 PRIVATE "${GENERATED_DIR}")
target_link_libraries(sm63 PRIVATE SDL3::SDL3 sm63_physics)
if(MSVC)
    target_compile_options(sm63 PRIVATE /W4 /permissive-)
else()
    target_compile_options(sm63 PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(physics_tests native/tests/physics_tests.cpp)
    target_link_libraries(physics_tests PRIVATE sm63_physics)
    add_dependencies(physics_tests level_data)
    target_compile_features(physics_tests PRIVATE cxx_std_17)
    target_include_directories(physics_tests PRIVATE native/src "${GENERATED_DIR}")
    add_test(NAME player_collision COMMAND physics_tests)
    add_executable(course_object_tests native/tests/course_object_tests.cpp)
    target_link_libraries(course_object_tests PRIVATE sm63_physics)
    add_test(NAME course_objects COMMAND course_object_tests)
    add_executable(sequence_tests native/tests/sequence_tests.cpp)
    target_link_libraries(sequence_tests PRIVATE sm63_physics)
    add_test(NAME collectible_sequences COMMAND sequence_tests)
    add_executable(sign_tests native/tests/sign_tests.cpp)
    target_link_libraries(sign_tests PRIVATE sm63_physics)
    add_test(NAME sign_interactions COMMAND sign_tests)
    add_executable(enemy_tests native/tests/enemy_tests.cpp)
    target_link_libraries(enemy_tests PRIVATE sm63_physics)
    add_test(NAME enemy_interactions COMMAND enemy_tests)
    add_executable(item_tests native/tests/item_tests.cpp)
    target_link_libraries(item_tests PRIVATE sm63_physics)
    add_test(NAME item_interactions COMMAND item_tests)
    add_executable(fludd_tests native/tests/fludd_tests.cpp)
    target_link_libraries(fludd_tests PRIVATE sm63_physics)
    add_test(NAME fludd_mechanics COMMAND fludd_tests)
    add_executable(platform_tests native/tests/platform_tests.cpp)
    target_link_libraries(platform_tests PRIVATE sm63_physics)
    add_test(NAME moving_platforms COMMAND platform_tests)
    add_executable(area_tests native/tests/area_tests.cpp)
    target_link_libraries(area_tests PRIVATE sm63_physics)
    add_test(NAME area_transitions COMMAND area_tests)
    add_executable(audio_tests native/tests/audio_tests.cpp)
    target_include_directories(audio_tests PRIVATE native/src)
    target_compile_features(audio_tests PRIVATE cxx_std_17)
    target_link_libraries(audio_tests PRIVATE SDL3::SDL3)
    add_dependencies(audio_tests audio_data)
    add_test(NAME audio_playback COMMAND audio_tests "${GENERATED_DIR}/audio")
    add_test(NAME level_importer COMMAND Python3::Interpreter -m unittest discover -s tools/tests)
    set_tests_properties(level_importer PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME native_sign_smoke COMMAND sm63 --area 1-1 --replay --read-sign 4 --frames 25)
    add_test(NAME opening_stage COMMAND sm63 --replay --frames 32)
    add_test(NAME opening_second_area COMMAND sm63 --area 8-E1-1-2 --replay --frames 32)
    add_test(NAME native_smoke COMMAND sm63 --area 1-1 --smoke-test)
    add_test(NAME course_exploration COMMAND sm63 --area 1-1 --smoke-test --explore)
    set_tests_properties(native_sign_smoke native_smoke course_exploration opening_stage opening_second_area PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software;SDL_AUDIODRIVER=dummy" TIMEOUT 30)
endif()

add_dependencies(sm63 sequence_data)

add_custom_command(OUTPUT "${GENERATED_DIR}/objects_data.hpp"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_objects.py" "${XFL_ROOT}" "${GENERATED_DIR}/object-art"
    DEPENDS tools/import_objects.py tools/import_signs.py tools/import_world.py ${SIGN_XML} ${EXPORTED_IMAGES} VERBATIM)
add_custom_target(object_data DEPENDS "${GENERATED_DIR}/objects_data.hpp")
add_dependencies(sm63_physics object_data)
