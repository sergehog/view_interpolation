cmake_minimum_required(VERSION 3.16)

project(view_interpolation
    DESCRIPTION "Real-time plane-sweep view interpolation in OpenGL"
    VERSION 2.0.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(GLEW REQUIRED)
find_package(glm REQUIRED)
find_package(Boost REQUIRED CONFIG)   # header-only: PropertyTree (XML config, JSON calibration)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswscale libavutil)

# third-party PNG encoder (snapshots, tools)
add_library(lodepng STATIC third_party/lodepng/lodepng.cpp)
target_include_directories(lodepng PUBLIC third_party/lodepng)

# input pipeline and calibration, shared by the program and the tools
add_library(vi_input STATIC
    src/camera.cpp
    src/config.cpp
    src/frame_source.cpp
    src/input_views.cpp
)
target_include_directories(vi_input PUBLIC src)
target_link_libraries(vi_input PUBLIC Boost::headers glm::glm Threads::Threads PkgConfig::FFMPEG)

add_executable(view_interpolation
    src/main.cpp
    src/fly_camera.cpp
    src/gl_program.cpp
    src/plane_sweep_renderer.cpp
)
target_link_libraries(view_interpolation PRIVATE vi_input lodepng GLEW::GLEW glfw OpenGL::GL)

# test helper (tools/eval.sh): undistorted ground-truth frame of a video as PNG
add_executable(gtframe tools/gtframe.cpp)
target_link_libraries(gtframe PRIVATE vi_input lodepng)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target vi_input view_interpolation gtframe)
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endforeach()
    target_compile_options(lodepng PRIVATE -w) # third-party code, not ours to fix
endif()
