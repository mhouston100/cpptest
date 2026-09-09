#pragma once

#include <string>
#include <vector>

#include <raylib.h>

#include "display.hpp"

namespace cpptest {

constexpr float kUiSpaceStep = 8.f;
constexpr float kUiTypeBody = 18.f;
constexpr float kUiTypeTitle = 28.f;

struct UiTheme {
  Color panel_fill{12, 14, 20, 215};
  Color panel_stroke{190, 190, 240, 255};
  Color text{245, 245, 245, 255};
  Color text_muted{160, 160, 180, 255};
  Color button_fill{36, 42, 58, 255};
  Color button_hover{52, 62, 88, 255};
  Color button_press{24, 28, 40, 255};
  Color button_stroke{190, 190, 240, 255};
  float space_step = kUiSpaceStep;
  float type_body = kUiTypeBody;
  float type_title = kUiTypeTitle;
};

inline constexpr UiTheme kUiTheme{};

[[nodiscard]] constexpr float UiSpace(const int steps) {
  return static_cast<float>(steps) * kUiSpaceStep;
}

[[nodiscard]] float UiMeasure(const char* text, float logical_size);
[[nodiscard]] Vector2 UiButtonSize(const char* label, const UiTheme& theme = kUiTheme);
void DrawPanel(const Letterbox& ui, float logical_x, float logical_y, float logical_w,
               float logical_h, const UiTheme& theme = kUiTheme);
void DrawLabel(const Letterbox& ui, float logical_x, float logical_y, const char* text,
               float logical_size, Color color);
[[nodiscard]] bool DrawButton(const Letterbox& ui, float logical_x, float logical_y,
                              const char* label, int activate_key = 0,
                              const UiTheme& theme = kUiTheme);
[[nodiscard]] bool DrawCheckbox(const Letterbox& ui, float logical_x, float logical_y,
                                const char* label, bool checked, const UiTheme& theme = kUiTheme);
[[nodiscard]] float DrawSlider(const Letterbox& ui, float logical_x, float logical_y, float logical_w,
                               float vmin, float vmax, float value, const char* label,
                               const void* id, const UiTheme& theme = kUiTheme);
[[nodiscard]] constexpr float UiChoiceRowHeight(const UiTheme& theme = kUiTheme) {
  return theme.type_body + UiSpace(1);
}
[[nodiscard]] constexpr float UiChoiceListHeight(const int count, const UiTheme& theme = kUiTheme) {
  return UiChoiceRowHeight(theme) * static_cast<float>(count < 0 ? 0 : count);
}
[[nodiscard]] int DrawChoiceList(const Letterbox& ui, float logical_x, float logical_y,
                                 float logical_w, const std::vector<std::string>& labels,
                                 int selected_index = -1, bool number_keys = true,
                                 const UiTheme& theme = kUiTheme);

}  // namespace cpptest
