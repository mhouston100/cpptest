#include "display.hpp"

#include <algorithm>
#include <cmath>

namespace cpptest {

float UniformScale(const int screen_w, const int screen_h, const float ref_w,
                   const float ref_h) {
  const float sx = static_cast<float>(screen_w) / ref_w;
  const float sy = static_cast<float>(screen_h) / ref_h;
  return std::clamp(std::min(sx, sy), kUiScaleMin, kUiScaleMax);
}

Letterbox ComputeLetterbox(const int screen_w, const int screen_h, const float content_w,
                           const float content_h) {
  const float scale = UniformScale(screen_w, screen_h, content_w, content_h);
  const float w = content_w * scale;
  const float h = content_h * scale;
  const float x = (static_cast<float>(screen_w) - w) * 0.5f;
  const float y = (static_cast<float>(screen_h) - h) * 0.5f;
  return Letterbox{Rectangle{x, y, w, h}, scale};
}

int LogicalToScreen(const float logical_px, const float scale) {
  return static_cast<int>(std::lround(logical_px * scale));
}

float UiScale() {
  return UniformScale(GetScreenWidth(), GetScreenHeight(), static_cast<float>(kUiLogicalW),
                      static_cast<float>(kUiLogicalH));
}

int UiPx(const float logical_px) {
  return LogicalToScreen(logical_px, UiScale());
}

Letterbox UiLetterbox() {
  return ComputeLetterbox(GetScreenWidth(), GetScreenHeight(), static_cast<float>(kUiLogicalW),
                          static_cast<float>(kUiLogicalH));
}

}  // namespace cpptest
