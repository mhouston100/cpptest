#pragma once

#include <raylib.h>

#include "game_map.hpp"

// START REMOVE-ALL STUDY NOTES
// This file holds the player state and the movement helpers. The goal is to keep
// all of the map-space movement logic in one feature module so the game loop can
// remain focused on orchestration instead of low-level collision behavior.
// END REMOVE-ALL STUDY NOTES

namespace cpptest {

constexpr float kPlayerFootprintHalfTiles = 0.35f;
constexpr float kGridInnerMarginTiles = 0.5f;

struct PlayerState {
  Vector2 position{0.f, 0.f};
  Vector2 velocity{0.f, 0.f};
  bool was_moving = false;
  bool was_snapping = false;
};

Vector2 NearestTileCenter(Vector2 p);
void ClampPlayerToMapBounds(Vector2& p, const GameMap& m);
void PlayerToCell(const Vector2& p, const GameMap& m, int& ix, int& iy);
bool IsWallAtPlayer(const Vector2& p, const GameMap& m);
void SpawnPlayerAtFirstWalkable(const GameMap& m, Vector2& out);
void UpdatePlayerMovement(PlayerState& player_state, const GameMap& map,
                         const Vector2& input, float dt, float camera_yaw_deg);

}  // namespace cpptest
