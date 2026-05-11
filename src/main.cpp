#include <algorithm>
#include <cmath>

#include <raylib.h>
#include <raymath.h>

namespace {

// --- World / sprite layer: fixed internal resolution (author sprites for this canvas).
// All gameplay + billboards + 3D scene draw here; upscale is integer + nearest = crisp pixels.
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

float g_camDist = 14.f;
float g_camPitchDeg = 50.f;
float g_camYawDeg = 42.f;

Vector3 TileToWorldCenter(float tx, float tz) {
  return {tx * g_tileWorld, 0.f, tz * g_tileWorld};
}

Vector2 NearestTileCenter(Vector2 p) {
  return {std::round(p.x - 0.5f) + 0.5f, std::round(p.y - 0.5f) + 0.5f};
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
  const float s = g_tileWorld * 0.35f;
  const float h = g_tileWorld * 0.85f;
  const Vector3 c{baseCenter.x, baseCenter.y + h * 0.5f, baseCenter.z};
  DrawCube(c, s * 2.f, h, s * 2.f, fill);
  DrawCubeWires(c, s * 2.f, h, s * 2.f, outline);
}

// Integer scale of world buffer into the window; letterboxes the rest.
Rectangle WorldDestRect(int bufW, int bufH) {
  const int sw = GetScreenWidth();
  const int sh = GetScreenHeight();
  int scale = std::min(sw / bufW, sh / bufH);
  if (scale < 1) {
    scale = 1;
  }
  const int dw = bufW * scale;
  const int dh = bufH * scale;
  const int ox = (sw - dw) / 2;
  const int oy = (sh - dh) / 2;
  return {static_cast<float>(ox), static_cast<float>(oy), static_cast<float>(dw), static_cast<float>(dh)};
}

void DrawWorldBufferToScreen(const RenderTexture2D& worldRt, int bufW, int bufH) {
  const Rectangle dest = WorldDestRect(bufW, bufH);
  const Rectangle src{0.f, 0.f, static_cast<float>(worldRt.texture.width),
                      -static_cast<float>(worldRt.texture.height)};
  DrawTexturePro(worldRt.texture, src, dest, {0.f, 0.f}, 0.f, WHITE);
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

  Vector2 player{4.5f, 4.5f};
  constexpr float kMoveSpeed = 5.f;
  constexpr float kSnapStrength = 18.f;

  Camera3D camera{};
  camera.fovy = 50.f;
  camera.projection = CAMERA_PERSPECTIVE;

  while (!WindowShouldClose()) {
    const float dt = GetFrameTime();

    if (IsKeyDown(KEY_LEFT_BRACKET)) {
      g_tileWorld = std::max(0.25f, g_tileWorld - 1.2f * dt);
    }
    if (IsKeyDown(KEY_RIGHT_BRACKET)) {
      g_tileWorld += 1.2f * dt;
    }
    if (IsKeyDown(KEY_MINUS) || IsKeyDown(KEY_KP_SUBTRACT)) {
      g_camDist = std::max(6.f, g_camDist - 10.f * dt);
    }
    if (IsKeyDown(KEY_EQUAL) || IsKeyDown(KEY_KP_ADD)) {
      g_camDist += 10.f * dt;
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
      const Vector2 target = NearestTileCenter(player);
      const float k = 1.f - std::exp(-kSnapStrength * dt);
      player.x += (target.x - player.x) * k;
      player.y += (target.y - player.y) * k;
      if (std::fabs(target.x - player.x) < 0.001f) player.x = target.x;
      if (std::fabs(target.y - player.y) < 0.001f) player.y = target.y;
    }

    Vector3 focus = TileToWorldCenter(player.x, player.y);
    focus.y += g_tileWorld * 0.35f;
    UpdateDiabloStyleCamera(camera, focus);

    // ----- Layer 1: world (pixel-consistent internal resolution) -----
    BeginTextureMode(worldTarget);
    ClearBackground(Color{12, 14, 20, 255});

    BeginMode3D(camera);

    DrawPlane({0.f, 0.f, 0.f}, {static_cast<float>(kGridHalf * 2 + 2) * g_tileWorld * 2.f,
                                 static_cast<float>(kGridHalf * 2 + 2) * g_tileWorld * 2.f},
              Color{32, 38, 48, 255});
    DrawWorldGrid(kGridHalf, Color{72, 86, 104, 255});

    const Vector3 feet = TileToWorldCenter(player.x, player.y);
    DrawPlayerBlock(feet, Color{210, 115, 70, 255}, Color{35, 18, 10, 255});

    EndMode3D();
    EndTextureMode();

    // ----- Compositing + Layer 2: UI (full window, independent layout) -----
    BeginDrawing();
    ClearBackground(Color{8, 9, 12, 255});

    DrawWorldBufferToScreen(worldTarget, kWorldBufferW, kWorldBufferH);

    const int m = UiPx(12.f);
    const int fs = UiPx(18.f);
    const int lh = UiPx(22.f);
    DrawText("WASD: move   [ / ] tile size   - / + dist   ; / ' pitch   , / . yaw", m, m, fs, RAYWHITE);
    DrawText("World buffer: pixel-consistent (nearest integer scale)", m, m + lh, fs,
             Color{200, 200, 200, 255});
    DrawText(TextFormat("Internal %dx%d  window %dx%d  uiScale %.2f", kWorldBufferW, kWorldBufferH,
                        GetScreenWidth(), GetScreenHeight(), static_cast<double>(UiScale())),
             m, m + lh * 2, fs, Color{170, 210, 170, 255});
    DrawText(TextFormat("tileWorld=%.2f  dist=%.1f  pitch=%.0f°  yaw=%.0f°",
                        static_cast<double>(g_tileWorld), static_cast<double>(g_camDist),
                        static_cast<double>(g_camPitchDeg), static_cast<double>(g_camYawDeg)),
             m, m + lh * 3, fs, Color{160, 200, 230, 255});

    EndDrawing();
  }

  UnloadRenderTexture(worldTarget);
  CloseWindow();
  return 0;
}
