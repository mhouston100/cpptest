#include "ui.hpp"

#include <algorithm>
#include <cmath>

namespace cpptest {

float UiMeasure(const char* text, const float logical_size) {
  const float scale = UiScale();
  const int font_px = LogicalToScreen(logical_size, scale);
  if (font_px <= 0 || scale <= 0.f) {
    return 0.f;
  }
  return static_cast<float>(MeasureText(text, font_px)) / scale;
}

void DrawPanel(const Letterbox& ui, const float logical_x, const float logical_y,
               const float logical_w, const float logical_h, const UiTheme& theme) {
  const Rectangle r = LogicalToScreenRect(logical_x, logical_y, logical_w, logical_h, ui);
  DrawRectangleRec(r, theme.panel_fill);
  const float stroke = std::max(1.f, ui.scale);
  DrawRectangleLinesEx(r, stroke, theme.panel_stroke);
}

void DrawLabel(const Letterbox& ui, const float logical_x, const float logical_y, const char* text,
               const float logical_size, const Color color) {
  const Vector2 p = LogicalToScreenPoint(logical_x, logical_y, ui);
  DrawText(text, static_cast<int>(std::lround(p.x)), static_cast<int>(std::lround(p.y)),
           LogicalToScreen(logical_size, ui.scale), color);
}

Vector2 UiButtonSize(const char* label, const UiTheme& theme) {
  const float pad_x = UiSpace(2);
  const float pad_y = UiSpace(1);
  return Vector2{UiMeasure(label, theme.type_body) + pad_x * 2.f, theme.type_body + pad_y * 2.f};
}

bool DrawButton(const Letterbox& ui, const float logical_x, const float logical_y, const char* label,
                const int activate_key, const UiTheme& theme) {
  const Vector2 size = UiButtonSize(label, theme);
  const Rectangle screen = LogicalToScreenRect(logical_x, logical_y, size.x, size.y, ui);
  const bool hover = CheckCollisionPointRec(GetMousePosition(), screen);
  const bool mouse_down = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
  Color fill = theme.button_fill;
  if (mouse_down) {
    fill = theme.button_press;
  } else if (hover) {
    fill = theme.button_hover;
  }
  DrawRectangleRec(screen, fill);
  const float stroke = std::max(1.f, ui.scale);
  DrawRectangleLinesEx(screen, stroke, theme.button_stroke);
  const float text_x = logical_x + (size.x - UiMeasure(label, theme.type_body)) * 0.5f;
  const float text_y = logical_y + (size.y - theme.type_body) * 0.5f;
  DrawLabel(ui, text_x, text_y, label, theme.type_body, theme.text);
  const bool clicked = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
  const bool keyed = activate_key != 0 && IsKeyPressed(activate_key);
  return clicked || keyed;
}

bool DrawCheckbox(const Letterbox& ui, const float logical_x, const float logical_y,
                  const char* label, const bool checked, const UiTheme& theme) {
  const float box = theme.type_body;
  const float row_w = box + UiSpace(1) + UiMeasure(label, theme.type_body);
  const Rectangle hit = LogicalToScreenRect(logical_x, logical_y, row_w, box, ui);
  const bool hover = CheckCollisionPointRec(GetMousePosition(), hit);
  const bool toggled = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
  const Rectangle box_r = LogicalToScreenRect(logical_x, logical_y, box, box, ui);
  DrawRectangleRec(box_r, hover ? theme.button_hover : theme.button_fill);
  DrawRectangleLinesEx(box_r, std::max(1.f, ui.scale), theme.button_stroke);
  if (checked) {
    const float inset = 4.f;
    DrawRectangleRec(LogicalToScreenRect(logical_x + inset, logical_y + inset, box - inset * 2.f,
                                         box - inset * 2.f, ui),
                     theme.text);
  }
  DrawLabel(ui, logical_x + box + UiSpace(1), logical_y, label, theme.type_body, theme.text);
  return toggled ? !checked : checked;
}

float DrawSlider(const Letterbox& ui, const float logical_x, const float logical_y,
                 const float logical_w, const float vmin, const float vmax, float value,
                 const char* label, const void* id, const UiTheme& theme) {
  static const void* dragging = nullptr;
  const float track_h = UiSpace(2);
  const float label_h = theme.type_body;
  const float span = std::max(0.0001f, vmax - vmin);
  value = std::clamp(value, vmin, vmax);
  DrawLabel(ui, logical_x, logical_y,
            TextFormat("%s  %.0f%%", label, static_cast<double>((value - vmin) / span * 100.f)),
            theme.type_body, theme.text);
  const float track_y = logical_y + label_h + UiSpace(1);
  const Rectangle track = LogicalToScreenRect(logical_x, track_y, logical_w, track_h, ui);
  DrawRectangleRec(track, theme.button_fill);
  DrawRectangleLinesEx(track, std::max(1.f, ui.scale), theme.button_stroke);

  const float t = (value - vmin) / span;
  const float knob_w = UiSpace(2);
  const float knob_x = logical_x + t * (logical_w - knob_w);
  DrawRectangleRec(LogicalToScreenRect(knob_x, track_y - 4.f, knob_w, track_h + 8.f, ui),
                   theme.button_hover);

  const bool hover = CheckCollisionPointRec(GetMousePosition(), track);
  if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    dragging = id;
  }
  if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && dragging == id) {
    dragging = nullptr;
  }
  if (dragging == id && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
    const float local = (GetMousePosition().x - track.x) / std::max(1.f, track.width);
    value = vmin + std::clamp(local, 0.f, 1.f) * span;
  }
  return value;
}

int DrawChoiceList(const Letterbox& ui, const float logical_x, const float logical_y,
                   const float logical_w, const std::vector<std::string>& labels,
                   const int selected_index, const bool number_keys, const UiTheme& theme) {
  const float row_h = UiChoiceRowHeight(theme);
  int picked = -1;
  const int n = static_cast<int>(labels.size());
  for (int i = 0; i < n; ++i) {
    const float y = logical_y + row_h * static_cast<float>(i);
    const Rectangle hit = LogicalToScreenRect(logical_x, y, logical_w, row_h, ui);
    const bool hover = CheckCollisionPointRec(GetMousePosition(), hit);
    if (i == selected_index) {
      DrawRectangleRec(hit, theme.button_fill);
    }
    if (hover) {
      DrawRectangleRec(hit, theme.button_hover);
    }
    const char* row =
        number_keys ? TextFormat("%d: %s", i + 1, labels[i].c_str()) : labels[i].c_str();
    DrawLabel(ui, logical_x + UiSpace(1), y + (row_h - theme.type_body) * 0.5f, row, theme.type_body,
              theme.text);
    if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      picked = i;
    }
    if (number_keys && i < 9 && IsKeyPressed(KEY_ONE + i)) {
      picked = i;
    }
  }
  return picked;
}

}  // namespace cpptest
