#include "player.hpp"

#include <algorithm>
#include <cmath>

// START REMOVE-ALL STUDY NOTES
// Player movement is handled in a dedicated module so the game loop does not need
// to understand the details of tile snapping, wall collision, or map bounds.
// The important idea here is to separate movement intent from collision resolution.
// END REMOVE-ALL STUDY NOTES

namespace cpptest {
namespace {

constexpr float kMoveSpeed = 5.f;
constexpr float kMoveAccel = 16.f;
constexpr float kSnapStrength = 18.f;

void CellCenterWorld(const GameMap& m, int ix, int iy, Vector2& out) {
  out.x = static_cast<float>(ix) + 0.5f - static_cast<float>(m.c_wid) * 0.5f;
  out.y = static_cast<float>(iy) + 0.5f - static_cast<float>(m.c_hei) * 0.5f;
}

}  // namespace

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

void PlayerToCell(const Vector2& p, const GameMap& m, int& ix, int& iy) {
  m.WorldToCell(p.x, p.y, ix, iy);
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

void SpawnPlayerAtFirstWalkable(const GameMap& m, Vector2& out) {
  for (int iy = 0; iy < m.c_hei; ++iy) {
    for (int ix = 0; ix < m.c_wid; ++ix) {
      if (!m.IsWall(ix, iy)) {
        CellCenterWorld(m, ix, iy, out);
        return;
      }
    }
  }
  out = {0.f, 0.f};
}

void SpawnPlayerAtSpawn(const GameMap& m, const std::string& spawn_id, Vector2& out) {
  if (const MapEntity* spawn = m.FindSpawn(spawn_id)) {
    CellCenterWorld(m, spawn->cell_x, spawn->cell_y, out);
    return;
  }
  SpawnPlayerAtFirstWalkable(m, out);
}

void UpdatePlayerMovement(PlayerState& player_state, const GameMap& map,
                         const Vector2& input, float dt, float camera_yaw_deg) {
  const Vector2 player_prev = player_state.position;
  const float move_yaw = camera_yaw_deg * DEG2RAD;
  const float move_sy = std::sin(move_yaw);
  const float move_cy = std::cos(move_yaw);

  Vector2 move{
      input.y * (-move_sy) + input.x * (move_cy),
      input.y * (-move_cy) + input.x * (-move_sy),
  };

  const bool moving = (input.x != 0.f || input.y != 0.f);
  Vector2 target = NearestTileCenter(player_state.position);
  ClampPlayerToMapBounds(target, map);
  const bool target_diff = std::fabs(target.x - player_state.position.x) > 0.001f ||
                          std::fabs(target.y - player_state.position.y) > 0.001f;

  if (moving) {
    // Normalize direction so diagonal movement travels at the same speed as axis-aligned movement.
    const float move_len = std::sqrt(move.x * move.x + move.y * move.y);
    if (move_len > 1e-5f) {
      move.x /= move_len;
      move.y /= move_len;
    }

    const Vector2 desired{move.x * kMoveSpeed, move.y * kMoveSpeed};
    const float accel_k = 1.f - std::exp(-kMoveAccel * dt);
    player_state.velocity.x += (desired.x - player_state.velocity.x) * accel_k;
    player_state.velocity.y += (desired.y - player_state.velocity.y) * accel_k;

    // Resolve collisions axis-by-axis to make diagonal wall hits slide smoothly instead of sticking.
    const Vector2 proposed = {player_state.position.x + player_state.velocity.x * dt,
                              player_state.position.y + player_state.velocity.y * dt};
    Vector2 resolved = player_state.position;

    resolved.x = proposed.x;
    ClampPlayerToMapBounds(resolved, map);
    if (IsWallAtPlayer(resolved, map)) {
      player_state.velocity.x = 0.f;
      resolved.x = player_state.position.x;
    }

    resolved.y = proposed.y;
    ClampPlayerToMapBounds(resolved, map);
    if (IsWallAtPlayer(resolved, map)) {
      player_state.velocity.y = 0.f;
      resolved.y = player_state.position.y;
    }

    ClampPlayerToMapBounds(resolved, map);
    if (IsWallAtPlayer(resolved, map)) {
      resolved = player_state.position;
      player_state.velocity = {0.f, 0.f};
    }

    player_state.position = resolved;
    player_state.was_snapping = false;
  } else {
    player_state.velocity = {0.f, 0.f};
    const bool will_snap = target_diff && !IsWallAtPlayer(target, map);
    if (will_snap) {
      const float k = 1.f - std::exp(-kSnapStrength * dt);
      player_state.position.x += (target.x - player_state.position.x) * k;
      player_state.position.y += (target.y - player_state.position.y) * k;
      if (std::fabs(target.x - player_state.position.x) < 0.001f) player_state.position.x = target.x;
      if (std::fabs(target.y - player_state.position.y) < 0.001f) player_state.position.y = target.y;
    }
    player_state.was_snapping = will_snap;
  }

  player_state.was_moving = moving;
  ClampPlayerToMapBounds(player_state.position, map);
  if (IsWallAtPlayer(player_state.position, map)) {
    player_state.position = player_prev;
  }
}

}  // namespace cpptest
