cmake_minimum_required(VERSION 3.16)
project(sm63 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# On Android the app's build (android/app/jni/CMakeLists.txt) builds SDL2 from source
# first and includes this project, which then produces libmain.so instead of programs.
if(NOT ANDROID)
    find_package(SDL2 REQUIRED)
endif()
find_package(Threads REQUIRED)
if(ANDROID)
    # Optimize native code in every build type: the interpreter-heavy game code is far
    # too slow at -O0 to play, even in a debug APK.
    add_compile_options(-O2)
endif()

add_library(flash STATIC
    src/flash/Inflate.cpp
    src/flash/Jpeg.cpp
    src/flash/Morph.cpp
    src/flash/Text.cpp
    src/flash/Effects.cpp
    src/flash/WorkerPool.cpp
    src/flash/Swf.cpp
    src/flash/Shape.cpp
    src/flash/Raster.cpp
    src/flash/DisplayObject.cpp
)
target_include_directories(flash PUBLIC src)
target_link_libraries(flash PRIVATE Threads::Threads)  # render worker threads

# Audio decoding and mixing (no platform dependencies). minimp3 is vendored in third_party.
add_library(audio STATIC
    src/audio/Decoder.cpp
    src/audio/Mixer.cpp
)
target_include_directories(audio PUBLIC src PRIVATE third_party/minimp3)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(audio PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

add_library(as STATIC
    src/as/Value.cpp
    src/as/Object.cpp
    src/as/Runtime.cpp
    src/as/Builtins.cpp
    src/as/Diagnostics.cpp
)
target_include_directories(as PUBLIC src)

# Display-list bridge and frame loop (depends on both the renderer and the runtime).
add_library(stage STATIC
    src/as/Stage.cpp
    src/as/Player.cpp
    src/as/Audio.cpp
)
target_link_libraries(stage PUBLIC as flash audio)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(stage PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(as PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(flash PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Game scripts transpiled from the XFL source by tools/as2cpp (src/game/generated).
# Re-run cmake after regenerating them. (No CONFIGURE_DEPENDS: its per-build glob check
# made ninja drop its dependency log and recompile everything on every build.)
file(GLOB GAME_SCRIPTS ${CMAKE_CURRENT_SOURCE_DIR}/src/game/generated/*.cpp)
if(NOT GAME_SCRIPTS)
    message(STATUS "No generated scripts in src/game/generated: building without game scripts")
    set(GAME_SCRIPTS src/game/NoScripts.cpp)
endif()
add_library(game STATIC src/game/Prelude.cpp ${GAME_SCRIPTS})
target_link_libraries(game PUBLIC stage)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Generated code: no unused-value noise; keep compile memory reasonable.
    target_compile_options(game PRIVATE -Wno-unused-value -Wno-unused-variable)
endif()

if(ANDROID)
    # SDL's Java activity loads libSDL2.so, then libmain.so, and calls its SDL_main.
    add_library(main SHARED src/main.cpp)
    target_link_libraries(main PRIVATE game SDL2::SDL2)
    return()  # the desktop tools and tests below are not built for Android
endif()

add_executable(sm63 src/main.cpp)
target_link_libraries(sm63 PRIVATE game SDL2::SDL2)

add_executable(jpeg_check tests/jpeg_check.cpp)
target_link_libraries(jpeg_check PRIVATE flash)

add_executable(swf_check tests/swf_check.cpp)
target_link_libraries(swf_check PRIVATE flash)

add_executable(as_test tests/as_test.cpp)
target_link_libraries(as_test PRIVATE as)

add_executable(stage_test tests/stage_test.cpp)
target_link_libraries(stage_test PRIVATE stage)

add_executable(audio_test tests/audio_test.cpp)
target_link_libraries(audio_test PRIVATE stage)

add_executable(charinfo tests/charinfo.cpp)
target_link_libraries(charinfo PRIVATE flash)

add_executable(missing_bitmaps tests/missing_bitmaps.cpp)
target_link_libraries(missing_bitmaps PRIVATE flash)

add_executable(text_bounds tests/text_bounds.cpp)
target_link_libraries(text_bounds PRIVATE flash)

# Regenerates src/MissingSwfImage.h (the "SWF not found" screen) from the game's sign font.
add_executable(missing_swf_image tools/missing_swf_image.cpp)
target_link_libraries(missing_swf_image PRIVATE flash)
