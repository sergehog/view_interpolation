// Unit tests for selectViewPair(): the virtual view must lie between the two chosen input cameras.

#include "view_selection.h"

#include <cmath>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <gtest/gtest.h>

namespace vi {
namespace {

constexpr float kMaxViewAngle = 60.f;
constexpr float kFar = 10.f;  // scene depth for the synthetic rigs whose cameras look along +z
// scene depths used by the app: the middle of the configs' depth range in inverse depth
constexpr float kGeorgievDepth = 2.f / (1.f / 1.f + 1.f / 7.f);      // configs/georgiev.config
constexpr float kPanopticDepth = 2.f / (1.f / 180.f + 1.f / 700.f);  // configs/panoptic_pose1*.config
const std::pair<int, int> kNoPrevious(-1, -1);

/// Camera at `center` looking at `target`; world y points down (as in the datasets), image y down.
Camera lookingAt(const glm::vec3& center, const glm::vec3& target, const std::string& name = "") {
  const glm::vec3 z = glm::normalize(target - center);
  const glm::vec3 x = glm::normalize(glm::cross(z, glm::vec3(0, -1, 0)));
  const glm::vec3 y = glm::cross(z, x);
  Camera cam;
  cam.name = name;
  cam.rotation = glm::transpose(glm::mat3(x, y, z));  // rows are the camera axes
  cam.translation = -(cam.rotation * center);
  return cam;
}

/// Whether the virtual camera views `scene_point` from a direction between the directions of the pair:
/// the virtual direction projects onto the chord between the two camera directions (unit vectors).
::testing::AssertionResult brackets(const std::vector<Camera>& cameras, std::pair<int, int> pair,
                                    const glm::vec3& virtual_center, const glm::vec3& scene_point) {
  const auto toward = [&](const glm::vec3& p) { return glm::normalize(p - scene_point); };
  const glm::vec3 ua = toward(cameras[pair.first].center()), ub = toward(cameras[pair.second].center());
  const float s = glm::dot(toward(virtual_center) - ua, ub - ua) / glm::dot(ub - ua, ub - ua);
  if (s >= -1e-4f && s <= 1 + 1e-4f) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure() << "virtual view is not between cameras " << cameras[pair.first].name << "#"
                                       << pair.first << " and " << cameras[pair.second].name << "#" << pair.second
                                       << " (position between their directions: " << s << ")";
}

std::pair<int, int> sorted(int a, int b) { return a < b ? std::make_pair(a, b) : std::make_pair(b, a); }

// --- synthetic rigs ---------------------------------------------------------------------------------------

/// Five cameras on the x axis (x = -2 ... 2), all looking along +z.
std::vector<Camera> linearRig() {
  std::vector<Camera> cams;
  for (int i = 0; i < 5; i++) {
    const glm::vec3 c(float(i - 2), 0, 0);
    cams.push_back(lookingAt(c, c + glm::vec3(0, 0, 10)));
  }
  return cams;
}

/// `n` cameras on a horizontal ring of radius `r` around the origin, all looking at the origin.
glm::vec3 onRing(float angle_deg, float r) {
  const float a = glm::radians(angle_deg);
  return {r * std::cos(a), 0, r * std::sin(a)};
}
std::vector<Camera> ringRig(int n, float r) {
  std::vector<Camera> cams;
  for (int i = 0; i < n; i++) {
    cams.push_back(lookingAt(onRing(360.f * i / n, r), glm::vec3(0)));
  }
  return cams;
}

TEST(SelectViewPair, TwoCamerasAreAlwaysUsed) {
  const std::vector<Camera> cams = {linearRig()[0], linearRig()[4]};
  EXPECT_EQ(selectViewPair(cams, glm::vec3(7, 0, 0), glm::vec3(0, 0, 1), kFar, kMaxViewAngle, kNoPrevious),
            std::make_pair(0, 1));
}

TEST(SelectViewPair, LinearRigPicksTheNeighboursOfTheVirtualView) {
  const std::vector<Camera> cams = linearRig();
  for (float x : {-1.7f, -0.5f, 0.2f, 0.9f, 1.4f}) {
    const glm::vec3 center(x, 0, 0);
    const int left = int(std::floor(x)) + 2;
    EXPECT_EQ(selectViewPair(cams, center, glm::vec3(0, 0, 1), kFar, kMaxViewAngle, kNoPrevious),
              std::make_pair(left, left + 1))
        << "virtual view at x = " << x;
  }
}

TEST(SelectViewPair, DoesNotExtrapolateFromTwoCamerasOnOneSide) {
  // two cameras very close together right of the virtual view, two further away on both sides
  std::vector<Camera> cams;
  for (float x : {-1.f, 0.1f, 0.2f, 1.f}) {
    cams.push_back(lookingAt(glm::vec3(x, 0, 0), glm::vec3(x, 0, 10)));
  }
  const glm::vec3 center(0, 0, 0);
  const std::pair<int, int> pair = selectViewPair(cams, center, glm::vec3(0, 0, 1), kFar, kMaxViewAngle, kNoPrevious);
  EXPECT_TRUE(brackets(cams, pair, center, center + glm::vec3(0, 0, kFar)));
  EXPECT_EQ(pair, std::make_pair(0, 1));  // the shortest pair around the virtual view
}

TEST(SelectViewPair, VirtualViewNextToACameraUsesThatCameraAndTheOneBeyondIt) {
  const std::vector<Camera> cams = linearRig();
  // just right of camera 2 (x = 0): (2, 3) brackets it, (1, 2) would be an extrapolation
  const glm::vec3 center(0.02f, 0, 0);
  EXPECT_EQ(selectViewPair(cams, center, glm::vec3(0, 0, 1), kFar, kMaxViewAngle, kNoPrevious), std::make_pair(2, 3));
}

TEST(SelectViewPair, RingPicksTheTwoCamerasAroundTheVirtualAngle) {
  const int n = 16;  // 22.5 degrees apart, like a sparse capture dome
  const std::vector<Camera> cams = ringRig(n, 3.f);
  for (float angle : {1.f, 5.f, 11.25f, 20.f, 93.f, 181.f, 358.f}) {
    const glm::vec3 center = onRing(angle, 3.f);
    const int k = int(angle / (360.f / n));
    EXPECT_EQ(selectViewPair(cams, center, glm::normalize(-center), 3.f, kMaxViewAngle, kNoPrevious),
              sorted(k, (k + 1) % n))
        << "virtual view at " << angle << " degrees";
  }
}

TEST(SelectViewPair, RandomVirtualViewsInsideARingAreAlwaysBracketed) {
  const int n = 16;
  const std::vector<Camera> cams = ringRig(n, 3.f);
  std::mt19937 rng(42);
  std::uniform_real_distribution<float> angle_dist(0.f, 360.f), radius_dist(2.f, 3.5f), height_dist(-0.5f, 0.5f);
  for (int trial = 0; trial < 500; trial++) {
    const float angle = angle_dist(rng);
    glm::vec3 center = onRing(angle, radius_dist(rng));
    center.y = height_dist(rng);
    const std::pair<int, int> pair =
        selectViewPair(cams, center, glm::normalize(-center), glm::length(center), kMaxViewAngle, kNoPrevious);
    // the chosen cameras must be the neighbours on both sides of the virtual view's angle
    const int k = int(angle / (360.f / n));
    EXPECT_EQ(pair, sorted(k, (k + 1) % n)) << "virtual view at " << angle << " degrees";
  }
}

TEST(SelectViewPair, IgnoresCamerasFacingAway) {
  // a close pair around the virtual view, but looking backwards, and a wider pair looking forwards
  std::vector<Camera> cams;
  for (float x : {-0.1f, 0.1f}) {
    cams.push_back(lookingAt(glm::vec3(x, 0, 0), glm::vec3(x, 0, -10)));
  }
  for (float x : {-1.f, 1.f}) {
    cams.push_back(lookingAt(glm::vec3(x, 0, 0), glm::vec3(x, 0, 10)));
  }
  EXPECT_EQ(selectViewPair(cams, glm::vec3(0), glm::vec3(0, 0, 1), kFar, kMaxViewAngle, kNoPrevious),
            std::make_pair(2, 3));
}

TEST(SelectViewPair, KeepsThePreviousPairWhileItIsStillAsGood) {
  const std::vector<Camera> cams = linearRig();
  const glm::vec3 at_camera_2(0, 0, 0);  // exactly at camera 2: (1, 2) and (2, 3) are equally good
  for (const std::pair<int, int> previous : {std::make_pair(1, 2), std::make_pair(2, 3)}) {
    EXPECT_EQ(selectViewPair(cams, at_camera_2, glm::vec3(0, 0, 1), kFar, kMaxViewAngle, previous), previous);
  }
  // but a previous pair that no longer contains the virtual view is dropped
  EXPECT_EQ(selectViewPair(cams, glm::vec3(0.5f, 0, 0), glm::vec3(0, 0, 1), kFar, kMaxViewAngle, std::make_pair(1, 2)),
            std::make_pair(2, 3));
}

// --- real calibrations ------------------------------------------------------------------------------------

std::vector<Camera> load(const std::string& file, const std::vector<std::string>& names) {
  std::vector<Camera> cams;
  for (const std::string& name : names) {
    cams.push_back(findCamera(file, name));
  }
  return cams;
}

TEST(SelectViewPair, GeorgievRow) {
  const std::string file = std::string(VI_SOURCE_DIR) + "/Georgiev/georgiev.txt";
  const std::vector<Camera> cams = load(file, {"Camera20", "Camera21", "Camera23", "Camera24"});
  const Camera virtual_cam = findCamera(file, "Camera22");  // held out, between 21 and 23
  const glm::vec3 dir = virtual_cam.forward();
  EXPECT_EQ(selectViewPair(cams, virtual_cam.center(), dir, kGeorgievDepth, kMaxViewAngle, kNoPrevious),
            std::make_pair(1, 2));
  // camera centres are at x = -0.08, -0.04, (0), 0.04, 0.08
  EXPECT_EQ(selectViewPair(cams, glm::vec3(-0.05f, 0, 0), dir, kGeorgievDepth, kMaxViewAngle, kNoPrevious),
            std::make_pair(0, 1));
  EXPECT_EQ(selectViewPair(cams, glm::vec3(0.07f, 0, 0), dir, kGeorgievDepth, kMaxViewAngle, kNoPrevious),
            std::make_pair(2, 3));
}

const std::string kPanoptic =
    std::string(VI_SOURCE_DIR) + "/data/171204_pose1_sample/calibration_171204_pose1_sample.json";

TEST(SelectViewPair, PanopticEveryHeldOutHdCameraIsBracketed) {
  if (!std::ifstream(kPanoptic)) {
    GTEST_SKIP() << "Panoptic sample not linked in data/ (see demo_video1.sh)";
  }
  std::vector<std::string> hd;
  for (const auto& [name, cam] : loadCameras(kPanoptic)) {
    if (name.rfind("00_", 0) == 0) {
      hd.push_back(name);
    }
  }
  ASSERT_EQ(hd.size(), 31u);
  for (const std::string& held_out : hd) {
    std::vector<std::string> others;
    for (const std::string& name : hd) {
      if (name != held_out) {
        others.push_back(name);
      }
    }
    const std::vector<Camera> cams = load(kPanoptic, others);
    const Camera v = findCamera(kPanoptic, held_out);
    const std::pair<int, int> pair =
        selectViewPair(cams, v.center(), v.forward(), kPanopticDepth, kMaxViewAngle, kNoPrevious);
    EXPECT_TRUE(brackets(cams, pair, v.center(), v.center() + v.forward() * kPanopticDepth))
        << held_out << " from " << cams[pair.first].name << " + " << cams[pair.second].name;
  }
}

TEST(SelectViewPair, PanopticDemoConfigUsesTheBracketingPair) {
  if (!std::ifstream(kPanoptic)) {
    GTEST_SKIP() << "Panoptic sample not linked in data/ (see demo_video1.sh)";
  }
  // the 8 inputs of configs/panoptic_pose1.config around the held-out camera 00_22
  const std::vector<Camera> cams =
      load(kPanoptic, {"00_02", "00_03", "00_05", "00_07", "00_09", "00_12", "00_15", "00_18"});
  const Camera v = findCamera(kPanoptic, "00_22");
  const std::pair<int, int> pair =
      selectViewPair(cams, v.center(), v.forward(), kPanopticDepth, kMaxViewAngle, kNoPrevious);
  EXPECT_TRUE(brackets(cams, pair, v.center(), v.center() + v.forward() * kPanopticDepth));
}

}  // namespace
}  // namespace vi
