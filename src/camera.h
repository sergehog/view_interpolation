// Calibrated pinhole cameras and calibration file loading.
#pragma once

#include <array>
#include <map>
#include <string>

#include <glm/glm.hpp>

namespace vi {

/// Pinhole Camera with optional lens distortion.
/// World -> Camera: x_cam = R * X + t; pixel ~ K * x_cam (y axis down, pixel centres at integer coordinates).
struct Camera {
  std::string name;
  glm::mat3 intrinsics{1.f};          ///< intrinsics
  glm::mat3 rotation{1.f};            ///< world -> Camera rotation
  glm::vec3 translation{0.f};         ///< world -> Camera translation
  std::array<float, 5> distortion{};  ///< OpenCV order: k1, k2, p1, p2, k3 (normalized coordinates)
  int width = 0;                      ///< resolution the intrinsics refer to, 0 = unknown
  int height = 0;                     ///< (then they are assumed to match the processing resolution)

  bool hasDistortion() const;
  /// [rotation | translation]
  glm::mat4x3 extrinsics() const;
  /// [K*R | K*t ; 0 0 0 1] with K = intrinsics, R = rotation, t = translation: homogeneous world point -> (u*z, v*z, z,
  /// 1)
  glm::mat4 projection() const;
  /// Camera centre in world coordinates.
  glm::vec3 center() const;
  /// Optical axis in world coordinates.
  glm::vec3 forward() const;
  /// Copy whose intrinsics refer to an image of w x h pixels (pixel-centre aware).
  Camera scaledTo(int w, int h) const;
};

/// All cameras of a calibration file (parsed once, then cached). Supported formats:
///  * *.json: CMU Panoptic ({"cameras": [{"name", "resolution", "K", "distCoef", "R", "t"}, ...]})
///  * anything else: text blocks of <name>, 3x3 K (row-major), k1 k2, 3x4 [R|t] (row-major, world -> Camera)
const std::map<std::string, Camera>& loadCameras(const std::string& file);

/// One Camera of a calibration file; throws if it does not exist.
Camera findCamera(const std::string& file, const std::string& name);

}  // namespace vi
