#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <vector>

#include <raylib.h>
#include <raymath.h>

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
float g_camYawDeg = 42.f;

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
  int ix = 0;
  int iy = 0;
  PlayerToCell(p, m, ix, iy);
  return m.IsWall(ix, iy);
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
  constexpr float k_snap_strength = 18.f;

  Camera3D camera{};
  camera.fovy = 50.f;
  camera.projection = CAMERA_PERSPECTIVE;

  int cam_zoom_target = 1;

  while (!WindowShouldClose()) {
    const float dt = GetFrameTime();

    if (IsKeyPressed(KEY_F1)) {
      if (map_fade == MapFade::Idle) {
        const int n = static_cast<int>(std::size(kMapCatalog));
        pending_map_index = (map_index - 1 + n) % n;
        map_fade = MapFade::Out;
        map_fade_t = 0.f;
      }
    }
    if (IsKeyPressed(KEY_F2)) {
      if (map_fade == MapFade::Idle) {
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

    const Vector2 player_prev = player;

    if (load_err.empty() && map_fade == MapFade::Idle) {
      Vector2 input{0.f, 0.f};
      if (IsKeyDown(KEY_W)) input.y -= 1.f;
      if (IsKeyDown(KEY_S)) input.y += 1.f;
      if (IsKeyDown(KEY_A)) input.x -= 1.f;
      if (IsKeyDown(KEY_D)) input.x += 1.f;

      const bool moving = (input.x != 0.f || input.y != 0.f);
      if (moving) {
        if (input.x != 0.f && input.y != 0.f) {
          const float inv = 0.70710678f;
          input.x *= inv;
          input.y *= inv;
        }
        player.x += input.x * k_move_speed * dt;
        player.y += input.y * k_move_speed * dt;
      } else {
        Vector2 target = NearestTileCenter(player);
        ClampPlayerToMapBounds(target, map);
        if (!IsWallAtPlayer(target, map)) {
          const float k = 1.f - std::exp(-k_snap_strength * dt);
          player.x += (target.x - player.x) * k;
          player.y += (target.y - player.y) * k;
          if (std::fabs(target.x - player.x) < 0.001f) player.x = target.x;
          if (std::fabs(target.y - player.y) < 0.001f) player.y = target.y;
        }
      }

      ClampPlayerToMapBounds(player, map);
      if (IsWallAtPlayer(player, map)) {
        player = player_prev;
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

    EndDrawing();
  }

  UnloadGroundDrawResources(ground);
  UnloadRenderTexture(world_target);
  CloseWindow();
  return 0;
}
