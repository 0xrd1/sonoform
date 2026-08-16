cmake_minimum_required(VERSION 3.20)
project(ParticleAudioEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# GPU-driven particles require compute shaders + SSBOs, which raylib only
# compiles in under GRAPHICS_API_OPENGL_43 (see rlgl.h). Without this,
# raylib's compute/SSBO functions silently no-op. FORCE is required because
# a prior configure may have already cached OPENGL_VERSION=OFF.
set(OPENGL_VERSION "4.3" CACHE STRING "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(raylib)

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")

add_executable(ParticleAudioEngine ${ENGINE_SOURCES})

target_include_directories(ParticleAudioEngine PRIVATE
    src
    src/app
    src/core
    src/gfx
    src/audio
    src/particles
    src/shapes
    src/lightning
    src/visualizers
)

target_link_libraries(ParticleAudioEngine PRIVATE raylib)

if(APPLE)
    target_link_libraries(ParticleAudioEngine PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()

if(MSVC)
    target_compile_options(ParticleAudioEngine PRIVATE /W4 /permissive-)
    set_property(TARGET ParticleAudioEngine PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
else()
    target_compile_options(ParticleAudioEngine PRIVATE -Wall -Wextra)
endif()

# Keep assets next to the built executable so relative paths (assets/audio/...) resolve.
add_custom_command(TARGET ParticleAudioEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:ParticleAudioEngine>/assets"
)
