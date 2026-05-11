#include <cmath>

#include <raylib.h>
#include <raymath.h>

namespace {

// Isometric screen offset from grid/world position (float tile coords).
// One step +gx / +gy moves along a screen diagonal; defaults approximate a 32px-wide tile (2:1 iso).
float g_isoHalfW = 16.f;
float g_isoHalfH = 16.f;

Vector2 GridToScreen(float gx, float gy) {
  return {(gx - gy) * g_isoHalfW, (gx + gy) * g_isoHalfH};
}

Vector2 NearestTileCenter(Vector2 p) {
  return {std::round(p.x - 0.5f) + 0.5f, std::round(p.y - 0.5f) + 0.5f};
}

void DrawIsoGrid(int gx0, int gy0, int gx1, int gy1, Vector2 cam, Color lineColor) {
  for (int gx = gx0; gx <= gx1; ++gx) {
    for (int gy = gy0; gy <= gy1; ++gy) {
      const Vector2 c00 = Vector2Add(GridToScreen(static_cast<float>(gx), static_cast<float>(gy)), cam);
      const Vector2 c10 = Vector2Add(GridToScreen(static_cast<float>(gx + 1), static_cast<float>(gy)), cam);
      const Vector2 c11 = Vector2Add(GridToScreen(static_cast<float>(gx + 1), static_cast<float>(gy + 1)), cam);
      const Vector2 c01 = Vector2Add(GridToScreen(static_cast<float>(gx), static_cast<float>(gy + 1)), cam);
      DrawLineV(c00, c10, lineColor);
      DrawLineV(c10, c11, lineColor);
      DrawLineV(c11, c01, lineColor);
      DrawLineV(c01, c00, lineColor);
    }
  }
}

void DrawPlayerSquare(Vector2 gridPos, Vector2 cam, Color fill, Color outline) {
  const Vector2 s = Vector2Add(GridToScreen(gridPos.x, gridPos.y), cam);
  const float half = 8.f;
  const Rectangle r{s.x - half, s.y - half, half * 2.f, half * 2.f};
  DrawRectangleRec(r, fill);
  DrawRectangleLinesEx(r, 2.f, outline);
}

}  // namespace

int main() {
  constexpr int kWindowW = 960;
  constexpr int kWindowH = 540;
  constexpr int kGridHalf = 10;

  InitWindow(kWindowW, kWindowH, "cpptest isometric");
  SetTargetFPS(60);

  Vector2 player{4.5f, 4.5f};
  constexpr float kMoveSpeed = 5.f;       // tiles per second (world grid units)
  constexpr float kSnapStrength = 18.f;   // higher = snappier settle onto tile center

  while (!WindowShouldClose()) {
    const float dt = GetFrameTime();

    // Adjustable isometric scale: [ / ] tweak both halves together (roughly "tile size" feel).
    if (IsKeyDown(KEY_LEFT_BRACKET)) {
      g_isoHalfW = std::max(4.f, g_isoHalfW - 20.f * dt);
      g_isoHalfH = std::max(4.f, g_isoHalfH - 20.f * dt);
    }
    if (IsKeyDown(KEY_RIGHT_BRACKET)) {
      g_isoHalfW += 20.f * dt;
      g_isoHalfH += 20.f * dt;
    }

    Vector2 input{0.f, 0.f};
    if (IsKeyDown(KEY_W)) input.y -= 1.f;  // grid -Y => up-right on screen for this transform
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

    const Vector2 origin{kWindowW * 0.5f, kWindowH * 0.35f};
    const Vector2 playerScreen = GridToScreen(player.x, player.y);
    const Vector2 cam = Vector2Subtract(origin, playerScreen);

    BeginDrawing();
    ClearBackground(Color{24, 28, 36, 255});

    DrawIsoGrid(-kGridHalf, -kGridHalf, kGridHalf, kGridHalf, cam,
                Color{70, 82, 99, 255});
    DrawPlayerSquare(player, cam, Color{220, 120, 80, 255}, Color{40, 20, 10, 255});

    DrawText("WASD: grid axes (W = up-right on screen)", 12, 12, 18, RAYWHITE);
    DrawText("[ / ]: shrink / grow isometric tile scale (isoHalfW & isoHalfH)", 12, 34, 18,
             Color{200, 200, 200, 255});
    DrawText(TextFormat("isoHalfW=%.1f  isoHalfH=%.1f", static_cast<double>(g_isoHalfW),
                        static_cast<double>(g_isoHalfH)),
             12, 56, 18, Color{180, 220, 180, 255});

    EndDrawing();
  }

  CloseWindow();
  return 0;
}
