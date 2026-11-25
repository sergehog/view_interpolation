#include "gl_program.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace vi {

namespace {

std::string readFile(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("Cannot open shader file " + path);
  }
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

GLuint compileShader(GLenum type, const std::string& path) {
  const std::string source = readFile(path);
  const char* text = source.c_str();
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &text, nullptr);
  glCompileShader(shader);

  GLint ok = GL_FALSE, log_length = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
  std::string log;
  if (log_length > 1) {
    std::vector<char> buffer(log_length);
    glGetShaderInfoLog(shader, log_length, nullptr, buffer.data());
    log = buffer.data();
  }
  if (!ok) {
    glDeleteShader(shader);
    throw std::runtime_error("Cannot compile " + path + ":\n" + log);
  }
  if (!log.empty()) {
    printf("%s: %s\n", path.c_str(), log.c_str());
  }
  return shader;
}

}  // namespace

GLuint loadProgram(const std::string& vertex, const std::string& geometry, const std::string& fragment) {
  std::vector<GLuint> shaders{compileShader(GL_VERTEX_SHADER, vertex)};
  if (!geometry.empty()) {
    shaders.push_back(compileShader(GL_GEOMETRY_SHADER, geometry));
  }
  shaders.push_back(compileShader(GL_FRAGMENT_SHADER, fragment));

  const GLuint program = glCreateProgram();
  for (GLuint s : shaders) {
    glAttachShader(program, s);
  }
  glLinkProgram(program);
  for (GLuint s : shaders) {
    glDetachShader(program, s);
    glDeleteShader(s);
  }

  GLint ok = GL_FALSE, log_length = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
  if (!ok) {
    std::vector<char> log(std::max(log_length, 1));
    glGetProgramInfoLog(program, GLsizei(log.size()), nullptr, log.data());
    glDeleteProgram(program);
    throw std::runtime_error("Cannot link " + vertex + " + " + fragment + ":\n" + log.data());
  }
  return program;
}

}  // namespace vi
