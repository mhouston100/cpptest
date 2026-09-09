#pragma once

#include <raylib.h>

#include "game_map.hpp"

// START REMOVE-ALL STUDY NOTES
// Rendering helpers are placed in their own feature module to keep the main loop
// from becoming a giant draw-call list. This makes it easier to change how the
// world, grid, or floor fade is drawn without disturbing gameplay systems.
// END REMOVE-ALL STUDY NOTES

extern float g_tileWorld;

struct GroundDrawResources {
  Model model{};
  Shader shader{};
  int loc_map_edge = -1;
  bool ok = false;
};

GroundDrawResources LoadGroundDrawResources();
void UnloadGroundDrawResources(GroundDrawResources& r);
void DrawGroundWithMapEdge(const GroundDrawResources& res, const GameMap& m,
                          const float tile_world);
void DrawWorldGridForMap(const GameMap& m, const Color line_color);
void DrawWallCells(const GameMap& m);
void DrawInteractables(const GameMap& m);
void DrawPlayerBlock(const Vector3& base_center, const Color fill, const Color outline);
