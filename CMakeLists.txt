cmake_minimum_required(VERSION 3.16)
project(sm63 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# On Android the app's build (android/app/jni/CMakeLists.txt) builds SDL2 from source
# first and includes this project, which then produces libmain.so instead of programs.
# SM63_SDL_SOURCE: build SDL2 from a source release and link it statically (Windows
# release builds); otherwise the system's SDL2 is used.
set(SM63_SDL_SOURCE "" CACHE PATH "SDL2 source directory to build and link statically")
if(ANDROID)
    set(SM63_SDL_LIBS SDL2::SDL2)
elseif(SM63_SDL_SOURCE)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Portable Linux release: bind every file (SDL's too) to old glibc symbol versions.
        set(SM63_GLIBC_COMPAT ON)
        enable_language(C)
        add_compile_options(-include ${CMAKE_CURRENT_SOURCE_DIR}/src/compat/glibc_compat.h)
    endif()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(${SM63_SDL_SOURCE} SDL2 EXCLUDE_FROM_ALL)
    set(SM63_SDL_LIBS SDL2::SDL2-static)
    if(TARGET SDL2::SDL2main)
        set(SM63_SDL_LIBS SDL2::SDL2main SDL2::SDL2-static)
    endif()
else()
    find_package(SDL2 REQUIRED)
    set(SM63_SDL_LIBS SDL2::SDL2)
    if(WIN32 AND TARGET SDL2::SDL2main)
        set(SM63_SDL_LIBS SDL2::SDL2main SDL2::SDL2)
    endif()
endif()
if(WIN32)
    # Windows XP and newer (SDL2 itself loads newer Windows functions only when present).
    add_compile_definitions(_WIN32_WINNT=0x0501 WINVER=0x0501)
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
    src/flash/TrueType.cpp
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

# Assets compiled in when present (see cmake/embed_assets.cmake); they belong to their
# makers, so keep them out of the repository (fonts/ and *.mp3 at the root are ignored):
# - fonts for text the SWF's embedded fonts can't fully draw (they often hold only the
#   letters of a field's initial text): Arial (regular, bold, italic) and Comic Sans MS;
# - the sound played when the pause menu opens and closes.
set(SM63_FONT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fonts" CACHE PATH "TrueType fonts to embed (optional)")
set(SM63_PAUSE_SOUND "${CMAKE_CURRENT_SOURCE_DIR}/SM64_Pause.oga.mp3" CACHE FILEPATH "Pause menu MP3 to embed (optional)")
set(SM63_ASSETS_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_assets.cpp")
file(GLOB SM63_ASSET_FILES CONFIGURE_DEPENDS "${SM63_FONT_DIR}/*.ttf" "${SM63_FONT_DIR}/*.TTF" "${SM63_PAUSE_SOUND}")
# Rewritten only when the set of files changes: files copied in keep old timestamps, so
# their own times can't tell the build that something was added or removed.
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/asset_list.txt" CONTENT "${SM63_ASSET_FILES}\n")
add_custom_command(OUTPUT "${SM63_ASSETS_CPP}"
    COMMAND ${CMAKE_COMMAND} -DFONT_DIR=${SM63_FONT_DIR} -DPAUSE_SOUND=${SM63_PAUSE_SOUND} -DOUT=${SM63_ASSETS_CPP}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_assets.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_assets.cmake ${SM63_ASSET_FILES}
            "${CMAKE_CURRENT_BINARY_DIR}/generated/asset_list.txt")
add_library(embedded_assets STATIC "${SM63_ASSETS_CPP}")
target_include_directories(embedded_assets PRIVATE src)
target_link_libraries(embedded_assets PRIVATE flash)

if(ANDROID)
    # SDL's Java activity loads libSDL2.so, then libmain.so, and calls its SDL_main.
    add_library(main SHARED src/main.cpp)
    target_link_libraries(main PRIVATE game embedded_assets ${SM63_SDL_LIBS})
    return()  # the desktop tools and tests below are not built for Android
endif()

add_executable(sm63 src/main.cpp)
target_link_libraries(sm63 PRIVATE game embedded_assets ${SM63_SDL_LIBS})
if(SM63_SDL_SOURCE AND NOT WIN32)
    # Portable Linux release: SDL2 is linked in, and so is the C++ runtime (testers'
    # distributions may have an older libstdc++); only glibc comes from the system.
    # libgcc stays shared: the static one needs glibc 2.35's _dl_find_object.
    target_link_options(sm63 PRIVATE -static-libstdc++)
endif()
if(SM63_GLIBC_COMPAT)
    target_sources(sm63 PRIVATE src/compat/glibc_compat.c)
    set_source_files_properties(src/compat/glibc_compat.c PROPERTIES COMPILE_OPTIONS "-std=c11")
endif()
if(MINGW)
    # One self-contained exe: no libstdc++/libgcc/winpthread DLLs, and no console window.
    # msvcrt-os: the system msvcrt.dll every Windows since XP has, not the Universal CRT
    # (api-ms-win-crt-*.dll, Vista and later) that current MinGW-w64 builds default to.
    target_link_options(sm63 PRIVATE -static -mwindows -mcrtdll=msvcrt-os)
endif()

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
