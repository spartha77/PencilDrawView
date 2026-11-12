# =============================================================================
# CadCore.GL - cross-platform build (Linux / macOS / Windows without Visual Studio)
#
# On Windows the Visual Studio solution uses CadCore.GL.vcxproj instead; this
# file builds the very same sources elsewhere.
#
#   Linux packages :  libglew-dev  libglfw3-dev (GLFW only for the example host)
#   Build          :  cmake -S . -B build && cmake --build build
#   Run example    :  ./build/glfw_viewer
# =============================================================================
cmake_minimum_required(VERSION 3.16)
project(CadCoreGL LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

add_library(CadCoreGL STATIC
    src/Renderer.cpp
    src/Scene.cpp
    src/SceneNodes.cpp
    src/ShaderProgram.cpp
    src/StlLoader.cpp
    src/Viewer.cpp)
target_include_directories(CadCoreGL PUBLIC include)
target_link_libraries(CadCoreGL PUBLIC GLEW::GLEW OpenGL::GL)

if(WIN32)
    # WGL context used by the WPF host (through the C++/CLI bridge).
    target_sources(CadCoreGL PRIVATE platform/WglContext.cpp)
    target_include_directories(CadCoreGL PUBLIC platform)
endif()

if(MSVC)
    target_compile_options(CadCoreGL PRIVATE /W4)
else()
    target_compile_options(CadCoreGL PRIVATE -Wall -Wextra)
endif()

# Optional stand-alone host (not needed for the WPF application).
find_package(glfw3 QUIET)
if(glfw3_FOUND)
    add_executable(glfw_viewer examples/glfw_viewer.cpp)
    target_link_libraries(glfw_viewer PRIVATE CadCoreGL glfw)
endif()
