#pragma once

#include <string>
#include <vector>

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

struct MapVisuals {
  Texture2D atlas{};
  bool ok = false;
  int columns = 4;
  int rows = 3;
  int floor_tile = 0;
  std::vector<int> wall_tiles;
};

GroundDrawResources LoadGroundDrawResources();
void UnloadGroundDrawResources(GroundDrawResources& r);
MapVisuals LoadMapVisuals();
void UnloadMapVisuals(MapVisuals& visuals);
void DrawGroundWithMapEdge(const GroundDrawResources& res, const GameMap& m,
                          const float tile_world);
void DrawWorldGridForMap(const GameMap& m, const Color line_color);
void DrawWallCells(const GameMap& m);
void DrawTiledMap(const MapVisuals& visuals, const GameMap& m);
void DrawMapEntities(const GameMap& m);
void DrawPlayerBlock(const Vector3& base_center, const Color fill, const Color outline);
