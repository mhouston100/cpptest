#pragma once

#include <raylib.h>

// START REMOVE-ALL STUDY NOTES
// Camera setup is a separate feature because it controls how the world is viewed,
// independent of the level data or rendering pipeline. This keeps the main loop
// much easier to read and allows the camera style to be adjusted without digging
// through unrelated systems.
// END REMOVE-ALL STUDY NOTES

namespace cpptest {

constexpr float kCamDistLevels[4] = {6.5f, 9.5f, 13.f, 18.f};

extern float g_camDist;
extern float g_camPitchDeg;
extern float g_camYawDeg;

void UpdateDiabloStyleCamera(Camera3D& cam, const Vector3& focus);

}  // namespace cpptest
