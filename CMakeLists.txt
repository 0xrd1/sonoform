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

# ---------------------------------------------------------------------------
# Dear ImGui + rlImGui -- the runtime parameter-editing UI (src/ui/).
#
# Neither repo ships a CMakeLists.txt: imgui is "drop the .cpp files into
# your build", and rlImGui builds with premake5. FetchContent_MakeAvailable
# normally calls add_subdirectory() on the populated source, which would
# hard-error on either. Pointing SOURCE_SUBDIR at a directory that does not
# exist makes MakeAvailable populate-only and skip add_subdirectory -- the
# supported modern replacement for the now-deprecated bare
# FetchContent_Populate().
#
# Version pinning is load-bearing, not hygiene: current rlImGui is written
# against Dear ImGui 1.92's dynamic-texture backend API (it sets
# ImGuiBackendFlags_RendererHasTextures and implements
# ImGui_ImplRaylib_UpdateTexture over ImTextureData). Pairing it with an
# older imgui does not compile. This pair is the combination upstream tags
# and tests together: rlImGui's "Raylib_5_5" release is published as
# "Raylib 5.5, ImGui 1.92.7". Do NOT float either to a moving branch --
# a future backend API change on either side would break a clean configure
# with no change on our side.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.7-docking
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  no-cmake-here
)

FetchContent_Declare(
    rlimgui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG        Raylib_5_5
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  no-cmake-here
)

FetchContent_MakeAvailable(imgui rlimgui)

# One target for both: rlImGui.cpp is the only translation unit that
# implements the ImGui_ImplRaylib_* backend declared in imgui_impl_raylib.h,
# so it has no independent existence. No backends/*.cpp from imgui is
# compiled -- rlImGui replaces both the platform and renderer backends,
# drawing through rlgl's existing immediate-mode batch (compiled in under
# GRAPHICS_API_OPENGL_43; see rlgl.h) rather than issuing GL directly. That
# is what keeps a second GL loader out of the build -- see src/core/GlCompat.h
# for why a second loader would be a problem here.
add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${rlimgui_SOURCE_DIR}/rlImGui.cpp"
)

# SYSTEM so imgui.h/rlImGui.h are treated as external headers in our own
# /W4 /permissive- translation unit (src/ui/EngineUi.cpp).
target_include_directories(imgui SYSTEM PUBLIC
    "${imgui_SOURCE_DIR}"
    "${rlimgui_SOURCE_DIR}"
)

target_link_libraries(imgui PUBLIC raylib)
target_compile_features(imgui PUBLIC cxx_std_17)

# Our /W4 /permissive- is applied to ParticleAudioEngine only (below), so
# this is belt-and-braces. It also produces no MSVC D9025 "overriding /W3"
# warning: cmake_minimum_required(3.20) puts CMP0092 in NEW mode, so CMake
# does not inject a default /W3 into CMAKE_CXX_FLAGS in the first place.
if(MSVC)
    target_compile_options(imgui PRIVATE /W0)
else()
    target_compile_options(imgui PRIVATE -w)
endif()

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
    src/ui
)

target_link_libraries(ParticleAudioEngine PRIVATE raylib imgui)

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
