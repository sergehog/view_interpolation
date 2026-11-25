#include "plane_sweep_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include <glm/gtc/type_ptr.hpp>

#include "gl_program.h"

namespace vi {

namespace {

GLsizei fullMipLevels(int w, int h) { return GLsizei(std::floor(std::log2(double(std::max(w, h))))) + 1; }

/// Two triangles covering [0, w] x [0, h], as integer vertex coordinates.
GLuint makeQuad(int w, int h) {
  const GLint vertices[] = {0, 0, w, 0, 0, h, w, 0, w, h, 0, h};
  GLuint buffer = 0;
  glGenBuffers(1, &buffer);
  glBindBuffer(GL_ARRAY_BUFFER, buffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  return buffer;
}

/// Binds a quad from makeQuad() as vertex attribute 0 (ivec2).
void bindQuad(GLuint buffer) {
  glBindBuffer(GL_ARRAY_BUFFER, buffer);
  glEnableVertexAttribArray(0);
  glVertexAttribIPointer(0, 2, GL_INT, 0, nullptr);
}

void setTextureFiltering(GLenum target) {
  glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

}  // namespace

PlaneSweepRenderer::PlaneSweepRenderer(const AppConfig& config, int out_w, int out_h)
    : input_width_(config.width),
      input_height_(config.height),
      output_width_(out_w),
      output_height_(out_h),
      layers_(config.layers) {
  if (!GLEW_VERSION_4_2 && !GLEW_ARB_texture_storage) {
    throw std::runtime_error("OpenGL 4.2 or ARB_texture_storage is required");
  }

  glGenVertexArrays(1, &vao_);
  glBindVertexArray(vao_);

  const ShaderFiles& s = config.sweep_shaders;
  sweep_program_ = loadProgram(s.vertex, s.geometry, s.fragment);
  const ShaderFiles& d = config.select_shaders;
  select_program_ = loadProgram(d.vertex, d.geometry, d.fragment);
  setCommonUniforms(sweep_program_, config);
  setCommonUniforms(select_program_, config);

  glUseProgram(sweep_program_);
  glUniform1i(glGetUniformLocation(sweep_program_, "Texture1"), 0);
  glUniform1i(glGetUniformLocation(sweep_program_, "Texture2"), 1);
  mvp1_location_ = glGetUniformLocation(sweep_program_, "MVP1");
  mvp2_location_ = glGetUniformLocation(sweep_program_, "MVP2");
  layer_location_ = glGetUniformLocation(sweep_program_, "layer");
  glUseProgram(select_program_);
  glUniform1i(glGetUniformLocation(select_program_, "Texture"), 0);

  sweep_quad_ = makeQuad(input_width_, input_height_);
  select_quad_ = makeQuad(output_width_, output_height_);

  glGenTextures(2, inputs_);
  for (GLuint texture : inputs_) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, fullMipLevels(input_width_, input_height_), GL_RGB8, input_width_, input_height_);
    setTextureFiltering(GL_TEXTURE_2D);
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);  // input rows are tightly packed BGR24

  // The sweep pass renders at output resolution; the select pass samples mip levels 1-3 of it.
  glGenTextures(1, &layer_array_);
  glBindTexture(GL_TEXTURE_2D_ARRAY, layer_array_);
  glTexStorage3D(GL_TEXTURE_2D_ARRAY, fullMipLevels(output_width_, output_height_), GL_RGBA8, output_width_,
                 output_height_, GLsizei(layers_));
  setTextureFiltering(GL_TEXTURE_2D_ARRAY);

  glGenFramebuffers(1, &framebuffer_);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, layer_array_, 0);  // layered: gl_Layer picks the slice
  const GLenum draw_buffer = GL_COLOR_ATTACHMENT0;
  glDrawBuffers(1, &draw_buffer);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    throw std::runtime_error("Layered framebuffer is incomplete");
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

PlaneSweepRenderer::~PlaneSweepRenderer() {
  glDeleteFramebuffers(1, &framebuffer_);
  glDeleteTextures(1, &layer_array_);
  glDeleteTextures(2, inputs_);
  glDeleteBuffers(1, &sweep_quad_);
  glDeleteBuffers(1, &select_quad_);
  glDeleteProgram(sweep_program_);
  glDeleteProgram(select_program_);
  glDeleteVertexArrays(1, &vao_);
}

void PlaneSweepRenderer::setCommonUniforms(GLuint program, const AppConfig& config) const {
  // Every shader may declare any of these; unused ones are simply not found.
  glUseProgram(program);
  glUniform1f(glGetUniformLocation(program, "param1"), config.param1);
  glUniform1f(glGetUniformLocation(program, "param2"), config.param2);
  glUniform1f(glGetUniformLocation(program, "minZ"), config.min_z);
  glUniform1f(glGetUniformLocation(program, "maxZ"), config.max_z);
  // depth hypotheses are uniform in inverse depth: 1/z = d * minNZ + maxNZ, d in [0, 1]
  glUniform1f(glGetUniformLocation(program, "minNZ"), float(1.0 / config.min_z - 1.0 / config.max_z));
  glUniform1f(glGetUniformLocation(program, "maxNZ"), float(1.0 / config.max_z));
  glUniform1ui(glGetUniformLocation(program, "width"), GLuint(input_width_));
  glUniform1ui(glGetUniformLocation(program, "height"), GLuint(input_height_));
  glUniform1ui(glGetUniformLocation(program, "screen_width"), GLuint(output_width_));
  glUniform1ui(glGetUniformLocation(program, "screen_height"), GLuint(output_height_));
  glUniform1ui(glGetUniformLocation(program, "layers"), layers_);
}

void PlaneSweepRenderer::uploadInput(int slot, const uint8_t* bgr) {
  glBindTexture(GL_TEXTURE_2D, inputs_[slot]);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, input_width_, input_height_, GL_RGB, GL_UNSIGNED_BYTE, bgr);
  glGenerateMipmap(GL_TEXTURE_2D);
}

void PlaneSweepRenderer::render(const glm::mat4& mvp1, const glm::mat4& mvp2) {
  glViewport(0, 0, output_width_, output_height_);

  // 1. sweep pass: one quad per depth layer into the texture array
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(sweep_program_);
  glUniformMatrix4fv(mvp1_location_, 1, GL_FALSE, glm::value_ptr(mvp1));
  glUniformMatrix4fv(mvp2_location_, 1, GL_FALSE, glm::value_ptr(mvp2));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, inputs_[0]);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, inputs_[1]);
  bindQuad(sweep_quad_);
  for (GLuint layer = 0; layer < layers_; layer++)  // the geometry shader routes each quad to slice `layer`
  {
    glUniform1ui(layer_location_, layer);
    glDrawArrays(GL_TRIANGLES, 0, 6);
  }

  // 2. select pass: aggregate costs via mipmaps, pick the best layer per pixel
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glClearColor(0, 0, 0.2f, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(select_program_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, layer_array_);
  glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
  bindQuad(select_quad_);
  glDrawArrays(GL_TRIANGLES, 0, 6);
}

std::vector<uint8_t> PlaneSweepRenderer::readBackBuffer() const {
  const size_t row = size_t(output_width_) * 3;
  std::vector<uint8_t> pixels(row * output_height_), flipped(pixels.size());
  glReadBuffer(GL_BACK);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, output_width_, output_height_, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  for (int y = 0; y < output_height_; y++)  // OpenGL rows are bottom-up
  {
    std::memcpy(&flipped[y * row], &pixels[(output_height_ - 1 - y) * row], row);
  }
  return flipped;
}

}  // namespace vi
