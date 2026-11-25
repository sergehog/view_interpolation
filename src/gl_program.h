// GLSL program loading.
#pragma once

#include <string>

#include <GL/glew.h>

namespace vi {

/// Compiles and links a program from shader files (geometry may be empty). Compiler warnings are
/// printed, errors throw std::runtime_error with the compiler log.
GLuint loadProgram(const std::string& vertex, const std::string& geometry, const std::string& fragment);

}  // namespace vi
