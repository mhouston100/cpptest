#include <algorithm>
#include <cmath>

#include <raylib.h>
#include <raymath.h>

namespace {

// --- World / sprite layer: fixed internal resolution, stretched to fill the window (nearest filter).
constexpr int kWorldBufferW = 640;
constexpr int kWorldBufferH = 360;

// --- UI layer: independent coordinate system in **window pixels** (not world-buffer pixels).
// Scale HUD from a reference window so text stays readable on large displays.
constexpr float kUiRefScreenW = 960.f;
constexpr float kUiRefScreenH = 540.f;

float UiScale() {
  const float sx = static_cast<float>(GetScreenWidth()) / kUiRefScreenW;
  const float sy = static_cast<float>(GetScreenHeight()) / kUiRefScreenH;
  return std::clamp(std::min(sx, sy), 0.5f, 3.f);
}

int UiPx(float logicalPixels) {
  return static_cast<int>(std::lround(logicalPixels * UiScale()));
}

// World meters per grid tile — tweak with [ / ].
float g_tileWorld = 1.f;

// Four fixed orbit distances (index 0 = closest, 3 = farthest). g_camDist eases toward the active preset.
constexpr float kCamDistLevels[4] = {6.5f, 9.5f, 13.f, 18.f};
constexpr float kCamZoomSmooth = 20.f;  // larger = snappier approach to preset
float g_camDist = kCamDistLevels[1];
float g_camPitchDeg = 50.f;
float g_camYawDeg = 42.f;

Vector3 TileToWorldCenter(float tx, float tz) {
  return {tx * g_tileWorld, 0.f, tz * g_tileWorld};
}

// Horizontal half-extent of the player cube in **tile units** (must match DrawPlayerBlock).
constexpr float kPlayerFootprintHalfTiles = 0.35f;
// Extra inset from the outer grid line so the model stays visibly inside (tile units → world × tileWorld).
constexpr float kGridInnerMarginTiles = 0.5f;

Vector2 NearestTileCenter(Vector2 p) {
  return {std::round(p.x - 0.5f) + 0.5f, std::round(p.y - 0.5f) + 0.5f};
}

// Clamp feet (tile coords → world XZ) so the player cube stays inside the grid: outer faces at most
// (L - margin) from centerline, with margin = kGridInnerMarginTiles * tileWorld past the grid line.
void ClampPlayerToGrid(Vector2& p, int halfTiles, float tileWorld) {
  if (tileWorld <= 1e-6f) {
    return;
  }
  const float L = static_cast<float>(halfTiles) * tileWorld;
  const float halfW = kPlayerFootprintHalfTiles * tileWorld;
  const float marginW = kGridInnerMarginTiles * tileWorld;
  const float Lc = std::max(L - marginW - halfW, 1e-4f);
  float wx = p.x * tileWorld;
  float wz = p.y * tileWorld;
  wx = std::clamp(wx, -Lc, Lc);
  wz = std::clamp(wz, -Lc, Lc);
  p.x = wx / tileWorld;
  p.y = wz / tileWorld;
}

void UpdateDiabloStyleCamera(Camera3D& cam, Vector3 focus) {
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

void DrawWorldGrid(int halfTiles, Color lineColor) {
  const float t = g_tileWorld;
  const float L = static_cast<float>(halfTiles) * t;
  const float y = 0.001f;
  for (int i = -halfTiles; i <= halfTiles; ++i) {
    const float o = static_cast<float>(i) * t;
    DrawLine3D({-L, y, o}, {L, y, o}, lineColor);
    DrawLine3D({o, y, -L}, {o, y, L}, lineColor);
  }
}

void DrawPlayerBlock(Vector3 baseCenter, Color fill, Color outline) {
  const float s = g_tileWorld * kPlayerFootprintHalfTiles;
  const float h = g_tileWorld * 0.85f;
  const Vector3 c{baseCenter.x, baseCenter.y + h * 0.5f, baseCenter.z};
  DrawCube(c, s * 2.f, h, s * 2.f, fill);
  DrawCubeWires(c, s * 2.f, h, s * 2.f, outline);
}

// Full-window stretch of world RT (aspect ratio follows window).
void DrawWorldBufferToScreen(const RenderTexture2D& worldRt) {
  const Rectangle dest{0.f, 0.f, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())};
  const Rectangle src{0.f, 0.f, static_cast<float>(worldRt.texture.width),
                      -static_cast<float>(worldRt.texture.height)};
  DrawTexturePro(worldRt.texture, src, dest, {0.f, 0.f}, 0.f, WHITE);
}

// Ground shader: world-space blend to black outside the same square as the grid (max(|x|,|z|) <= L).
// Independent of camera/player — suitable for later replacing the square with building walls / interior.
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
  int locMapEdge = -1;
  bool ok = false;
};

GroundDrawResources LoadGroundDrawResources() {
  GroundDrawResources r{};
  r.shader = LoadShaderFromMemory(kGroundVs, kGroundFs);
  if (!IsShaderValid(r.shader)) {
    return r;
  }
  r.locMapEdge = GetShaderLocation(r.shader, "mapEdge");
  if (r.locMapEdge < 0) {
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

void DrawGroundWithMapEdge(const GroundDrawResources& res, int gridHalf, float tileWorld) {
  const float planeSize = static_cast<float>(gridHalf * 2 + 2) * tileWorld * 2.f;
  const float L = static_cast<float>(gridHalf) * tileWorld;
  constexpr float kFadeSpanTiles = 2.75f;
  const float fadeW = std::max(0.02f, kFadeSpanTiles * tileWorld);
  const float mapEdge[4] = {L, fadeW, 0.f, 0.f};

  if (!res.ok) {
    DrawPlane({0.f, 0.f, 0.f}, {planeSize, planeSize}, Color{32, 38, 48, 255});
    return;
  }
  SetShaderValue(res.shader, res.locMapEdge, mapEdge, SHADER_UNIFORM_VEC4);
  DrawModelEx(res.model, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, 0.f, {planeSize, 1.f, planeSize}, WHITE);
}

}  // namespace

int main() {
  constexpr int kWindowW = 960;
  constexpr int kWindowH = 540;
  constexpr int kGridHalf = 10;

  InitWindow(kWindowW, kWindowH, "cpptest — Diablo-style view");
  SetTargetFPS(60);

  RenderTexture2D worldTarget = LoadRenderTexture(kWorldBufferW, kWorldBufferH);
  SetTextureFilter(worldTarget.texture, TEXTURE_FILTER_POINT);

  GroundDrawResources ground = LoadGroundDrawResources();

  Vector2 player{4.5f, 4.5f};
  constexpr float kMoveSpeed = 5.f;
  constexpr float kSnapStrength = 18.f;

  Camera3D camera{};
  camera.fovy = 50.f;
  camera.projection = CAMERA_PERSPECTIVE;

  int camZoomTarget = 1;

  while (!WindowShouldClose()) {
    const float dt = GetFrameTime();

    if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) {
      camZoomTarget = std::max(0, camZoomTarget - 1);
    }
    if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) {
      camZoomTarget = std::min(3, camZoomTarget + 1);
    }

    const float camGoal = kCamDistLevels[camZoomTarget];
    {
      const float k = 1.f - std::exp(-kCamZoomSmooth * dt);
      g_camDist += (camGoal - g_camDist) * k;
      if (std::fabs(g_camDist - camGoal) < 0.004f) {
        g_camDist = camGoal;
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
      player.x += input.x * kMoveSpeed * dt;
      player.y += input.y * kMoveSpeed * dt;
    } else {
      Vector2 target = NearestTileCenter(player);
      ClampPlayerToGrid(target, kGridHalf, g_tileWorld);
      const float k = 1.f - std::exp(-kSnapStrength * dt);
      player.x += (target.x - player.x) * k;
      player.y += (target.y - player.y) * k;
      if (std::fabs(target.x - player.x) < 0.001f) player.x = target.x;
      if (std::fabs(target.y - player.y) < 0.001f) player.y = target.y;
    }

    ClampPlayerToGrid(player, kGridHalf, g_tileWorld);

    Vector3 focus = TileToWorldCenter(player.x, player.y);
    focus.y += g_tileWorld * 0.35f;
    UpdateDiabloStyleCamera(camera, focus);

    // ----- Layer 1: world (pixel-consistent internal resolution) -----
    BeginTextureMode(worldTarget);
    ClearBackground(Color{12, 14, 20, 255});

    BeginMode3D(camera);

    DrawGroundWithMapEdge(ground, kGridHalf, g_tileWorld);
    DrawWorldGrid(kGridHalf, Color{72, 86, 104, 255});

    const Vector3 feet = TileToWorldCenter(player.x, player.y);
    DrawPlayerBlock(feet, Color{210, 115, 70, 255}, Color{35, 18, 10, 255});

    EndMode3D();

    EndTextureMode();

    // ----- Compositing + Layer 2: UI (full window, independent layout) -----
    BeginDrawing();
    ClearBackground(Color{8, 9, 12, 255});

    DrawWorldBufferToScreen(worldTarget);

    const int m = UiPx(12.f);
    const int fs = UiPx(18.f);
    const int lh = UiPx(22.f);
    DrawText("WASD: move   [ / ] tile size   - / + zoom preset (4 steps, smooth)", m, m, fs, RAYWHITE);
    DrawText("World: buffer stretched to window; ground past grid blends to black (world space)", m, m + lh, fs,
             Color{200, 200, 200, 255});
    DrawText(TextFormat("Internal %dx%d  window %dx%d  uiScale %.2f", kWorldBufferW, kWorldBufferH,
                        GetScreenWidth(), GetScreenHeight(), static_cast<double>(UiScale())),
             m, m + lh * 2, fs, Color{170, 210, 170, 255});
    DrawText(TextFormat(
                 "tileWorld=%.2f  preset %d/4  dist %.1f→%.1f  pitch=%.0f°  yaw=%.0f°",
                 static_cast<double>(g_tileWorld), camZoomTarget + 1, static_cast<double>(g_camDist),
                 static_cast<double>(kCamDistLevels[camZoomTarget]), static_cast<double>(g_camPitchDeg),
                 static_cast<double>(g_camYawDeg)),
             m, m + lh * 3, fs, Color{160, 200, 230, 255});

    EndDrawing();
  }

  UnloadGroundDrawResources(ground);
  UnloadRenderTexture(worldTarget);
  CloseWindow();
  return 0;
}
