#include "config.h"

#include <stdexcept>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

namespace vi {

namespace {

using boost::property_tree::ptree;

template <typename T>
void required(const ptree& pt, const char* path, T& value) {
  const auto node = pt.get_optional<T>(path);
  if (!node) {
    throw std::runtime_error(std::string("Missing or invalid config value <") + path + ">");
  }
  value = *node;
}

template <typename T>
void optional(const ptree& pt, const char* path, T& value) {
  value = pt.get<T>(path, value);
}

}  // namespace

AppConfig loadConfig(const std::string& file) {
  ptree root;
  boost::property_tree::read_xml(file, root);
  const ptree& input = root.get_child("config.input");
  const ptree& settings = root.get_child("config.settings");

  AppConfig c;
  required(input, "width", c.width);
  required(input, "height", c.height);
  required(input, "minZ", c.min_z);
  required(input, "maxZ", c.max_z);
  required(input, "camera_file", c.camera_file);
  optional(input, "frames", c.frames);
  optional(input, "fps", c.fps);

  if (const auto views = input.get_child_optional("views")) {
    for (const auto& item : *views) {
      if (item.first == "view") {
        c.views.push_back({item.second.get<std::string>("file"), item.second.get<std::string>("camera")});
      }
    }
  } else  // legacy two-view form
  {
    for (const char* i : {"1", "2"}) {
      const auto color = input.get_optional<std::string>(std::string("color") + i);
      const auto camera = input.get_optional<std::string>(std::string("camera") + i);
      if (color && camera) {
        c.views.push_back({*color, *camera});
      }
    }
  }
  if (c.views.size() < 2) {
    throw std::runtime_error("At least two input views are required (<views> or color1/camera1 + color2/camera2)");
  }

  required(settings, "desired_camera", c.desired_camera);
  c.desired_camera_file = settings.get<std::string>("desired_camera_file", c.camera_file);
  required(settings, "layers", c.layers);
  c.output_width = settings.get<int>("output_width", c.width);
  c.output_height = settings.get<int>("output_height", c.height);
  optional(settings, "param1", c.param1);
  optional(settings, "param2", c.param2);
  optional(settings, "speed", c.speed);
  optional(settings, "mouseSpeed", c.mouse_speed);
  optional(settings, "max_view_angle", c.max_view_angle);
  optional(settings, "buffer_frames", c.buffer_frames);
  optional(settings, "vertex_internal", c.sweep_shaders.vertex);
  optional(settings, "geometry_internal", c.sweep_shaders.geometry);
  optional(settings, "fragment_internal", c.sweep_shaders.fragment);
  optional(settings, "vertex_shader", c.select_shaders.vertex);
  optional(settings, "geometry_shader", c.select_shaders.geometry);
  optional(settings, "fragment_shader", c.select_shaders.fragment);

  if (c.width <= 0 || c.height <= 0 || c.layers < 2 || c.min_z <= 0 || c.max_z < c.min_z) {
    throw std::runtime_error("Invalid config: need width, height > 0, layers >= 2 and 0 < minZ <= maxZ");
  }
  return c;
}

}  // namespace vi
