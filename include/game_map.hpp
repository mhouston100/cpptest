#pragma once

#include <string>
#include <unordered_map>
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

  [[nodiscard]] int Interactable(int ix, int iy) const noexcept {
    return InBounds(ix, iy) ? interactables[static_cast<size_t>(iy * c_wid + ix)] : 0;
  }

  [[nodiscard]] std::string InteractableType(int ix, int iy) const noexcept {
    const int value = Interactable(ix, iy);
    if (value == 0) {
      return {};
    }
    const auto it = interactable_type_ids.find(value);
    return it != interactable_type_ids.end() ? it->second : std::string{};
  }

  [[nodiscard]] std::string InteractableName(int ix, int iy) const noexcept {
    return InBounds(ix, iy) ? interactable_names[static_cast<size_t>(iy * c_wid + ix)] : std::string{};
  }

  [[nodiscard]] bool HasInteractable(int ix, int iy) const noexcept {
    return Interactable(ix, iy) != 0;
  }

  [[nodiscard]] bool IsWall(int ix, int iy) const noexcept {
    return InBounds(ix, iy) && Cell(ix, iy) != 0;
  }

  std::vector<int> interactables;
  std::vector<std::string> interactable_names;
  std::unordered_map<int, std::string> interactable_type_ids;
};
