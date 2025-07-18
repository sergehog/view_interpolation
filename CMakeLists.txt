cmake_minimum_required(VERSION 3.16)

project(view_interpolation
    DESCRIPTION ""
    VERSION "1.0.0"
)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(GLEW REQUIRED)
find_package(glm REQUIRED)

add_executable(view_interpolation view_interpolation.cpp lodepng.cpp common/controls.cpp)

target_link_libraries(view_interpolation
    PRIVATE
    GLEW::GLEW
    glfw
    ${OPENGL_LIBRARY}
    )
