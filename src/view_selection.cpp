#include "view_selection.h"

#include <cmath>
#include <limits>

namespace vi {

namespace {

// Small tolerance so that a virtual view exactly at a camera still counts as between two cameras.
constexpr float kBracketTolerance = 1e-4f;
// Weight of the angular baseline: among pairs around the virtual view, prefer cameras close to each other.
constexpr float kBaselineWeight = 0.25f;
// A previous pair is kept unless the best one scores this much better (avoids flickering).
constexpr float kHysteresis = 1.05f;

struct PairScore {
  bool brackets = false;  ///< the virtual view lies between the two cameras
  float cost = std::numeric_limits<float>::max();

  bool betterThan(const PairScore& other) const { return brackets != other.brackets ? brackets : cost < other.cost; }
};

/// Scores a pair by the viewing directions onto the scene: `ua`, `ub`, `uv` are unit vectors from the scene
/// point to camera a, camera b and the virtual camera.
PairScore scorePair(const glm::vec3& ua, const glm::vec3& ub, const glm::vec3& uv) {
  const glm::vec3 ab = ub - ua;
  const float length2 = glm::dot(ab, ab);
  PairScore score;
  if (length2 <= 1e-12f) {
    return score;  // cameras in the same direction: no parallax between them
  }
  // position of the virtual direction along the a-b chord: 0 at a, 1 at b
  const float s = glm::dot(uv - ua, ab) / length2;
  score.brackets = s >= -kBracketTolerance && s <= 1 + kBracketTolerance;
  const glm::vec3 closest = ua + glm::clamp(s, 0.f, 1.f) * ab;
  score.cost = glm::length(uv - closest) + kBaselineWeight * std::sqrt(length2);
  return score;
}

}  // namespace

std::pair<int, int> selectViewPair(const std::vector<Camera>& cameras, const glm::vec3& center,
                                   const glm::vec3& direction, float scene_depth, float max_angle_deg,
                                   std::pair<int, int> previous) {
  const int n = int(cameras.size());
  if (n == 2) {
    return {0, 1};
  }

  const float min_cos = std::cos(glm::radians(max_angle_deg));
  std::vector<int> candidates;
  for (int i = 0; i < n; i++) {
    if (glm::dot(cameras[i].forward(), direction) >= min_cos) {
      candidates.push_back(i);
    }
  }
  if (candidates.size() < 2) {  // nothing faces the right way: consider all cameras
    candidates.clear();
    for (int i = 0; i < n; i++) {
      candidates.push_back(i);
    }
  }

  // viewing directions onto the scene, seen from a point the virtual camera looks at
  const glm::vec3 scene_point = center + glm::normalize(direction) * scene_depth;
  const auto toward = [&scene_point](const glm::vec3& p) { return glm::normalize(p - scene_point); };
  std::vector<glm::vec3> u(n);
  for (int i : candidates) {
    u[i] = toward(cameras[i].center());
  }
  const glm::vec3 uv = toward(center);

  std::pair<int, int> best(candidates[0], candidates[1]);
  PairScore best_score;
  for (size_t i = 0; i < candidates.size(); i++) {
    for (size_t j = i + 1; j < candidates.size(); j++) {
      const PairScore score = scorePair(u[candidates[i]], u[candidates[j]], uv);
      if (score.betterThan(best_score)) {
        best_score = score;
        best = {candidates[i], candidates[j]};
      }
    }
  }

  if (previous.first >= 0 && previous.second < n && previous != best) {
    const PairScore kept =
        scorePair(toward(cameras[previous.first].center()), toward(cameras[previous.second].center()), uv);
    if (kept.brackets == best_score.brackets && kept.cost <= best_score.cost * kHysteresis) {
      return previous;
    }
  }
  return best;
}

}  // namespace vi
