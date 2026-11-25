#include "camera.h"

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <vector>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <glm/gtc/matrix_access.hpp>

namespace vi {

bool Camera::hasDistortion() const {
  for (float d : distortion) {
    if (d != 0.f) {
      return true;
    }
  }
  return false;
}

glm::mat4x3 Camera::extrinsics() const { return glm::mat4x3(rotation[0], rotation[1], rotation[2], translation); }

glm::mat4 Camera::projection() const { return glm::mat4(intrinsics * extrinsics()); }

glm::vec3 Camera::center() const { return -(glm::transpose(rotation) * translation); }

glm::vec3 Camera::forward() const { return glm::row(rotation, 2); }

Camera Camera::scaledTo(int w, int h) const {
  Camera c = *this;
  c.width = w;
  c.height = h;
  if (width <= 0 || height <= 0 || (width == w && height == h)) {
    return c;
  }
  const float sx = float(w) / width;
  const float sy = float(h) / height;
  c.intrinsics[0][0] *= sx;                                    // fx
  c.intrinsics[1][0] *= sx;                                    // skew
  c.intrinsics[1][1] *= sy;                                    // fy
  c.intrinsics[2][0] = (intrinsics[2][0] + 0.5f) * sx - 0.5f;  // cx
  c.intrinsics[2][1] = (intrinsics[2][1] + 0.5f) * sy - 0.5f;  // cy
  return c;
}

namespace {

/// GLM matrices are column-major: build one from 9 row-major values.
glm::mat3 mat3FromRows(const float* v) { return glm::mat3(v[0], v[3], v[6], v[1], v[4], v[7], v[2], v[5], v[8]); }

bool endsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool isNumber(const std::string& s) {
  char* end = nullptr;
  std::strtod(s.c_str(), &end);
  return end != s.c_str() && *end == '\0';
}

std::map<std::string, Camera> parseText(const std::string& file) {
  std::ifstream in(file);
  if (!in) {
    throw std::runtime_error("Cannot open camera file " + file);
  }

  std::map<std::string, Camera> cameras;
  std::string name;
  while (in >> name) {
    if (isNumber(name)) {
      throw std::runtime_error("Unexpected number \"" + name + "\" in camera file " + file);
    }
    float v[23];  // K (9), k1 k2 (2), [R|t] (12)
    for (float& x : v) {
      if (!(in >> x)) {
        throw std::runtime_error("Incomplete parameters of camera \"" + name + "\" in " + file);
      }
    }
    Camera c;
    c.name = name;
    c.intrinsics = mat3FromRows(v);
    c.distortion[0] = v[9];
    c.distortion[1] = v[10];
    const float* e = v + 11;
    c.rotation = glm::mat3(e[0], e[4], e[8], e[1], e[5], e[9], e[2], e[6], e[10]);
    c.translation = glm::vec3(e[3], e[7], e[11]);
    cameras[name] = c;
  }
  return cameras;
}

/// JSON arrays are ptree children with empty keys; nested arrays are flattened row by row.
std::vector<float> flatNumbers(const boost::property_tree::ptree& node) {
  if (node.empty()) {
    return {node.get_value<float>()};
  }
  std::vector<float> out;
  for (const auto& child : node) {
    const std::vector<float> sub = flatNumbers(child.second);
    out.insert(out.end(), sub.begin(), sub.end());
  }
  return out;
}

std::map<std::string, Camera> parseJson(const std::string& file) {
  boost::property_tree::ptree pt;
  boost::property_tree::read_json(file, pt);

  std::map<std::string, Camera> cameras;
  for (const auto& item : pt.get_child("cameras")) {
    const boost::property_tree::ptree& node = item.second;
    Camera c;
    c.name = node.get<std::string>("name");

    const std::vector<float> k = flatNumbers(node.get_child("K"));
    const std::vector<float> r = flatNumbers(node.get_child("R"));
    const std::vector<float> t = flatNumbers(node.get_child("t"));
    if (k.size() != 9 || r.size() != 9 || t.size() != 3) {
      throw std::runtime_error("Malformed calibration of camera \"" + c.name + "\" in " + file);
    }
    c.intrinsics = mat3FromRows(k.data());
    c.rotation = mat3FromRows(r.data());
    c.translation = glm::vec3(t[0], t[1], t[2]);

    if (const auto dist = node.get_child_optional("distCoef")) {
      const std::vector<float> d = flatNumbers(*dist);
      for (size_t i = 0; i < d.size() && i < c.distortion.size(); i++) {
        c.distortion[i] = d[i];
      }
    }
    if (const auto resolution = node.get_child_optional("resolution")) {
      const std::vector<float> wh = flatNumbers(*resolution);
      if (wh.size() == 2) {
        c.width = int(wh[0]);
        c.height = int(wh[1]);
      }
    }
    cameras[c.name] = c;
  }
  return cameras;
}

}  // namespace

const std::map<std::string, Camera>& loadCameras(const std::string& file) {
  static std::map<std::string, std::map<std::string, Camera>> cache;
  auto it = cache.find(file);
  if (it == cache.end()) {
    it = cache.emplace(file, endsWith(file, ".json") ? parseJson(file) : parseText(file)).first;
  }
  return it->second;
}

Camera findCamera(const std::string& file, const std::string& name) {
  const auto& cameras = loadCameras(file);
  const auto it = cameras.find(name);
  if (it == cameras.end()) {
    throw std::runtime_error("Camera \"" + name + "\" not found in " + file);
  }
  return it->second;
}

}  // namespace vi
