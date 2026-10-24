cmake_minimum_required(VERSION 3.16)
project(sm63_android C CXX)

# SDL2 from its source release (SDL_SOURCE_DIR is passed by app/build.gradle), then the
# game itself, which builds libmain.so when ANDROID is set.
set(SDL_SOURCE_DIR "" CACHE PATH "SDL2 source directory")
if(NOT EXISTS "${SDL_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "SDL2 source not found at '${SDL_SOURCE_DIR}' (see android/README.md)")
endif()
add_subdirectory(${SDL_SOURCE_DIR} SDL2)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../.. sm63)
