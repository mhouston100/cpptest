#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

#include <raylib.h>
#include <raymath.h>
#include <nlohmann/json.hpp>

#include "game_map.hpp"
#include "ldtk_load.hpp"

namespace {

constexpr int kWorldBufferW = 640;
constexpr int kWorldBufferH = 360;

constexpr float kUiRefScreenW = 960.f;
constexpr float kUiRefScreenH = 540.f;

float UiScale() {
  const float sx = static_cast<float>(GetScreenWidth()) / kUiRefScreenW;
  const float sy = static_cast<float>(GetScreenHeight()) / kUiRefScreenH;
  return std::clamp(std::min(sx, sy), 0.5f, 3.f);
}

int UiPx(const float logical_pixels) {
  return static_cast<int>(std::lround(logical_pixels * UiScale()));
}

float g_tileWorld = 1.f;

constexpr float kCamDistLevels[4] = {6.5f, 9.5f, 13.f, 18.f};
constexpr float kCamZoomSmooth = 20.f;
float g_camDist = kCamDistLevels[1];
float g_camPitchDeg = 50.f;
// 45 deg keeps the tile grid axes at screen diagonals, so screen-relative
// diagonal input (e.g. W+D) travels straight along a tile axis.
float g_camYawDeg = 45.f;

// Player position: map-centered tile coords (origin at map center). (0,0) = center of map.
// Cell (ix, iy) LDtk top-left has center (ix + 0.5 - W/2, iy + 0.5 - H/2).
constexpr float kPlayerFootprintHalfTiles = 0.35f;
constexpr float kGridInnerMarginTiles = 0.5f;

Vector2 NearestTileCenter(Vector2 p) {
  return {std::round(p.x - 0.5f) + 0.5f, std::round(p.y - 0.5f) + 0.5f};
}

void ClampPlayerToMapBounds(Vector2& p, const GameMap& m) {
  const float inset = kGridInnerMarginTiles + kPlayerFootprintHalfTiles;
  const float hx = static_cast<float>(m.c_wid) * 0.5f;
  const float hz = static_cast<float>(m.c_hei) * 0.5f;
  p.x = std::clamp(p.x, -hx + 0.5f + inset, hx - 0.5f - inset);
  p.y = std::clamp(p.y, -hz + 0.5f + inset, hz - 0.5f - inset);
}

// LDtk cell index under player feet (center coords).
void PlayerToCell(const Vector2& p, const GameMap& m, int& ix, int& iy) {
  ix = static_cast<int>(std::floor(p.x + static_cast<float>(m.c_wid) * 0.5f - 0.5f + 1e-4f));
  iy = static_cast<int>(std::floor(p.y + static_cast<float>(m.c_hei) * 0.5f - 0.5f + 1e-4f));
  ix = std::clamp(ix, 0, m.c_wid - 1);
  iy = std::clamp(iy, 0, m.c_hei - 1);
}

bool IsWallAtPlayer(const Vector2& p, const GameMap& m) {
  int cx = 0, cy = 0;
  PlayerToCell(p, m, cx, cy);

  const float half_cell = 0.5f;
  const float overlap = half_cell + kPlayerFootprintHalfTiles;

  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      int nx = cx + dx;
      int ny = cy + dy;
      if (!m.InBounds(nx, ny) || !m.IsWall(nx, ny)) {
        continue;
      }

      const float wall_x = static_cast<float>(nx) + 0.5f - static_cast<float>(m.c_wid) * 0.5f;
      const float wall_y = static_cast<float>(ny) + 0.5f - static_cast<float>(m.c_hei) * 0.5f;
      if (std::fabs(p.x - wall_x) <= overlap && std::fabs(p.y - wall_y) <= overlap) {
        return true;
      }
    }
  }

  return false;
}

struct DialogChoice {
  std::string label;
  int next_step = -1;
};

struct DialogStep {
  std::string text;
  std::vector<DialogChoice> choices;
};

struct DialogTree {
  std::vector<DialogStep> steps;
  int current_step = 0;
};

bool GetAdjacentInteractable(const Vector2& player, const GameMap& m, int& out_item,
                             std::string& out_name, std::string& out_type) {
  int cx = 0, cy = 0;
  PlayerToCell(player, m, cx, cy);
  // Scan all four primary directions; interact with the first neighbor that
  // holds an interactable, independent of which way the player last moved.
  constexpr int kDirs[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
  int tx = 0, ty = 0;
  bool found = false;
  for (const auto& d : kDirs) {
    const int nx = cx + d[0];
    const int ny = cy + d[1];
    if (m.InBounds(nx, ny) && m.HasInteractable(nx, ny)) {
      tx = nx;
      ty = ny;
      found = true;
      break;
    }
  }
  if (!found) {
    return false;
  }
  out_item = m.Interactable(tx, ty);
  out_name = m.InteractableName(tx, ty);
  out_type = m.InteractableType(tx, ty);
  return true;
}

std::string ReplaceDialogTokens(std::string text, const std::string& instance_name,
                                const std::string& type_name) {
  auto replace_all = [&](const std::string& token, const std::string& value) {
    size_t pos = 0;
    while ((pos = text.find(token, pos)) != std::string::npos) {
      text.replace(pos, token.size(), value);
      pos += value.size();
    }
  };
  replace_all("${instance}", instance_name);
  replace_all("${type}", type_name);
  replace_all("${name}", instance_name);
  return text;
}

bool LoadDialogTreeFromJson(const nlohmann::json& root, DialogTree& out, const std::string& source_name) {
  if (!root.contains("steps") || !root["steps"].is_array()) {
    TraceLog(LOG_WARNING, "Dialog file %s missing steps array", source_name.c_str());
    return false;
  }

  out.steps.clear();
  for (const auto& step_json : root["steps"]) {
    if (!step_json.is_object()) {
      TraceLog(LOG_WARNING, "Dialog step in %s is invalid", source_name.c_str());
      return false;
    }

    if (!step_json.contains("text") || !step_json["text"].is_string()) {
      TraceLog(LOG_WARNING, "Dialog step in %s missing text", source_name.c_str());
      return false;
    }

    DialogStep step;
    step.text = step_json["text"].get<std::string>();

    if (step_json.contains("choices")) {
      if (!step_json["choices"].is_array()) {
        TraceLog(LOG_WARNING, "Choices array in %s is invalid", source_name.c_str());
        return false;
      }
      for (const auto& choice_json : step_json["choices"]) {
        if (!choice_json.is_object() || !choice_json.contains("label") ||
            !choice_json["label"].is_string()) {
          TraceLog(LOG_WARNING, "Dialog choice in %s is invalid", source_name.c_str());
          return false;
        }
        DialogChoice choice;
        choice.label = choice_json["label"].get<std::string>();
        choice.next_step = choice_json.value("next_step", -1);
        step.choices.push_back(std::move(choice));
      }
    }

    out.steps.push_back(std::move(step));
  }

  out.current_step = 0;
  return !out.steps.empty();
}

static std::unordered_map<std::string, DialogTree> LoadDialogRegistry(const std::string& dialog_dir) {
  std::unordered_map<std::string, DialogTree> registry;
  std::filesystem::path dir_path(dialog_dir);

  if (!std::filesystem::exists(dir_path) || !std::filesystem::is_directory(dir_path)) {
    const std::filesystem::path alternate_path = dir_path.parent_path() / dir_path.filename();
    if (std::filesystem::exists(alternate_path) && std::filesystem::is_directory(alternate_path)) {
      dir_path = alternate_path;
    } else {
      TraceLog(LOG_WARNING, "Dialog directory not found: %s", dialog_dir.c_str());
      return registry;
    }
  }

  for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".json") {
      continue;
    }

    std::ifstream in(entry.path());
    if (!in) {
      TraceLog(LOG_WARNING, "Could not open dialog file: %s", entry.path().c_str());
      continue;
    }

    nlohmann::json root;
    try {
      in >> root;
    } catch (const std::exception& e) {
      TraceLog(LOG_WARNING, "JSON parse error in %s: %s", entry.path().c_str(), e.what());
      continue;
    }

    const std::string type_name = entry.path().stem().string();
    // If the root is a dialog object (contains "steps"), treat it as the type default.
    if (root.is_object() && root.contains("steps")) {
      DialogTree tree;
      if (!LoadDialogTreeFromJson(root, tree, entry.path().string())) {
        continue;
      }
      registry[type_name] = std::move(tree);
      TraceLog(LOG_INFO, "Loaded dialog for type '%s' from %s", type_name.c_str(), entry.path().c_str());
    } else if (root.is_object()) {
      // Otherwise expect a mapping of instance keys to dialog objects, optionally a "default" key.
      for (auto it = root.begin(); it != root.end(); ++it) {
        const std::string key = it.key();
        const nlohmann::json& val = it.value();
        DialogTree tree;
        if (!LoadDialogTreeFromJson(val, tree, entry.path().string() + ":" + key)) {
          TraceLog(LOG_WARNING, "Skipping dialog entry %s in %s", key.c_str(), entry.path().c_str());
          continue;
        }
        registry[key] = tree;
        TraceLog(LOG_INFO, "Loaded dialog key '%s' from %s", key.c_str(), entry.path().c_str());
        if (key == "default" || key == type_name) {
          registry[type_name] = tree;
          TraceLog(LOG_INFO, "Registered type fallback '%s' from %s", type_name.c_str(), entry.path().c_str());
        }
      }
    } else {
      TraceLog(LOG_WARNING, "Dialog file %s has unexpected root type", entry.path().c_str());
    }
  }

  return registry;
}

DialogTree MakeFallbackDialogTree(const std::string& instance_name, const std::string& type_name) {
  return DialogTree{{
      {type_name + " " + instance_name + ": There's nothing special here.", {}},
      {"Press ESC to close.", {}},
  }, 0};
}

DialogTree MakeDialogTreeForInstance(const std::string& instance_name, const std::string& type_name,
                                     const std::unordered_map<std::string, DialogTree>& registry) {
  auto it_inst = registry.find(instance_name);
  if (it_inst != registry.end()) {
    TraceLog(LOG_INFO, "Dialog lookup: matched instance '%s'", instance_name.c_str());
    DialogTree dialog = it_inst->second;
    for (auto& step : dialog.steps) {
      step.text = ReplaceDialogTokens(step.text, instance_name, type_name);
    }
    return dialog;
  }
  auto it_type = registry.find(type_name);
  if (it_type != registry.end()) {
    TraceLog(LOG_INFO, "Dialog lookup: matched type '%s' for instance '%s'", type_name.c_str(), instance_name.c_str());
    DialogTree dialog = it_type->second;
    for (auto& step : dialog.steps) {
      step.text = ReplaceDialogTokens(step.text, instance_name, type_name);
    }
    return dialog;
  }

  TraceLog(LOG_INFO, "Dialog lookup: no match for instance '%s' (type '%s'), using fallback", instance_name.c_str(), type_name.c_str());
  DialogTree dialog = MakeFallbackDialogTree(instance_name, type_name);
  for (auto& step : dialog.steps) {
    step.text = ReplaceDialogTokens(step.text, instance_name, type_name);
  }
  return dialog;
}

Vector3 TileToWorldCenter(const float tx, const float tz) {
  return {tx * g_tileWorld, 0.f, tz * g_tileWorld};
}

void SpawnPlayerAtFirstWalkable(const GameMap& m, Vector2& out) {
  for (int iy = 0; iy < m.c_hei; ++iy) {
    for (int ix = 0; ix < m.c_wid; ++ix) {
      if (!m.IsWall(ix, iy)) {
        out.x = static_cast<float>(ix) + 0.5f - static_cast<float>(m.c_wid) * 0.5f;
        out.y = static_cast<float>(iy) + 0.5f - static_cast<float>(m.c_hei) * 0.5f;
        return;
      }
    }
  }
  out = {0.f, 0.f};
}

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

void DrawWorldGridForMap(const GameMap& m, const Color line_color) {
  const float tw = g_tileWorld;
  const float hx = static_cast<float>(m.c_wid) * 0.5f;
  const float hz = static_cast<float>(m.c_hei) * 0.5f;
  constexpr float y = 0.001f;
  for (int ix = 0; ix <= m.c_wid; ++ix) {
    const float x = (static_cast<float>(ix) - hx) * tw;
    DrawLine3D({x, y, -hz * tw}, {x, y, hz * tw}, line_color);
  }
  for (int iy = 0; iy <= m.c_hei; ++iy) {
    const float z = (static_cast<float>(iy) - hz) * tw;
    DrawLine3D({-hx * tw, y, z}, {hx * tw, y, z}, line_color);
  }
}

void DrawWallCells(const GameMap& m) {
  const float tw = g_tileWorld;
  const float hx = static_cast<float>(m.c_wid) * 0.5f;
  const float hz = static_cast<float>(m.c_hei) * 0.5f;
  const float wall_h = tw * 0.55f;
  const float wall_w = tw * 0.92f;
  const Color fill{48, 52, 68, 255};
  const Color outline{22, 24, 32, 255};
  for (int iy = 0; iy < m.c_hei; ++iy) {
    for (int ix = 0; ix < m.c_wid; ++ix) {
      if (!m.IsWall(ix, iy)) {
        continue;
      }
      const float wx = (static_cast<float>(ix) + 0.5f - hx) * tw;
      const float wz = (static_cast<float>(iy) + 0.5f - hz) * tw;
      const Vector3 c{wx, wall_h * 0.5f + 0.01f, wz};
      DrawCube(c, wall_w, wall_h, wall_w, fill);
      DrawCubeWires(c, wall_w, wall_h, wall_w, outline);
    }
  }
}

void DrawInteractables(const GameMap& m) {
  const float tw = g_tileWorld;
  const float hx = static_cast<float>(m.c_wid) * 0.5f;
  const float hz = static_cast<float>(m.c_hei) * 0.5f;
  const float item_size = tw * 0.6f;
  const Color fill{244, 58, 58, 255};
  const Color outline{180, 40, 40, 255};
  for (int iy = 0; iy < m.c_hei; ++iy) {
    for (int ix = 0; ix < m.c_wid; ++ix) {
      if (!m.HasInteractable(ix, iy)) {
        continue;
      }
      const float ixw = (static_cast<float>(ix) + 0.5f - hx) * tw;
      const float iz = (static_cast<float>(iy) + 0.5f - hz) * tw;
      const Vector3 c{ixw, item_size * 0.5f + 0.01f, iz};
      DrawCube(c, item_size, item_size, item_size, fill);
      DrawCubeWires(c, item_size, item_size, item_size, outline);
    }
  }
}

void DrawPlayerBlock(const Vector3& base_center, const Color fill, const Color outline) {
  const float s = g_tileWorld * kPlayerFootprintHalfTiles;
  const float h = g_tileWorld * 0.85f;
  const Vector3 c{base_center.x, base_center.y + h * 0.5f, base_center.z};
  DrawCube(c, s * 2.f, h, s * 2.f, fill);
  DrawCubeWires(c, s * 2.f, h, s * 2.f, outline);
}

void DrawWorldBufferToScreen(const RenderTexture2D& world_rt) {
  const Rectangle dest{0.f, 0.f, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())};
  const Rectangle src{0.f, 0.f, static_cast<float>(world_rt.texture.width),
                      -static_cast<float>(world_rt.texture.height)};
  DrawTexturePro(world_rt.texture, src, dest, {0.f, 0.f}, 0.f, WHITE);
}

constexpr const char* kGroundVs = R"(
#version 330
in vec3 vertexPosition;
uniform mat4 matProjection;
uniform mat4 matView;
uniform mat4 matModel;
out vec3 fragWorldPos;

void main() {
  fragWorldPos = vec3(matModel * vec4(vertexPosition, 1.0));
  gl_Position = matProjection * matView * matModel * vec4(vertexPosition, 1.0);
}
)";

constexpr const char* kGroundFs = R"(
#version 330
in vec3 fragWorldPos;
uniform vec4 mapEdge;
out vec4 finalColor;

void main() {
  float L = mapEdge.x;
  float fade = mapEdge.y;
  float d = max(abs(fragWorldPos.x), abs(fragWorldPos.z));
  float t = smoothstep(L, L + fade, d);
  vec3 base = vec3(0.12549019607843137, 0.14901960784313725, 0.18823529411764706);
  finalColor = vec4(mix(base, vec3(0.0), t), 1.0);
}
)";

struct GroundDrawResources {
  Model model{};
  Shader shader{};
  int loc_map_edge = -1;
  bool ok = false;
};

GroundDrawResources LoadGroundDrawResources() {
  GroundDrawResources r{};
  r.shader = LoadShaderFromMemory(kGroundVs, kGroundFs);
  if (!IsShaderValid(r.shader)) {
    return r;
  }
  r.loc_map_edge = GetShaderLocation(r.shader, "mapEdge");
  if (r.loc_map_edge < 0) {
    UnloadShader(r.shader);
    r.shader = {};
    return r;
  }
  Mesh mesh = GenMeshPlane(1.f, 1.f, 1, 1);
  r.model = LoadModelFromMesh(mesh);
  r.model.materials[0].shader = r.shader;
  r.ok = true;
  return r;
}

void UnloadGroundDrawResources(GroundDrawResources& r) {
  if (r.ok) {
    UnloadModel(r.model);
    UnloadShader(r.shader);
    r.ok = false;
  }
}

void DrawGroundWithMapEdge(const GroundDrawResources& res, const GameMap& m, const float tile_world) {
  if (m.c_wid <= 0 || m.c_hei <= 0) {
    DrawPlane({0.f, 0.f, 0.f}, {40.f, 40.f}, Color{32, 38, 48, 255});
    return;
  }
  const float max_cells = static_cast<float>(std::max(m.c_wid, m.c_hei));
  const float plane_size = (max_cells + 4.f) * tile_world * 2.f;
  const float L = max_cells * 0.5f * tile_world;
  constexpr float k_fade_span_tiles = 2.75f;
  const float fade_w = std::max(0.02f, k_fade_span_tiles * tile_world);
  const float map_edge[4] = {L, fade_w, 0.f, 0.f};

  if (!res.ok) {
    DrawPlane({0.f, 0.f, 0.f}, {plane_size, plane_size}, Color{32, 38, 48, 255});
    return;
  }
  SetShaderValue(res.shader, res.loc_map_edge, map_edge, SHADER_UNIFORM_VEC4);
  DrawModelEx(res.model, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, 0.f, {plane_size, 1.f, plane_size}, WHITE);
}

struct MapCatalogEntry {
  const char* path;
  int level_index = 0;
};

constexpr MapCatalogEntry kMapCatalog[] = {
    {"map/cpptest.ldtk", 0},
    {"map/floor_1.ldtk", 0},
};

enum class MapFade { Idle, Out, In };

}  // namespace

int main() {
  constexpr int k_window_w = 960;
  constexpr int k_window_h = 540;

  InitWindow(k_window_w, k_window_h, "cpptest — maps");
  SetTargetFPS(60);
  SetTraceLogLevel(LOG_INFO);

  RenderTexture2D world_target = LoadRenderTexture(kWorldBufferW, kWorldBufferH);
  SetTextureFilter(world_target.texture, TEXTURE_FILTER_POINT);

  GroundDrawResources ground = LoadGroundDrawResources();

  GameMap map;
  std::string load_err = LoadLdtkLevel(kMapCatalog[0].path, kMapCatalog[0].level_index, map);
  if (!load_err.empty()) {
    TraceLog(LOG_ERROR, "Map load: %s", load_err.c_str());
  }

  Vector2 player{};
  if (load_err.empty()) {
    SpawnPlayerAtFirstWalkable(map, player);
  }

  int map_index = 0;
  MapFade map_fade = MapFade::Idle;
  float map_fade_t = 0.f;
  int pending_map_index = -1;
  constexpr float k_map_fade_sec = 0.42f;

  constexpr float k_move_speed = 5.f;
  constexpr float k_move_accel = 16.f;
  constexpr float k_snap_strength = 18.f;
  bool was_moving = false;
  bool was_snapping = false;
  Vector2 player_vel{0.f, 0.f};
  bool interaction_dialog = false;
  std::string active_interactable_name;
  std::string active_interactable_type;
  DialogTree active_dialog;
  const auto dialog_registry = LoadDialogRegistry("dialogs");

  Camera3D camera{};
  camera.fovy = 50.f;
  camera.projection = CAMERA_PERSPECTIVE;

  int cam_zoom_target = 1;

  while (!WindowShouldClose()) {
    const float dt = GetFrameTime();

    if (IsKeyPressed(KEY_F1)) {
      TraceLog(LOG_INFO, "Key pressed: F1");
      if (map_fade == MapFade::Idle) {
        const int n = static_cast<int>(std::size(kMapCatalog));
        pending_map_index = (map_index - 1 + n) % n;
        map_fade = MapFade::Out;
        map_fade_t = 0.f;
        TraceLog(LOG_INFO, "Map fade out started: prev map %d", pending_map_index + 1);
      }
    }
    if (IsKeyPressed(KEY_F2)) {
      TraceLog(LOG_INFO, "Key pressed: F2");
      if (map_fade == MapFade::Idle) {
        const int n = static_cast<int>(std::size(kMapCatalog));
        pending_map_index = (map_index + 1) % n;
        map_fade = MapFade::Out;
        map_fade_t = 0.f;
        TraceLog(LOG_INFO, "Map fade out started: next map %d", pending_map_index + 1);
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
          SpawnPlayerAtFirstWalkable(map, player);
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

    if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) {
      TraceLog(LOG_INFO, "Key pressed: - / KP_SUBTRACT");
      cam_zoom_target = std::max(0, cam_zoom_target - 1);
      TraceLog(LOG_INFO, "Zoom preset changed to %d", cam_zoom_target + 1);
    }
    if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) {
      TraceLog(LOG_INFO, "Key pressed: = / KP_ADD");
      cam_zoom_target = std::min(3, cam_zoom_target + 1);
      TraceLog(LOG_INFO, "Zoom preset changed to %d", cam_zoom_target + 1);
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
      TraceLog(LOG_INFO, "Key down: [  tileWorld=%.2f", g_tileWorld);
    }
    if (IsKeyDown(KEY_RIGHT_BRACKET)) {
      g_tileWorld += 1.2f * dt;
      TraceLog(LOG_INFO, "Key down: ]  tileWorld=%.2f", g_tileWorld);
    }
    if (IsKeyDown(KEY_SEMICOLON)) {
      g_camPitchDeg = std::clamp(g_camPitchDeg - 35.f * dt, 28.f, 88.f);
      TraceLog(LOG_INFO, "Key down: ;  camPitch=%.1f", g_camPitchDeg);
    }
    if (IsKeyDown(KEY_APOSTROPHE)) {
      g_camPitchDeg = std::clamp(g_camPitchDeg + 35.f * dt, 28.f, 88.f);
      TraceLog(LOG_INFO, "Key down: '  camPitch=%.1f", g_camPitchDeg);
    }
    if (IsKeyDown(KEY_COMMA)) {
      g_camYawDeg -= 55.f * dt;
      TraceLog(LOG_INFO, "Key down: ,  camYaw=%.1f", g_camYawDeg);
    }
    if (IsKeyDown(KEY_PERIOD)) {
      g_camYawDeg += 55.f * dt;
      TraceLog(LOG_INFO, "Key down: .  camYaw=%.1f", g_camYawDeg);
    }

    const Vector2 player_prev = player;
    int adjacent_item = 0;
    bool has_adjacent_interactable = false;
    std::string adjacent_name;
    std::string adjacent_type;

    if (load_err.empty() && map_fade == MapFade::Idle) {
      // Raw key input in screen space: x = right, y = up (+up).
      Vector2 input{0.f, 0.f};
      if (IsKeyDown(KEY_W)) input.y += 1.f;
      if (IsKeyDown(KEY_S)) input.y -= 1.f;
      if (IsKeyDown(KEY_A)) input.x -= 1.f;
      if (IsKeyDown(KEY_D)) input.x += 1.f;

      // Rotate screen-space input into world/grid space using the camera yaw so
      // "up" on screen always moves the player away from the camera regardless
      // of how the view is rotated. Player coords map to (worldX, worldZ); the
      // ground basis derived from the camera is:
      //   screen-up    -> (-sin yaw, -cos yaw)
      //   screen-right -> ( cos yaw, -sin yaw)
      const float move_yaw = g_camYawDeg * DEG2RAD;
      const float move_sy = std::sin(move_yaw);
      const float move_cy = std::cos(move_yaw);
      Vector2 move{
          input.y * (-move_sy) + input.x * (move_cy),
          input.y * (-move_cy) + input.x * (-move_sy),
      };

      const bool moving = (input.x != 0.f || input.y != 0.f);
      Vector2 target = NearestTileCenter(player);
      ClampPlayerToMapBounds(target, map);
      const bool target_diff = std::fabs(target.x - player.x) > 0.001f || std::fabs(target.y - player.y) > 0.001f;

      if (moving) {
        if (!was_moving) {
          TraceLog(LOG_INFO, "Movement started: input=(%.2f, %.2f)", input.x, input.y);
        }
        // Normalize so all 8 directions share the same speed.
        const float move_len = std::sqrt(move.x * move.x + move.y * move.y);
        if (move_len > 1e-5f) {
          move.x /= move_len;
          move.y /= move_len;
        }
        // Ease velocity toward the desired direction for a smoother start.
        const Vector2 desired{move.x * k_move_speed, move.y * k_move_speed};
        const float accel_k = 1.f - std::exp(-k_move_accel * dt);
        player_vel.x += (desired.x - player_vel.x) * accel_k;
        player_vel.y += (desired.y - player_vel.y) * accel_k;
        player.x += player_vel.x * dt;
        player.y += player_vel.y * dt;
        was_snapping = false;
      } else {
        player_vel = {0.f, 0.f};
        if (was_moving) {
          TraceLog(LOG_INFO, "Movement stopped");
        }
        const bool will_snap = target_diff && !IsWallAtPlayer(target, map);
        if (will_snap) {
          if (!was_snapping) {
            TraceLog(LOG_INFO, "Snapping to tile center: target=(%.2f, %.2f)", target.x, target.y);
          }
          const float k = 1.f - std::exp(-k_snap_strength * dt);
          player.x += (target.x - player.x) * k;
          player.y += (target.y - player.y) * k;
          if (std::fabs(target.x - player.x) < 0.001f) player.x = target.x;
          if (std::fabs(target.y - player.y) < 0.001f) player.y = target.y;
        } else if (was_snapping) {
          TraceLog(LOG_INFO, "Snapping finished: at tile center=(%.2f, %.2f)", target.x, target.y);
        }
        was_snapping = will_snap;
      }

      const bool did_change = moving || was_snapping;
      was_moving = moving;
      if (did_change) {
        ClampPlayerToMapBounds(player, map);
        if (IsWallAtPlayer(player, map)) {
          TraceLog(LOG_INFO, "Wall collision detected; reverting player position");
          player = player_prev;
        }
      }

      has_adjacent_interactable = GetAdjacentInteractable(player, map, adjacent_item,
                                                          adjacent_name, adjacent_type);
      if (IsKeyPressed(KEY_E) && has_adjacent_interactable) {
        interaction_dialog = true;
        active_interactable_name = adjacent_name;
        active_interactable_type = adjacent_type;
        active_dialog = MakeDialogTreeForInstance(active_interactable_name, active_interactable_type,
                                                 dialog_registry);
        active_dialog.current_step = 0;
        TraceLog(LOG_INFO, "Interaction started with %s (%s)", active_interactable_name.c_str(),
                 active_interactable_type.c_str());
      }
      if (interaction_dialog) {
        if (active_dialog.steps.empty()) {
          interaction_dialog = false;
        } else {
          const auto& step = active_dialog.steps[active_dialog.current_step];
          if (!step.choices.empty()) {
            for (int i = 0; i < static_cast<int>(step.choices.size()); ++i) {
              if (IsKeyPressed(KEY_ONE + i)) {
                const int next_step = step.choices[i].next_step;
                if (next_step >= 0 && next_step < static_cast<int>(active_dialog.steps.size())) {
                  active_dialog.current_step = next_step;
                } else {
                  interaction_dialog = false;
                }
              }
            }
          } else if (IsKeyPressed(KEY_ENTER)) {
            if (active_dialog.current_step < static_cast<int>(active_dialog.steps.size()) - 1) {
              active_dialog.current_step += 1;
            } else {
              interaction_dialog = false;
            }
          }
          if (IsKeyPressed(KEY_ESCAPE)) {
            interaction_dialog = false;
          }
        }
      }
    }

    Vector3 focus = TileToWorldCenter(player.x, player.y);
    focus.y += g_tileWorld * 0.35f;
    UpdateDiabloStyleCamera(camera, focus);

    BeginTextureMode(world_target);
    ClearBackground(Color{12, 14, 20, 255});

    BeginMode3D(camera);

    if (load_err.empty()) {
      DrawGroundWithMapEdge(ground, map, g_tileWorld);
      DrawWorldGridForMap(map, Color{72, 86, 104, 255});
      DrawWallCells(map);
      DrawInteractables(map);
    }

    const Vector3 feet = TileToWorldCenter(player.x, player.y);
    DrawPlayerBlock(feet, Color{210, 115, 70, 255}, Color{35, 18, 10, 255});

    EndMode3D();

    EndTextureMode();

    BeginDrawing();
    ClearBackground(Color{8, 9, 12, 255});

    DrawWorldBufferToScreen(world_target);

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

    const int m = UiPx(12.f);
    const int fs = UiPx(18.f);
    const int lh = UiPx(22.f);
    DrawText("WASD move   F1/F2 prev/next map (fade)   [ / ] tile size   - / + zoom", m, m, fs, RAYWHITE);
    DrawText(TextFormat("Map %d/%d  %s  level:%s", map_index + 1, static_cast<int>(std::size(kMapCatalog)),
                        kMapCatalog[map_index].path, map.level_identifier.c_str()),
             m, m + lh, fs, Color{200, 200, 200, 255});
    if (!load_err.empty()) {
      DrawText(load_err.c_str(), m, m + lh * 2, fs, Color{255, 120, 120, 255});
    } else {
      DrawText(TextFormat("%dx%d cells  grid_px=%d  tileWorld=%.2f", map.c_wid, map.c_hei, map.grid_px,
                          static_cast<double>(g_tileWorld)),
               m, m + lh * 2, fs, Color{170, 210, 170, 255});
    }
    DrawText(TextFormat("preset %d/4  dist %.1f  pitch=%.0f yaw=%.0f", cam_zoom_target + 1,
                        static_cast<double>(g_camDist), static_cast<double>(g_camPitchDeg),
                        static_cast<double>(g_camYawDeg)),
             m, m + lh * 3, fs, Color{160, 200, 230, 255});

    if (interaction_dialog) {
      if (!active_dialog.steps.empty()) {
        const int box_w = GetScreenWidth() - m * 2;
        const int box_h = UiPx(120.f);
        const int box_x = m;
        const int box_y = GetScreenHeight() - box_h - m;
        DrawRectangle(box_x, box_y, box_w, box_h, Color{12, 14, 20, 215});
        DrawRectangleLines(box_x, box_y, box_w, box_h, Color{190, 190, 240, 255});
        const auto& step = active_dialog.steps[active_dialog.current_step];
        DrawText(TextFormat("%s", step.text.c_str()), box_x + m, box_y + m, fs, RAYWHITE);
        if (step.choices.empty()) {
          DrawText("Press ENTER to continue or ESC to close", box_x + m, box_y + box_h - lh, fs,
                   Color{160, 160, 180, 255});
        } else {
          int choice_y = box_y + m + lh * 2;
          for (int i = 0; i < static_cast<int>(step.choices.size()); ++i) {
            DrawText(TextFormat("%d: %s", i + 1, step.choices[i].label.c_str()), box_x + m, choice_y, fs,
                     Color{200, 220, 255, 255});
            choice_y += lh;
          }
          DrawText("Press number to choose or ESC to close", box_x + m, box_y + box_h - lh, fs,
                   Color{160, 160, 180, 255});
        }
      } else {
        interaction_dialog = false;
      }
    } else if (has_adjacent_interactable) {
      DrawText("Press E to interact", m, GetScreenHeight() - lh - m, fs, Color{220, 220, 120, 255});
    }

    EndDrawing();
  }

  UnloadGroundDrawResources(ground);
  UnloadRenderTexture(world_target);
  CloseWindow();
  return 0;
}
