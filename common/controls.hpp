// Include GLFW
#include <GLFW/glfw3.h>

#ifndef CONTROLS_HPP
#define CONTROLS_HPP

glm::mat4 computeViewMatrix(const float speed, const float mouseSpeed, GLFWwindow *window);
//glm::mat4 computeViewMatrix(const config::config config, GLFWwindow *window);
//glm::mat4 getViewMatrix();
//glm::mat4 getProjectionMatrix();
GLuint LoadShaders(const char * vertex_file_path, const char * fragment_file_path);
GLuint Load3Shaders(const char * vertex_file_path, const char * geometry_file_path, const char * fragment_file_path);

#endif