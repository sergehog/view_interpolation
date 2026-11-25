// GPU plane sweep: per-depth-layer matching costs (sweep pass) and per-pixel best layer (select pass).
#pragma once

#include <cstdint>
#include <vector>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "config.h"

namespace vi {

/// Renders the virtual view from two input images in two passes:
///  1. sweep:  for each of `layers` depth hypotheses of the virtual camera, project every pixel into both
///             inputs and store (mean colour, matching cost) in one slice of a texture array;
///  2. select: mipmaps of the array aggregate the costs; each output pixel takes the colour of the layer
///             with the lowest aggregated cost (winner-takes-all).
/// Requires a current OpenGL 3.3 core context with ARB_texture_storage (or OpenGL 4.2).
class PlaneSweepRenderer {
 public:
  /// Inputs have the processing resolution of the config; output_width/height is the framebuffer size.
  PlaneSweepRenderer(const AppConfig& config, int output_width, int output_height);
  ~PlaneSweepRenderer();

  PlaneSweepRenderer(const PlaneSweepRenderer&) = delete;
  PlaneSweepRenderer& operator=(const PlaneSweepRenderer&) = delete;

  /// Uploads one input image (BGR24 at the processing resolution, top row first) to slot 0 or 1.
  void uploadInput(int slot, const uint8_t* bgr);

  /// Renders into the default framebuffer. mvp_i maps (u*z, v*z, z, 1) of the virtual camera to input i.
  void render(const glm::mat4& mvp1, const glm::mat4& mvp2);

  /// Reads the default framebuffer's back buffer as RGB24, top row first.
  std::vector<uint8_t> readBackBuffer() const;

 private:
  void setCommonUniforms(GLuint program, const AppConfig& config) const;

  const int input_width_, input_height_;
  const int output_width_, output_height_;
  const unsigned layers_;

  GLuint vao_ = 0;
  GLuint sweep_program_ = 0, select_program_ = 0;
  GLint mvp1_location_ = -1, mvp2_location_ = -1, layer_location_ = -1;
  GLuint sweep_quad_ = 0, select_quad_ = 0;  // two triangles each, ivec2 pixel coordinates
  GLuint inputs_[2] = {0, 0};                // input images, full mip chain
  GLuint layer_array_ = 0;                   // RGBA8 texture array: rgb = mean colour, a = cost
  GLuint framebuffer_ = 0;
};

}  // namespace vi
