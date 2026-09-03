#include "camera.hpp"

#include <cmath>

#include <raymath.h>

// START REMOVE-ALL STUDY NOTES
// Camera update is intentionally isolated so the surrounding game loop can treat the
// scene view as a single feature. This keeps the 3D camera logic independent from
// the level logic and makes it easier to adjust look/zoom behavior later.
// END REMOVE-ALL STUDY NOTES

namespace cpptest {

float g_camDist = kCamDistLevels[1];
float g_camPitchDeg = 50.f;
float g_camYawDeg = 45.f;

void UpdateDiabloStyleCamera(Camera3D& cam, const Vector3& focus) {
  const float pitch = g_camPitchDeg * DEG2RAD;
  const float yaw = g_camYawDeg * DEG2RAD;
  const float cp = std::cos(pitch);
  const float sp = std::sin(pitch);
  const float cy = std::cos(yaw);
  const float sy = std::sin(yaw);
  const Vector3 offset{g_camDist * cp * sy, g_camDist * sp, g_camDist * cp * cy};
  cam.position = Vector3Add(focus, offset);
  cam.target = focus;
  cam.up = {0.f, 1.f, 0.f};
}

}  // namespace cpptest
