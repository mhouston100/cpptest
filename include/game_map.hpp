#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

enum class MapEntityKind { Spawn, Warp, Npc, Prop };

struct MapEntity {
  MapEntityKind kind = MapEntityKind::Prop;
  int cell_x = 0;
  int cell_y = 0;
  std::string iid;
  std::string spawn_id;
  std::string target_map;
  std::string target_spawn;
  std::string npc_id;
  std::string dialog_key;
};

[[nodiscard]] inline bool IsTalkable(const MapEntityKind kind) noexcept {
  return kind == MapEntityKind::Npc || kind == MapEntityKind::Prop;
}

inline void TalkIdentity(const MapEntity& entity, std::string& name, std::string& type) {
  if (entity.kind == MapEntityKind::Npc) {
    name = !entity.npc_id.empty() ? entity.npc_id : std::string{"Npc"};
    type = !entity.dialog_key.empty() ? entity.dialog_key : name;
    return;
  }
  name = !entity.dialog_key.empty() ? entity.dialog_key : std::string{"Prop"};
  type = name;
}

// One LDtk level instance: IntGrid wall mask (row-major, LDtk Y down = iy increases downward in file).
struct GameMap {
  int c_wid = 0;
  int c_hei = 0;
  int grid_px = 32;
  std::string level_identifier;
  std::string source_path;
  std::vector<int> walls;
  std::vector<MapEntity> entities;

  [[nodiscard]] bool InBounds(int ix, int iy) const noexcept {
    return ix >= 0 && iy >= 0 && ix < c_wid && iy < c_hei;
  }

  [[nodiscard]] int Cell(int ix, int iy) const noexcept {
    return walls[static_cast<size_t>(iy * c_wid + ix)];
  }

  [[nodiscard]] bool IsWall(int ix, int iy) const noexcept {
    return InBounds(ix, iy) && Cell(ix, iy) != 0;
  }

  void WorldToCell(float wx, float wy, int& ix, int& iy) const {
    ix = static_cast<int>(std::floor(wx + static_cast<float>(c_wid) * 0.5f - 0.5f + 1e-4f));
    iy = static_cast<int>(std::floor(wy + static_cast<float>(c_hei) * 0.5f - 0.5f + 1e-4f));
    if (c_wid > 0) {
      ix = std::clamp(ix, 0, c_wid - 1);
    }
    if (c_hei > 0) {
      iy = std::clamp(iy, 0, c_hei - 1);
    }
  }

  [[nodiscard]] const MapEntity* FindAt(int ix, int iy) const {
    for (const auto& entity : entities) {
      if (entity.cell_x == ix && entity.cell_y == iy) {
        return &entity;
      }
    }
    return nullptr;
  }

  [[nodiscard]] const MapEntity* FindAt(int ix, int iy, MapEntityKind kind) const {
    for (const auto& entity : entities) {
      if (entity.kind == kind && entity.cell_x == ix && entity.cell_y == iy) {
        return &entity;
      }
    }
    return nullptr;
  }

  [[nodiscard]] const MapEntity* FindSpawn(const std::string& spawn_id) const {
    const MapEntity* fallback = nullptr;
    for (const auto& entity : entities) {
      if (entity.kind != MapEntityKind::Spawn) {
        continue;
      }
      if (fallback == nullptr) {
        fallback = &entity;
      }
      if (!spawn_id.empty() && entity.spawn_id == spawn_id) {
        return &entity;
      }
    }
    return fallback;
  }
};
