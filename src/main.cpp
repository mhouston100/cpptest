#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include <raylib.h>

#include "camera.hpp"
#include "day.hpp"
#include "dialog.hpp"
#include "dice.hpp"
#include "display.hpp"
#include "game.hpp"
#include "game_map.hpp"
#include "ldtk_load.hpp"
#include "map_catalog.hpp"
#include "npc.hpp"
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
constexpr float kWarpPadHalfExtent = 0.25f;

enum class MapFade { Idle, Out, In };
enum class PausePage { Root, Options };

int WindowModeIndex(const cpptest::WindowMode mode) {
  switch (mode) {
    case cpptest::WindowMode::Borderless:
      return 1;
    case cpptest::WindowMode::Fullscreen:
      return 2;
    case cpptest::WindowMode::Windowed:
    default:
      return 0;
  }
}

cpptest::WindowMode WindowModeFromIndex(const int index) {
  if (index == 1) {
    return cpptest::WindowMode::Borderless;
  }
  if (index == 2) {
    return cpptest::WindowMode::Fullscreen;
  }
  return cpptest::WindowMode::Windowed;
}

Vector3 TileToWorldCenter(const float tx, const float tz) {
  return {tx * g_tileWorld, 0.f, tz * g_tileWorld};
}

}  // namespace

int main() {
  using namespace cpptest;

  GameOptions options = LoadGameOptions();

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
  MapVisuals map_visuals = LoadMapVisuals();

  // START REMOVE-ALL STUDY NOTES
  // Load the initial level before the gameplay loop starts so the camera and player
  // begin in a valid world state instead of a blank or uninitialized map.
  // END REMOVE-ALL STUDY NOTES
  MapCatalog catalog;
  std::string catalog_err = LoadMapCatalog("map/catalog.json", catalog);
  if (!catalog_err.empty()) {
    TraceLog(LOG_ERROR, "Map catalog: %s", catalog_err.c_str());
  }

  GameMap map;
  std::string current_map_id;
  std::string load_err = catalog_err;
  const MapCatalogEntry* start_map = CatalogStart(catalog);
  if (load_err.empty() && start_map != nullptr) {
    load_err = LoadLdtkLevel(start_map->path, start_map->level_index, map);
    if (load_err.empty()) {
      current_map_id = start_map->id;
    }
  }
  if (!load_err.empty()) {
    TraceLog(LOG_ERROR, "Map load: %s", load_err.c_str());
  }

  PlayerState player_state{};
  if (load_err.empty()) {
    SpawnPlayerAtSpawn(map, "start", player_state.position);
  }

  MapFade map_fade = MapFade::Idle;
  float map_fade_t = 0.f;
  const MapCatalogEntry* pending_map = nullptr;
  std::string pending_spawn = "start";
  bool warp_armed = true;
  constexpr float k_map_fade_sec = 0.42f;

  int cam_zoom_target = 1;
  const auto dialog_registry = LoadDialogRegistry("dialogs");
  NpcRegistry npc_registry;
  const std::string npc_err = LoadNpcRegistry("npcs/registry.json", npc_registry);
  if (!npc_err.empty()) {
    TraceLog(LOG_WARNING, "NPC registry: %s", npc_err.c_str());
  }
  PlayerSave player_save = LoadPlayerSave();
  HydrateNpcSave(player_save, npc_registry);
  EnsureDaySave(player_save);
  EnsureDiceBag(player_save);
  if (load_err.empty()) {
    CaptureNpcPads(map, current_map_id, npc_registry, player_save);
  }
  SyncNpcSchedules(player_save, npc_registry);
  SavePlayerSave(player_save);

  std::unordered_map<std::string, IncidentDef> incident_registry;
  const std::string incident_err = LoadIncidentRegistry("incidents", incident_registry);
  if (!incident_err.empty()) {
    TraceLog(LOG_WARNING, "Incidents: %s", incident_err.c_str());
  }

  Camera3D camera{};
  camera.fovy = 50.f;
  camera.projection = CAMERA_PERSPECTIVE;

  bool interaction_dialog = false;
  bool has_adjacent_interactable = false;
  TalkTarget adjacent_talk{};
  TalkTarget active_talk{};
  DialogTree active_dialog;
  bool in_menu = false;
  bool in_sleep = false;
  bool in_incident = false;
  bool sleep_forced = false;
  SleepSummary sleep_preview{};
  IncidentPlay incident_play{};
  PausePage pause_page = PausePage::Root;
  GameOptions draft_options{};
  int draft_mode = 0;
  int draft_res = 2;

  const auto open_sleep = [&](const bool forced) {
    sleep_preview = MakeSleepSummary(player_save);
    in_sleep = true;
    sleep_forced = forced;
    in_menu = false;
    interaction_dialog = false;
    in_incident = false;
  };
  const auto confirm_sleep = [&]() {
    ApplySleep(player_save, sleep_preview);
    SyncNpcSchedules(player_save, npc_registry);
    SavePlayerSave(player_save);
    in_sleep = false;
    sleep_forced = false;
  };
  const auto maybe_force_sleep = [&]() {
    if (!in_sleep && DayShouldForceSleep(player_save)) {
      open_sleep(true);
    }
  };
  const auto try_start_queued_incident = [&]() {
    if (player_save.queued_incident.empty()) {
      return;
    }
    const std::string id = player_save.queued_incident;
    player_save.queued_incident.clear();
    const IncidentDef* def = FindIncident(incident_registry, id);
    if (def == nullptr) {
      TraceLog(LOG_WARNING, "Unknown incident: %s", id.c_str());
      return;
    }
    EnsureDiceBag(player_save);
    BeginIncident(incident_play, *def);
    in_incident = true;
    interaction_dialog = false;
    in_menu = false;
  };
  const auto finish_incident = [&]() {
    if (!incident_play.resolved) {
      ResolveIncident(incident_play, player_save);
    }
    SyncNpcSchedules(player_save, npc_registry);
    SavePlayerSave(player_save);
    maybe_force_sleep();
  };

  while (!WindowShouldClose()) {
    const float dt = GetFrameTime();
    GameMode mode =
        ResolveGameMode(map_fade != MapFade::Idle, interaction_dialog, in_menu, in_sleep,
                        in_incident);

    if (IsKeyPressed(KEY_ESCAPE)) {
      if (mode == GameMode::Playing) {
        in_menu = true;
        pause_page = PausePage::Root;
      } else if (mode == GameMode::Menu) {
        if (pause_page == PausePage::Options) {
          pause_page = PausePage::Root;
        } else {
          in_menu = false;
        }
      } else if (mode == GameMode::Sleep) {
        if (sleep_forced) {
          confirm_sleep();
        } else {
          in_sleep = false;
        }
      } else if (mode == GameMode::Incident) {
        if (incident_play.resolved || !incident_play.rolled) {
          in_incident = false;
        } else {
          finish_incident();
        }
      } else if (mode == GameMode::Dialog) {
        interaction_dialog = false;
      }
    }
    mode = ResolveGameMode(map_fade != MapFade::Idle, interaction_dialog, in_menu, in_sleep,
                           in_incident);

    // START REMOVE-ALL STUDY NOTES
    // Map transitions happen as a fade rather than a hard reset so the level swap
    // feels smoother and avoids the player noticing the world being reloaded.
    // END REMOVE-ALL STUDY NOTES
    if (IsKeyPressed(KEY_F1) || IsKeyPressed(KEY_F2)) {
      if (mode == GameMode::Playing && !catalog.maps.empty()) {
        const int n = static_cast<int>(catalog.maps.size());
        int index = CatalogIndexOf(catalog, current_map_id);
        if (index < 0) {
          index = 0;
        }
        index = (index + (IsKeyPressed(KEY_F2) ? 1 : n - 1)) % n;
        pending_map = &catalog.maps[static_cast<size_t>(index)];
        pending_spawn = "start";
        map_fade = MapFade::Out;
        map_fade_t = 0.f;
      }
    }

    if (map_fade == MapFade::Out) {
      map_fade_t += dt;
      if (map_fade_t >= k_map_fade_sec) {
        if (pending_map == nullptr) {
          load_err = "Warp target is missing from the map catalog";
          TraceLog(LOG_ERROR, "Map load: %s", load_err.c_str());
        } else {
          load_err = LoadLdtkLevel(pending_map->path, pending_map->level_index, map);
          if (!load_err.empty()) {
            TraceLog(LOG_ERROR, "Map load: %s", load_err.c_str());
          } else {
            current_map_id = pending_map->id;
            SpawnPlayerAtSpawn(map, pending_spawn, player_state.position);
            player_state.velocity = {0.f, 0.f};
            warp_armed = false;
            CaptureNpcPads(map, current_map_id, npc_registry, player_save);
            SyncNpcSchedules(player_save, npc_registry);
          }
        }
        map_fade = MapFade::In;
        map_fade_t = 0.f;
      }
    } else if (map_fade == MapFade::In) {
      map_fade_t += dt;
      if (map_fade_t >= k_map_fade_sec) {
        map_fade = MapFade::Idle;
        pending_map = nullptr;
        maybe_force_sleep();
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
      SyncNpcSchedules(player_save, npc_registry);
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

      int cell_x = 0;
      int cell_y = 0;
      PlayerToCell(player_state.position, map, cell_x, cell_y);
      const MapEntity* warp = map.FindAt(cell_x, cell_y, MapEntityKind::Warp);
      if (warp == nullptr) {
        warp_armed = true;
      } else if (warp_armed &&
                 PlayerInCellInset(player_state.position, map, warp->cell_x, warp->cell_y,
                                   kWarpPadHalfExtent)) {
        const MapCatalogEntry* dest = FindCatalogMap(catalog, warp->target_map);
        if (dest == nullptr) {
          TraceLog(LOG_WARNING, "Warp target not in catalog: %s", warp->target_map.c_str());
          warp_armed = false;
        } else {
          pending_map = dest;
          pending_spawn = warp->target_spawn.empty() ? "start" : warp->target_spawn;
          map_fade = MapFade::Out;
          map_fade_t = 0.f;
          warp_armed = false;
          ApplyDayDelta(player_save, kTravelMinutes, kTravelEnergy, 0, 0);
          SyncNpcSchedules(player_save, npc_registry);
          SavePlayerSave(player_save);
        }
      }

      if (IsKeyPressed(KEY_N)) {
        open_sleep(false);
      }

      has_adjacent_interactable =
          FindAdjacentTalkTarget(player_state.position, map, current_map_id, npc_registry,
                                 player_save, adjacent_talk);
      if (!has_adjacent_interactable) {
        adjacent_talk = {};
      }
      if (IsKeyPressed(KEY_E) && has_adjacent_interactable) {
        active_talk = adjacent_talk;
        active_dialog = MakeDialogTreeForInstance(active_talk.display_name, active_talk.dialog_key,
                                                 dialog_registry);
        if (OpenDialogOnValidStep(active_dialog, player_save, active_talk.id)) {
          if (active_talk.kind == MapEntityKind::Npc) {
            ApplyDayDelta(player_save, kTalkMinutes, kTalkEnergy, 0, 0);
          }
          interaction_dialog = true;
          SyncNpcSchedules(player_save, npc_registry);
          SavePlayerSave(player_save);
          try_start_queued_incident();
          maybe_force_sleep();
        }
      }
    } else if (mode == GameMode::Dialog) {
      if (load_err.empty()) {
        UpdatePlayerMovement(player_state, map, Vector2{0.f, 0.f}, dt, g_camYawDeg);
      }
      if (active_dialog.steps.empty()) {
        interaction_dialog = false;
      } else {
        const auto& step = active_dialog.steps[active_dialog.current_step];
        std::vector<int> visible_indices;
        std::vector<std::string> visible_labels;
        VisibleDialogChoices(step, player_save, active_talk.id, visible_indices, visible_labels);
        if (visible_labels.empty() && IsKeyPressed(KEY_ENTER)) {
          if (AdvanceDialogNoChoice(active_dialog, player_save, active_talk.id) ==
              DialogAdvance::Close) {
            interaction_dialog = false;
          }
          SyncNpcSchedules(player_save, npc_registry);
          SavePlayerSave(player_save);
          try_start_queued_incident();
          maybe_force_sleep();
        }
      }
    } else if (mode == GameMode::Incident) {
      if (load_err.empty()) {
        UpdatePlayerMovement(player_state, map, Vector2{0.f, 0.f}, dt, g_camYawDeg);
      }
      if (!incident_play.resolved) {
        const int hold_keys[kDiceCount] = {KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE};
        for (int i = 0; i < kDiceCount; ++i) {
          if (IsKeyPressed(hold_keys[i])) {
            ToggleIncidentHold(incident_play, i);
          }
        }
        if (incident_play.rolls_left > 0 && IsKeyPressed(KEY_SPACE)) {
          RollIncidentDice(incident_play, player_save.dice);
        }
      }
    } else if (mode == GameMode::Menu || mode == GameMode::Sleep) {
      if (load_err.empty()) {
        UpdatePlayerMovement(player_state, map, Vector2{0.f, 0.f}, dt, g_camYawDeg);
      }
    }

    mode = ResolveGameMode(map_fade != MapFade::Idle, interaction_dialog, in_menu, in_sleep,
                           in_incident);

    Vector3 focus = TileToWorldCenter(player_state.position.x, player_state.position.y);
    focus.y += g_tileWorld * 0.35f;
    UpdateDiabloStyleCamera(camera, focus);

    BeginDrawing();
    ClearBackground(Color{12, 14, 20, 255});
    BeginMode3D(camera);

    if (load_err.empty()) {
      SyncNpcSchedules(player_save, npc_registry);
      DrawGroundWithMapEdge(ground, map, g_tileWorld);
      DrawWorldGridForMap(map, Color{72, 86, 104, 255});
      DrawTiledMap(map_visuals, map);
      DrawMapEntities(map);
      std::vector<WorldNpcPose> npc_poses;
      CollectNpcsOnMap(map, current_map_id, npc_registry, player_save, npc_poses);
      DrawNpcMarkers(map, npc_poses);
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
    DrawLabel(ui, pad, pad, "WASD move   E talk   N sleep   walk onto blue to travel   F1/F2 debug map",
              body, RAYWHITE);
    const int map_index = CatalogIndexOf(catalog, current_map_id);
    DrawLabel(ui, pad, pad + line,
              TextFormat("Map %d/%d  %s  %s  level:%s", map_index + 1,
                         static_cast<int>(catalog.maps.size()), current_map_id.c_str(),
                         map.source_path.c_str(), map.level_identifier.c_str()),
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

    {
      const std::string clock = FormatClock(player_save.minutes);
      const std::string hud_day = TextFormat("Day %d   %s", player_save.day, clock.c_str());
      const std::string hud_stats =
          GetFlag(player_save, "job_helpdesk")
              ? std::string(TextFormat("Helpdesk   Energy %d   Stress %d   $%d", player_save.energy,
                                       player_save.stress, player_save.money))
              : std::string(TextFormat("Energy %d   Stress %d   $%d", player_save.energy,
                                       player_save.stress, player_save.money));
      const float hud_w = std::max(UiMeasure(hud_day.c_str(), body), UiMeasure(hud_stats.c_str(), body));
      const float hud_x = static_cast<float>(kUiLogicalW) - pad * 2.f - hud_w;
      const float hud_y = pad;
      const float hud_h = pad + line * 2.f + pad * 0.5f;
      DrawPanel(ui, hud_x - pad, hud_y, hud_w + pad * 2.f, hud_h);
      Color clock_color = kUiTheme.text;
      if (IsVeryLate(player_save.minutes)) {
        clock_color = Color{255, 140, 110, 255};
      } else if (IsLate(player_save.minutes)) {
        clock_color = Color{255, 200, 120, 255};
      }
      DrawLabel(ui, hud_x, hud_y + pad * 0.5f, hud_day.c_str(), body, clock_color);
      const Color energy_color =
          player_save.energy < 40 ? Color{255, 170, 120, 255} : kUiTheme.text;
      DrawLabel(ui, hud_x, hud_y + pad * 0.5f + line, hud_stats.c_str(), body, energy_color);
    }

    if (mode == GameMode::Sleep) {
      DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 140});
      const float title = kUiTheme.type_title;
      const float panel_w = 640.f;
      const float panel_h = 420.f;
      const float panel_x = (static_cast<float>(kUiLogicalW) - panel_w) * 0.5f;
      const float panel_y = (static_cast<float>(kUiLogicalH) - panel_h) * 0.5f;
      DrawPanel(ui, panel_x, panel_y, panel_w, panel_h);
      const char* heading = sleep_forced ? "You collapsed" : "Call it a night";
      const float inner_x = panel_x + UiSpace(3);
      DrawLabel(ui, panel_x + (panel_w - UiMeasure(heading, title)) * 0.5f, panel_y + UiSpace(3),
                heading, title, kUiTheme.text);
      float y = panel_y + UiSpace(9);
      DrawLabel(ui, inner_x, y,
                TextFormat("Day %d ended at %s", sleep_preview.from_day,
                           FormatClock(sleep_preview.bedtime_minutes).c_str()),
                body, kUiTheme.text);
      y += line;
      if (sleep_preview.worked_late) {
        DrawLabel(ui, inner_x, y, "You stayed late on tickets. Morning energy takes the hit.", body,
                  Color{255, 180, 120, 255});
        y += line;
      } else if (sleep_preview.very_late) {
        DrawLabel(ui, inner_x, y, "Turned in after 22:00. Sleep was thin.", body,
                  Color{255, 180, 120, 255});
        y += line;
      } else if (sleep_preview.stayed_late) {
        DrawLabel(ui, inner_x, y, "Past 18:00 — tomorrow starts slower.", body,
                  Color{255, 210, 140, 255});
        y += line;
      } else {
        DrawLabel(ui, inner_x, y, "A reasonable hour. You should recover.", body, kUiTheme.text_muted);
        y += line;
      }
      y += UiSpace(1);
      DrawLabel(ui, inner_x, y,
                TextFormat("Energy %d  ->  %d", sleep_preview.energy_before, sleep_preview.energy_after),
                body, kUiTheme.text);
      y += line;
      DrawLabel(ui, inner_x, y,
                TextFormat("Stress %d  ->  %d", sleep_preview.stress_before, sleep_preview.stress_after),
                body, kUiTheme.text);
      y += line;
      DrawLabel(ui, inner_x, y, TextFormat("$%d in the account", sleep_preview.money), body,
                kUiTheme.text);
      const char* footer = sleep_forced ? "ENTER to wake" : "ENTER to wake   ESC to keep going";
      DrawLabel(ui, panel_x + (panel_w - UiMeasure(footer, body)) * 0.5f,
                panel_y + panel_h - UiSpace(10), footer, body, kUiTheme.text_muted);
      const Vector2 wake_size = UiButtonSize("Wake");
      if (DrawButton(ui, panel_x + (panel_w - wake_size.x) * 0.5f,
                     panel_y + panel_h - wake_size.y - UiSpace(3), "Wake", KEY_ENTER)) {
        confirm_sleep();
      }
    } else if (mode == GameMode::Incident) {
      DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 140});
      const float title = kUiTheme.type_title;
      const float panel_w = 760.f;
      const float panel_h = 520.f;
      const float panel_x = (static_cast<float>(kUiLogicalW) - panel_w) * 0.5f;
      const float panel_y = (static_cast<float>(kUiLogicalH) - panel_h) * 0.5f;
      DrawPanel(ui, panel_x, panel_y, panel_w, panel_h);
      EnsureDiceBag(player_save);
      const float inner_x = panel_x + UiSpace(3);
      DrawLabel(ui, panel_x + (panel_w - UiMeasure(incident_play.def.title.c_str(), title)) * 0.5f,
                panel_y + UiSpace(3), incident_play.def.title.c_str(), title, kUiTheme.text);
      float y = panel_y + UiSpace(9);
      if (!incident_play.def.prompt.empty()) {
        DrawLabel(ui, inner_x, y, incident_play.def.prompt.c_str(), body, kUiTheme.text_muted);
        y += line;
      }
      DrawLabel(ui, inner_x, y,
                TextFormat("Target %d   skill %s   +%d if a %s face shows", incident_play.def.target,
                           incident_play.def.skill.c_str(), incident_play.def.skill_bonus,
                           incident_play.def.skill.c_str()),
                body, kUiTheme.text);
      y += line * 2.f;

      UiTheme held_theme = kUiTheme;
      held_theme.button_fill = Color{70, 92, 48, 255};
      held_theme.button_hover = Color{88, 112, 58, 255};
      float die_x = inner_x;
      const float die_gap = UiSpace(2);
      for (int i = 0; i < kDiceCount; ++i) {
        std::string label = "-";
        if (incident_play.rolled) {
          label = std::to_string(incident_play.pips[static_cast<size_t>(i)]);
          const std::string tag =
              DieFaceTag(player_save.dice.dice[static_cast<size_t>(i)],
                         incident_play.pips[static_cast<size_t>(i)]);
          if (!tag.empty()) {
            label += " ";
            label += tag;
          }
          if (incident_play.held[static_cast<size_t>(i)]) {
            label = "[" + label + "]";
          }
        }
        const bool held = incident_play.held[static_cast<size_t>(i)];
        if (DrawButton(ui, die_x, y, label.c_str(), 0, held ? held_theme : kUiTheme) &&
            !incident_play.resolved) {
          ToggleIncidentHold(incident_play, i);
        }
        die_x += UiButtonSize(label.c_str(), held ? held_theme : kUiTheme).x + die_gap;
      }
      y += UiButtonSize("6 WEB", kUiTheme).y + UiSpace(2);
      DrawLabel(ui, inner_x, y, "Click a die or 1-5 to hold. R / Space to roll.", body,
                kUiTheme.text_muted);
      y += line * 2.f;

      if (incident_play.rolled) {
        if (incident_play.tagged) {
          DrawLabel(ui, inner_x, y,
                    TextFormat("Sum %d  +  %d %s  =  %d  vs  %d", incident_play.pip_sum,
                               incident_play.bonus, incident_play.def.skill.c_str(), incident_play.score,
                               incident_play.def.target),
                    body, Color{180, 220, 140, 255});
        } else {
          DrawLabel(ui, inner_x, y,
                    TextFormat("Sum %d  (no %s face)  vs  %d", incident_play.pip_sum,
                               incident_play.def.skill.c_str(), incident_play.def.target),
                    body, kUiTheme.text);
        }
        y += line;
      }
      DrawLabel(ui, inner_x, y, TextFormat("Rolls left: %d", incident_play.rolls_left), body,
                kUiTheme.text_muted);
      y += line;

      if (incident_play.resolved) {
        const char* result = incident_play.success ? "Reproduced. Ticket closed." : "Could not reproduce.";
        DrawLabel(ui, inner_x, y, result, body,
                  incident_play.success ? Color{180, 220, 140, 255} : Color{255, 170, 120, 255});
        y += line;
        DrawLabel(ui, inner_x, y,
                  TextFormat("%s  $%d  stress %+d", incident_play.success ? "Pass" : "Fail",
                             incident_play.success ? incident_play.def.success.money
                                                   : incident_play.def.fail.money,
                             incident_play.success ? incident_play.def.success.stress
                                                   : incident_play.def.fail.stress),
                  body, kUiTheme.text);
        const Vector2 close_size = UiButtonSize("Close");
        if (DrawButton(ui, panel_x + (panel_w - close_size.x) * 0.5f,
                       panel_y + panel_h - close_size.y - UiSpace(3), "Close", KEY_ENTER)) {
          in_incident = false;
        }
      } else {
        const Vector2 roll_size = UiButtonSize("Roll");
        const Vector2 bank_size = UiButtonSize("Bank");
        const float gap = UiSpace(2);
        const float btn_y = panel_y + panel_h - roll_size.y - UiSpace(3);
        const float pair_w = roll_size.x + gap + bank_size.x;
        const float roll_x = panel_x + (panel_w - pair_w) * 0.5f;
        if (incident_play.rolls_left > 0 && DrawButton(ui, roll_x, btn_y, "Roll", KEY_R)) {
          RollIncidentDice(incident_play, player_save.dice);
        }
        if (incident_play.rolled &&
            DrawButton(ui, roll_x + roll_size.x + gap, btn_y, "Bank", KEY_ENTER)) {
          finish_incident();
        }
      }
    } else if (mode == GameMode::Dialog) {
      if (!active_dialog.steps.empty()) {
        const auto& step = active_dialog.steps[active_dialog.current_step];
        std::vector<int> visible_indices;
        std::vector<std::string> visible_labels;
        VisibleDialogChoices(step, player_save, active_talk.id, visible_indices, visible_labels);
        const std::string spoken = FormatDialogText(step, active_talk, player_save);
        const float box_w = static_cast<float>(kUiLogicalW) - pad * 2.f;
        const float box_x = pad;
        const int choice_n = static_cast<int>(visible_labels.size());
        const float list_h = choice_n > 0 ? UiChoiceListHeight(choice_n) : 0.f;
        const float box_h = std::max(120.f, pad + line + list_h + line + pad);
        const float box_y = static_cast<float>(kUiLogicalH) - box_h - pad;
        DrawPanel(ui, box_x, box_y, box_w, box_h);
        DrawLabel(ui, box_x + pad, box_y + pad, spoken.c_str(), body, kUiTheme.text);
        if (visible_labels.empty()) {
          DrawLabel(ui, box_x + pad, box_y + box_h - line, "Press ENTER to continue or ESC to close",
                    body, kUiTheme.text_muted);
        } else {
          const int picked =
              DrawChoiceList(ui, box_x + pad, box_y + pad + line, box_w - pad * 2.f, visible_labels);
          if (picked >= 0 && picked < choice_n) {
            const int choice_index = visible_indices[static_cast<size_t>(picked)];
            if (PickDialogChoice(active_dialog, choice_index, player_save, active_talk.id) ==
                DialogAdvance::Close) {
              interaction_dialog = false;
            }
            SyncNpcSchedules(player_save, npc_registry);
            SavePlayerSave(player_save);
            try_start_queued_incident();
            maybe_force_sleep();
          }
          DrawLabel(ui, box_x + pad, box_y + box_h - line, "Click or press 1-9, ESC to close", body,
                    kUiTheme.text_muted);
        }
      } else {
        interaction_dialog = false;
      }
    } else if (mode == GameMode::Playing && has_adjacent_interactable) {
      const float prompt_y = static_cast<float>(kUiLogicalH) - line - pad;
      if (DayShouldHintSleep(player_save)) {
        DrawLabel(ui, pad, prompt_y - line, "It's late. Press N to sleep.", body,
                  Color{255, 180, 120, 255});
      }
      if (adjacent_talk.kind == MapEntityKind::Npc) {
        adjacent_talk.relationship = GetRelationship(player_save, adjacent_talk.id);
        DrawLabel(ui, pad, prompt_y,
                  TextFormat("Press E to talk to %s  ·  rel %d", adjacent_talk.display_name.c_str(),
                             adjacent_talk.relationship),
                  body, Color{220, 220, 120, 255});
      } else {
        DrawLabel(ui, pad, prompt_y,
                  TextFormat("Press E to inspect %s", adjacent_talk.display_name.c_str()), body,
                  Color{220, 220, 120, 255});
      }
    } else if (mode == GameMode::Playing && DayShouldHintSleep(player_save)) {
      DrawLabel(ui, pad, static_cast<float>(kUiLogicalH) - line - pad, "It's late. Press N to sleep.",
                body, Color{255, 180, 120, 255});
    } else if (mode == GameMode::Menu) {
      DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 140});
      const float title = kUiTheme.type_title;
      if (pause_page == PausePage::Root) {
        const float panel_w = 520.f;
        const float panel_h = 380.f;
        const float panel_x = (static_cast<float>(kUiLogicalW) - panel_w) * 0.5f;
        const float panel_y = (static_cast<float>(kUiLogicalH) - panel_h) * 0.5f;
        DrawPanel(ui, panel_x, panel_y, panel_w, panel_h);
        const char* paused = "Paused";
        const char* hint = "ESC to resume";
        const float inner_x = panel_x + UiSpace(3);
        const float inner_w = panel_w - UiSpace(6);
        DrawLabel(ui, panel_x + (panel_w - UiMeasure(paused, title)) * 0.5f, panel_y + UiSpace(3),
                  paused, title, kUiTheme.text);
        DrawLabel(ui, panel_x + (panel_w - UiMeasure(hint, body)) * 0.5f, panel_y + UiSpace(8), hint,
                  body, kUiTheme.text_muted);
        master_volume =
            DrawSlider(ui, inner_x, panel_y + UiSpace(12), inner_w, 0.f, 1.f, master_volume, "Volume",
                       &master_volume);
        SetMasterVolume(master_volume);
        const Vector2 opt_size = UiButtonSize("Options");
        const Vector2 reset_size = UiButtonSize("Reset");
        const Vector2 sleep_size = UiButtonSize("Sleep");
        const float gap = UiSpace(2);
        const float btn_y = panel_y + panel_h - reset_size.y - UiSpace(3);
        const float row_w = opt_size.x + gap + reset_size.x + gap + sleep_size.x;
        const float opt_x = panel_x + (panel_w - row_w) * 0.5f;
        if (DrawButton(ui, opt_x, btn_y, "Options")) {
          draft_options = options;
          draft_mode = WindowModeIndex(options.window_mode);
          draft_res = VideoPresetIndex(options.width, options.height);
          pause_page = PausePage::Options;
        }
        if (DrawButton(ui, opt_x + opt_size.x + gap, btn_y, "Reset", KEY_R) && load_err.empty()) {
          SpawnPlayerAtSpawn(map, "start", player_state.position);
          player_state.velocity = {0.f, 0.f};
          player_state.was_moving = false;
          player_state.was_snapping = false;
        }
        if (DrawButton(ui, opt_x + opt_size.x + gap + reset_size.x + gap, btn_y, "Sleep")) {
          open_sleep(false);
        }
      } else {
        const float panel_w = 640.f;
        const float panel_h = 620.f;
        const float panel_x = (static_cast<float>(kUiLogicalW) - panel_w) * 0.5f;
        const float panel_y = (static_cast<float>(kUiLogicalH) - panel_h) * 0.5f;
        DrawPanel(ui, panel_x, panel_y, panel_w, panel_h);
        const char* heading = "Options";
        const float inner_x = panel_x + UiSpace(3);
        const float inner_w = panel_w - UiSpace(6);
        DrawLabel(ui, panel_x + (panel_w - UiMeasure(heading, title)) * 0.5f, panel_y + UiSpace(3),
                  heading, title, kUiTheme.text);
        DrawLabel(ui, inner_x, panel_y + UiSpace(8), "Window mode", body, kUiTheme.text_muted);
        const std::vector<std::string> mode_labels = {"Windowed", "Borderless", "Fullscreen"};
        const int mode_picked =
            DrawChoiceList(ui, inner_x, panel_y + UiSpace(11), inner_w, mode_labels, draft_mode, false);
        if (mode_picked >= 0) {
          draft_mode = mode_picked;
          draft_options.window_mode = WindowModeFromIndex(draft_mode);
        }
        float res_y = panel_y + UiSpace(11) + UiChoiceListHeight(3) + UiSpace(2);
        DrawLabel(ui, inner_x, res_y, "Resolution", body, kUiTheme.text_muted);
        res_y += line;
        std::vector<std::string> res_labels;
        res_labels.reserve(std::size(kVideoPresets));
        for (const VideoPreset& preset : kVideoPresets) {
          res_labels.push_back(TextFormat("%d x %d", preset.width, preset.height));
        }
        const int res_picked =
            DrawChoiceList(ui, inner_x, res_y, inner_w, res_labels, draft_res, false);
        if (res_picked >= 0) {
          draft_res = res_picked;
          draft_options.width = kVideoPresets[draft_res].width;
          draft_options.height = kVideoPresets[draft_res].height;
        }
        const float vsync_y = res_y + UiChoiceListHeight(static_cast<int>(std::size(kVideoPresets))) +
                              UiSpace(2);
        draft_options.vsync = DrawCheckbox(ui, inner_x, vsync_y, "Vsync", draft_options.vsync);
        const Vector2 apply_size = UiButtonSize("Apply");
        const Vector2 back_size = UiButtonSize("Back");
        const float gap = UiSpace(2);
        const float btn_y = panel_y + panel_h - apply_size.y - UiSpace(3);
        const float pair_w = apply_size.x + gap + back_size.x;
        const float apply_x = panel_x + (panel_w - pair_w) * 0.5f;
        if (DrawButton(ui, apply_x, btn_y, "Apply", KEY_ENTER)) {
          if (draft_res >= 0) {
            draft_options.width = kVideoPresets[draft_res].width;
            draft_options.height = kVideoPresets[draft_res].height;
          }
          options = SanitizeGameOptions(draft_options);
          ApplyGameOptions(options);
          SaveGameOptions(options);
        }
        if (DrawButton(ui, apply_x + apply_size.x + gap, btn_y, "Back")) {
          pause_page = PausePage::Root;
        }
      }
    }

    EndDrawing();
  }

  UnloadMapVisuals(map_visuals);
  UnloadGroundDrawResources(ground);
  CloseAudioDevice();
  CloseWindow();
  return 0;
}
