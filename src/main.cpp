#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <raylib.h>

#include "camera.hpp"
#include "dialog.hpp"
#include "display.hpp"
#include "game.hpp"
#include "game_map.hpp"
#include "ldtk_load.hpp"
#include "options.hpp"
#include "player.hpp"
#include "render.hpp"
#include "ui.hpp"

// START REMOVE-ALL STUDY NOTES
// This file is intentionally small. It acts as the game coordinator: it builds the
// game state, updates the systems in order, and hands off drawing to feature modules.
// The gameplay, camera, dialog, and rendering logic all live outside this file so the
// project stays easy to extend as it grows.
// END REMOVE-ALL STUDY NOTES

namespace {

constexpr float kCamZoomSmooth = 20.f;

struct MapCatalogEntry {
  const char* path;
  int level_index = 0;
};

constexpr MapCatalogEntry kMapCatalog[] = {
    {"map/cpptest.ldtk", 0},
    {"map/floor_1.ldtk", 0},
};

enum class MapFade { Idle, Out, In };

Vector3 TileToWorldCenter(const float tx, const float tz) {
  return {tx * g_tileWorld, 0.f, tz * g_tileWorld};
}

}  // namespace

int main() {
  using namespace cpptest;

  GameOptions options = SanitizeGameOptions(GameOptions{});

  // START REMOVE-ALL STUDY NOTES
  // Initialize the application shell first so that everything else has a valid
  // window and render target when it begins to load the map and spawn entities.
  // END REMOVE-ALL STUDY NOTES
  SetTraceLogLevel(LOG_INFO);
  SetConfigFlags(GameOptionsConfigFlags(options));
  InitWindow(options.width, options.height, "cpptest — maps");
  SetWindowMinSize(640, 360);
  SetExitKey(KEY_NULL);
  SetWindowFocused();
  SetTargetFPS(60);
  InitAudioDevice();
  float master_volume = 1.f;
  SetMasterVolume(master_volume);

  GroundDrawResources ground = LoadGroundDrawResources();

  // START REMOVE-ALL STUDY NOTES
  // Load the initial level before the gameplay loop starts so the camera and player
  // begin in a valid world state instead of a blank or uninitialized map.
  // END REMOVE-ALL STUDY NOTES
  GameMap map;
  std::string load_err = LoadLdtkLevel(kMapCatalog[0].path, kMapCatalog[0].level_index, map);
  if (!load_err.empty()) {
    TraceLog(LOG_ERROR, "Map load: %s", load_err.c_str());
  }

  PlayerState player_state{};
  if (load_err.empty()) {
    SpawnPlayerAtFirstWalkable(map, player_state.position);
  }

  int map_index = 0;
  MapFade map_fade = MapFade::Idle;
  float map_fade_t = 0.f;
  int pending_map_index = -1;
  constexpr float k_map_fade_sec = 0.42f;

  int cam_zoom_target = 1;
  const auto dialog_registry = LoadDialogRegistry("dialogs");

  Camera3D camera{};
  camera.fovy = 50.f;
  camera.projection = CAMERA_PERSPECTIVE;

  bool interaction_dialog = false;
  bool has_adjacent_interactable = false;
  std::string active_interactable_name;
  std::string active_interactable_type;
  DialogTree active_dialog;
  bool in_menu = false;

  while (!WindowShouldClose()) {
    const float dt = GetFrameTime();
    GameMode mode = ResolveGameMode(map_fade != MapFade::Idle, interaction_dialog, in_menu);

    if (IsKeyPressed(KEY_ESCAPE)) {
      if (mode == GameMode::Playing) {
        in_menu = true;
      } else if (mode == GameMode::Menu) {
        in_menu = false;
      } else if (mode == GameMode::Dialog) {
        interaction_dialog = false;
      }
    }
    mode = ResolveGameMode(map_fade != MapFade::Idle, interaction_dialog, in_menu);

    // START REMOVE-ALL STUDY NOTES
    // Map transitions happen as a fade rather than a hard reset so the level swap
    // feels smoother and avoids the player noticing the world being reloaded.
    // END REMOVE-ALL STUDY NOTES
    if (IsKeyPressed(KEY_F1)) {
      if (mode == GameMode::Playing) {
        const int n = static_cast<int>(std::size(kMapCatalog));
        pending_map_index = (map_index - 1 + n) % n;
        map_fade = MapFade::Out;
        map_fade_t = 0.f;
      }
    }
    if (IsKeyPressed(KEY_F2)) {
      if (mode == GameMode::Playing) {
        const int n = static_cast<int>(std::size(kMapCatalog));
        pending_map_index = (map_index + 1) % n;
        map_fade = MapFade::Out;
        map_fade_t = 0.f;
      }
    }

    if (map_fade == MapFade::Out) {
      map_fade_t += dt;
      if (map_fade_t >= k_map_fade_sec) {
        load_err = LoadLdtkLevel(kMapCatalog[pending_map_index].path,
                                 kMapCatalog[pending_map_index].level_index, map);
        if (!load_err.empty()) {
          TraceLog(LOG_ERROR, "Map load: %s", load_err.c_str());
        } else {
          map_index = pending_map_index;
          SpawnPlayerAtFirstWalkable(map, player_state.position);
        }
        map_fade = MapFade::In;
        map_fade_t = 0.f;
      }
    } else if (map_fade == MapFade::In) {
      map_fade_t += dt;
      if (map_fade_t >= k_map_fade_sec) {
        map_fade = MapFade::Idle;
        pending_map_index = -1;
      }
    }

    // START REMOVE-ALL STUDY NOTES
    // Camera zoom and orientation controls are treated as direct player view tweaks.
    // They are adjusted in the loop rather than buried in the rendering code.
    // END REMOVE-ALL STUDY NOTES
    if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) {
      cam_zoom_target = std::max(0, cam_zoom_target - 1);
    }
    if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) {
      cam_zoom_target = std::min(3, cam_zoom_target + 1);
    }

    const float cam_goal = kCamDistLevels[cam_zoom_target];
    {
      const float k = 1.f - std::exp(-kCamZoomSmooth * dt);
      g_camDist += (cam_goal - g_camDist) * k;
      if (std::fabs(g_camDist - cam_goal) < 0.004f) {
        g_camDist = cam_goal;
      }
    }

    if (IsKeyDown(KEY_LEFT_BRACKET)) {
      g_tileWorld = std::max(0.25f, g_tileWorld - 1.2f * dt);
    }
    if (IsKeyDown(KEY_RIGHT_BRACKET)) {
      g_tileWorld += 1.2f * dt;
    }
    if (IsKeyDown(KEY_SEMICOLON)) {
      g_camPitchDeg = std::clamp(g_camPitchDeg - 35.f * dt, 28.f, 88.f);
    }
    if (IsKeyDown(KEY_APOSTROPHE)) {
      g_camPitchDeg = std::clamp(g_camPitchDeg + 35.f * dt, 28.f, 88.f);
    }
    if (IsKeyDown(KEY_COMMA)) {
      g_camYawDeg -= 55.f * dt;
    }
    if (IsKeyDown(KEY_PERIOD)) {
      g_camYawDeg += 55.f * dt;
    }

    if (load_err.empty() && mode == GameMode::Playing) {
      Vector2 input{0.f, 0.f};
      if (IsKeyDown(KEY_W)) input.y += 1.f;
      if (IsKeyDown(KEY_S)) input.y -= 1.f;
      if (IsKeyDown(KEY_A)) input.x -= 1.f;
      if (IsKeyDown(KEY_D)) input.x += 1.f;

      // START REMOVE-ALL STUDY NOTES
      // Movement is delegated to the player module so collision, tile snapping, and
      // bounds checks remain isolated from the frame loop.
      // END REMOVE-ALL STUDY NOTES
      UpdatePlayerMovement(player_state, map, input, dt, g_camYawDeg);

      int adjacent_item = 0;
      std::string adjacent_name;
      std::string adjacent_type;
      has_adjacent_interactable = GetAdjacentInteractable(player_state.position, map, adjacent_item,
                                                        adjacent_name, adjacent_type);
      if (IsKeyPressed(KEY_E) && has_adjacent_interactable) {
        interaction_dialog = true;
        active_interactable_name = adjacent_name;
        active_interactable_type = adjacent_type;
        active_dialog = MakeDialogTreeForInstance(active_interactable_name, active_interactable_type,
                                                 dialog_registry);
        active_dialog.current_step = 0;
      }
    } else if (mode == GameMode::Dialog) {
      if (load_err.empty()) {
        UpdatePlayerMovement(player_state, map, Vector2{0.f, 0.f}, dt, g_camYawDeg);
      }
      if (active_dialog.steps.empty()) {
        interaction_dialog = false;
      } else {
        const auto& step = active_dialog.steps[active_dialog.current_step];
        if (step.choices.empty() && IsKeyPressed(KEY_ENTER)) {
          if (active_dialog.current_step < static_cast<int>(active_dialog.steps.size()) - 1) {
            active_dialog.current_step += 1;
          } else {
            interaction_dialog = false;
          }
        }
      }
    } else if (mode == GameMode::Menu) {
      if (load_err.empty()) {
        UpdatePlayerMovement(player_state, map, Vector2{0.f, 0.f}, dt, g_camYawDeg);
      }
    }

    mode = ResolveGameMode(map_fade != MapFade::Idle, interaction_dialog, in_menu);

    Vector3 focus = TileToWorldCenter(player_state.position.x, player_state.position.y);
    focus.y += g_tileWorld * 0.35f;
    UpdateDiabloStyleCamera(camera, focus);

    BeginDrawing();
    ClearBackground(Color{12, 14, 20, 255});
    BeginMode3D(camera);

    if (load_err.empty()) {
      DrawGroundWithMapEdge(ground, map, g_tileWorld);
      DrawWorldGridForMap(map, Color{72, 86, 104, 255});
      DrawWallCells(map);
      DrawInteractables(map);
    }

    const Vector3 feet = TileToWorldCenter(player_state.position.x, player_state.position.y);
    DrawPlayerBlock(feet, Color{210, 115, 70, 255}, Color{35, 18, 10, 255});

    EndMode3D();

    const Letterbox ui = UiLetterbox();
    DrawLetterboxBars(ui, Color{0, 0, 0, 255});

    float fade_overlay = 0.f;
    if (map_fade == MapFade::Out) {
      fade_overlay = std::min(1.f, map_fade_t / k_map_fade_sec);
    } else if (map_fade == MapFade::In) {
      fade_overlay = 1.f - std::min(1.f, map_fade_t / k_map_fade_sec);
    }
    if (fade_overlay > 0.f) {
      const unsigned char a =
          static_cast<unsigned char>(std::lround(std::clamp(fade_overlay, 0.f, 1.f) * 255.f));
      DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, a});
    }

    const float pad = 12.f;
    const float body = kUiTheme.type_body;
    const float line = 24.f;
    DrawLabel(ui, pad, pad, "WASD move   F1/F2 prev/next map (fade)   [ / ] tile size   - / + zoom",
              body, RAYWHITE);
    DrawLabel(ui, pad, pad + line,
              TextFormat("Map %d/%d  %s  level:%s", map_index + 1,
                         static_cast<int>(std::size(kMapCatalog)), kMapCatalog[map_index].path,
                         map.level_identifier.c_str()),
              body, Color{200, 200, 200, 255});
    if (!load_err.empty()) {
      DrawLabel(ui, pad, pad + line * 2, load_err.c_str(), body, Color{255, 120, 120, 255});
    } else {
      DrawLabel(ui, pad, pad + line * 2,
                TextFormat("%dx%d cells  grid_px=%d  tileWorld=%.2f", map.c_wid, map.c_hei, map.grid_px,
                           static_cast<double>(g_tileWorld)),
                body, Color{170, 210, 170, 255});
    }
    DrawLabel(ui, pad, pad + line * 3,
              TextFormat("preset %d/4  dist %.1f  pitch=%.0f yaw=%.0f  %s", cam_zoom_target + 1,
                         static_cast<double>(g_camDist), static_cast<double>(g_camPitchDeg),
                         static_cast<double>(g_camYawDeg), GameModeName(mode)),
              body, Color{160, 200, 230, 255});

    if (mode == GameMode::Dialog) {
      if (!active_dialog.steps.empty()) {
        const auto& step = active_dialog.steps[active_dialog.current_step];
        const float box_w = static_cast<float>(kUiLogicalW) - pad * 2.f;
        const float box_x = pad;
        const int choice_n = static_cast<int>(step.choices.size());
        const float list_h = choice_n > 0 ? UiChoiceListHeight(choice_n) : 0.f;
        const float box_h = std::max(120.f, pad + line + list_h + line + pad);
        const float box_y = static_cast<float>(kUiLogicalH) - box_h - pad;
        DrawPanel(ui, box_x, box_y, box_w, box_h);
        DrawLabel(ui, box_x + pad, box_y + pad, step.text.c_str(), body, kUiTheme.text);
        if (step.choices.empty()) {
          DrawLabel(ui, box_x + pad, box_y + box_h - line, "Press ENTER to continue or ESC to close",
                    body, kUiTheme.text_muted);
        } else {
          std::vector<std::string> labels;
          labels.reserve(step.choices.size());
          for (const auto& choice : step.choices) {
            labels.push_back(choice.label);
          }
          const int picked =
              DrawChoiceList(ui, box_x + pad, box_y + pad + line, box_w - pad * 2.f, labels);
          if (picked >= 0 && picked < choice_n) {
            const int next_step = step.choices[picked].next_step;
            if (next_step >= 0 && next_step < static_cast<int>(active_dialog.steps.size())) {
              active_dialog.current_step = next_step;
            } else {
              interaction_dialog = false;
            }
          }
          DrawLabel(ui, box_x + pad, box_y + box_h - line, "Click or press 1-9, ESC to close", body,
                    kUiTheme.text_muted);
        }
      } else {
        interaction_dialog = false;
      }
    } else if (mode == GameMode::Playing && has_adjacent_interactable) {
      DrawLabel(ui, pad, static_cast<float>(kUiLogicalH) - line - pad, "Press E to interact", body,
                Color{220, 220, 120, 255});
    } else if (mode == GameMode::Menu) {
      DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 140});
      const float panel_w = 520.f;
      const float panel_h = 360.f;
      const float panel_x = (static_cast<float>(kUiLogicalW) - panel_w) * 0.5f;
      const float panel_y = (static_cast<float>(kUiLogicalH) - panel_h) * 0.5f;
      DrawPanel(ui, panel_x, panel_y, panel_w, panel_h);
      const char* paused = "Paused";
      const char* hint = "ESC to resume";
      const char* reset_label = "Reset";
      const float title = kUiTheme.type_title;
      const float inner_x = panel_x + UiSpace(3);
      const float inner_w = panel_w - UiSpace(6);
      DrawLabel(ui, panel_x + (panel_w - UiMeasure(paused, title)) * 0.5f, panel_y + UiSpace(3),
                paused, title, kUiTheme.text);
      DrawLabel(ui, panel_x + (panel_w - UiMeasure(hint, body)) * 0.5f, panel_y + UiSpace(8), hint,
                body, kUiTheme.text_muted);
      const bool vsync = DrawCheckbox(ui, inner_x, panel_y + UiSpace(13), "Vsync", options.vsync);
      if (vsync != options.vsync) {
        options.vsync = vsync;
        ApplyGameOptionsRuntime(options);
      }
      master_volume =
          DrawSlider(ui, inner_x, panel_y + UiSpace(18), inner_w, 0.f, 1.f, master_volume, "Volume",
                     &master_volume);
      SetMasterVolume(master_volume);
      const Vector2 reset_size = UiButtonSize(reset_label);
      const float reset_x = panel_x + (panel_w - reset_size.x) * 0.5f;
      const float reset_y = panel_y + panel_h - reset_size.y - UiSpace(3);
      if (DrawButton(ui, reset_x, reset_y, reset_label, KEY_R) && load_err.empty()) {
        SpawnPlayerAtFirstWalkable(map, player_state.position);
        player_state.velocity = {0.f, 0.f};
        player_state.was_moving = false;
        player_state.was_snapping = false;
      }
    }

    EndDrawing();
  }

  UnloadGroundDrawResources(ground);
  CloseAudioDevice();
  CloseWindow();
  return 0;
}
