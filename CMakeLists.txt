cmake_minimum_required(VERSION 3.16)

project(view_interpolation
    DESCRIPTION "Real-time plane-sweep view interpolation in OpenGL"
    VERSION "1.0.0"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(GLEW REQUIRED)
find_package(glm REQUIRED)
find_package(Boost REQUIRED CONFIG)   # header-only: PropertyTree (XML config, JSON calibration)
find_package(OpenMP REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswscale libavutil)

add_executable(view_interpolation
    view_interpolation.cpp
    lodepng.cpp
    common/controls.cpp
    common/frame_source.cpp
    )

target_link_libraries(view_interpolation
    PRIVATE
    GLEW::GLEW
    glfw
    ${OPENGL_LIBRARY}
    Boost::headers
    OpenMP::OpenMP_CXX
    Threads::Threads
    PkgConfig::FFMPEG
    )
