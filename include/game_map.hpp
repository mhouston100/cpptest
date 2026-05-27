#pragma once

#include <string>
#include <vector>

// One LDtk level instance: IntGrid wall mask (row-major, LDtk Y down = iy increases downward in file).
struct GameMap {
  int c_wid = 0;
  int c_hei = 0;
  int grid_px = 32;
  std::string level_identifier;
  std::string source_path;
  std::vector<int> walls;

  [[nodiscard]] bool InBounds(int ix, int iy) const noexcept {
    return ix >= 0 && iy >= 0 && ix < c_wid && iy < c_hei;
  }

  [[nodiscard]] int Cell(int ix, int iy) const noexcept {
    return walls[static_cast<size_t>(iy * c_wid + ix)];
  }

  [[nodiscard]] bool IsWall(int ix, int iy) const noexcept {
    return InBounds(ix, iy) && Cell(ix, iy) != 0;
  }
};
