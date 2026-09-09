#pragma once

#include <raylib.h>

namespace cpptest {

constexpr int kUiLogicalW = 1920;
constexpr int kUiLogicalH = 1080;
constexpr int kDefaultWindowW = 1920;
constexpr int kDefaultWindowH = 1080;
constexpr float kUiScaleMin = 0.25f;
constexpr float kUiScaleMax = 3.f;

struct Letterbox {
  Rectangle dest{};
  float scale = 1.f;
};

[[nodiscard]] float UniformScale(int screen_w, int screen_h, float ref_w, float ref_h);
[[nodiscard]] Letterbox ComputeLetterbox(int screen_w, int screen_h, float content_w,
                                        float content_h);
[[nodiscard]] int LogicalToScreen(float logical_px, float scale);

[[nodiscard]] float UiScale();
[[nodiscard]] int UiPx(float logical_px);
[[nodiscard]] Letterbox UiLetterbox();
void DrawLetterboxBars(const Letterbox& box, Color color);

}  // namespace cpptest
