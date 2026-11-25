// Application configuration (XML file).
#pragma once

#include <string>
#include <vector>

namespace vi {

/// One input view: an image, video or raw YUV file and the name of its camera in the calibration file.
struct ViewSource {
  std::string file;
  std::string camera;
};

/// Shader files of one render pass; an empty geometry path means no geometry shader.
struct ShaderFiles {
  std::string vertex;
  std::string geometry;
  std::string fragment;
};

struct AppConfig {
  // <input>
  int width = 0;  ///< processing resolution: inputs are scaled to it, intrinsics rescaled accordingly
  int height = 0;
  float min_z = 0;  ///< depth range of the plane sweep, in calibration units
  float max_z = 0;
  std::string camera_file;
  std::vector<ViewSource> views;
  unsigned frames = 0;  ///< loop length limit, 0 = shortest video
  double fps = 0;       ///< playback rate, 0 = from the video (else 25)

  // <settings>
  std::string desired_camera_file;  ///< defaults to camera_file
  std::string desired_camera;       ///< initial pose of the virtual camera
  unsigned layers = 0;              ///< number of depth hypotheses
  int output_width = 0;             ///< window size, defaults to the processing resolution
  int output_height = 0;
  float param1 = 0;  ///< free shader parameters (uniforms param1/param2); param1 = cost threshold of sweep.frag
  float param2 = 0;
  float speed = 1;              ///< free-fly motion, world units per second
  float mouse_speed = 0.0001f;  ///< radians per pixel of mouse drag
  float max_view_angle = 60;    ///< input views facing further away from the virtual camera are not used
  unsigned buffer_frames = 4;   ///< decoded frames queued per input
  ShaderFiles sweep_shaders{"shaders/sweep.vert", "shaders/sweep.geom", "shaders/sweep.frag"};
  ShaderFiles select_shaders{"shaders/select.vert", "", "shaders/select.frag"};
};

/// Loads and validates a config file; throws std::runtime_error with a readable message on errors.
AppConfig loadConfig(const std::string& file);

}  // namespace vi
