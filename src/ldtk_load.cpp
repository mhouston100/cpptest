#include "ldtk_load.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

// START REMOVE-ALL STUDY NOTES
// This file is the map/level importer. It reads the LDtk JSON file and turns the
// raw grid data into a simplified GameMap structure that the rest of the project
// can reason about. If you want to strip this guide out later, delete everything
// between the START and END markers in this file and in main.cpp.
// END REMOVE-ALL STUDY NOTES

namespace {

using nlohmann::json;

bool LayerIdentifierMatches(const std::string& id) {
  if (id.empty()) {
    return false;
  }
  if (id == "Walls_0" || id == "walls_0" || id == "_Level_walls") {
    return true;
  }
  return false;
}

bool InteractablesLayerIdentifierMatches(const std::string& id) {
  if (id.empty()) {
    return false;
  }
  return id == "Interactables" || id == "interactables";
}

bool FieldIdentifierMatches(const std::string& id, const char* snake, const char* pascal) {
  return id == snake || id == pascal;
}

std::string FieldString(const json& entity, const char* snake, const char* pascal) {
  if (!entity.contains("fieldInstances") || !entity["fieldInstances"].is_array()) {
    return {};
  }
  for (const auto& field : entity["fieldInstances"]) {
    const std::string id = field.value("__identifier", std::string{});
    if (!FieldIdentifierMatches(id, snake, pascal)) {
      continue;
    }
    const auto& value = field["__value"];
    if (value.is_string()) {
      return value.get<std::string>();
    }
    return {};
  }
  return {};
}

bool ParseEntityKind(const std::string& identifier, MapEntityKind& out) {
  if (identifier == "Spawn") {
    out = MapEntityKind::Spawn;
    return true;
  }
  if (identifier == "Warp") {
    out = MapEntityKind::Warp;
    return true;
  }
  if (identifier == "Npc") {
    out = MapEntityKind::Npc;
    return true;
  }
  if (identifier == "Prop") {
    out = MapEntityKind::Prop;
    return true;
  }
  return false;
}

bool CellFromEntity(const json& entity, int grid_px, int& cx, int& cy) {
  if (entity.contains("__grid") && entity["__grid"].is_array() && entity["__grid"].size() >= 2) {
    cx = entity["__grid"][0].get<int>();
    cy = entity["__grid"][1].get<int>();
    return true;
  }
  if (entity.contains("px") && entity["px"].is_array() && entity["px"].size() >= 2 && grid_px > 0) {
    cx = entity["px"][0].get<int>() / grid_px;
    cy = entity["px"][1].get<int>() / grid_px;
    return true;
  }
  return false;
}

void ApplyInteractableFromEntity(GameMap& m, const MapEntity& entity, int slot) {
  if (entity.kind != MapEntityKind::Npc && entity.kind != MapEntityKind::Prop) {
    return;
  }
  if (!m.InBounds(entity.cell_x, entity.cell_y)) {
    return;
  }

  const size_t idx = static_cast<size_t>(entity.cell_y * m.c_wid + entity.cell_x);
  m.interactables[idx] = slot;

  std::string name;
  std::string type;
  if (entity.kind == MapEntityKind::Npc) {
    name = !entity.npc_id.empty() ? entity.npc_id : std::string{"Npc"};
    type = !entity.dialog_key.empty() ? entity.dialog_key : name;
  } else {
    name = !entity.dialog_key.empty() ? entity.dialog_key : std::string{"Prop"};
    type = name;
  }
  m.interactable_names[idx] = name;
  m.interactable_type_ids[slot] = type;
}

void LoadEntityInstances(const json& layer, GameMap& m) {
  if (!layer.contains("entityInstances") || !layer["entityInstances"].is_array()) {
    return;
  }

  for (const auto& instance : layer["entityInstances"]) {
    const std::string identifier = instance.value("__identifier", std::string{});
    MapEntityKind kind = MapEntityKind::Prop;
    if (!ParseEntityKind(identifier, kind)) {
      continue;
    }

    int cx = 0;
    int cy = 0;
    if (!CellFromEntity(instance, m.grid_px, cx, cy)) {
      continue;
    }

    MapEntity entity;
    entity.kind = kind;
    entity.cell_x = cx;
    entity.cell_y = cy;
    entity.iid = instance.value("iid", std::string{});
    entity.spawn_id = FieldString(instance, "spawn_id", "SpawnId");
    entity.target_map = FieldString(instance, "target_map", "TargetMap");
    entity.target_spawn = FieldString(instance, "target_spawn", "TargetSpawn");
    entity.npc_id = FieldString(instance, "npc_id", "NpcId");
    entity.dialog_key = FieldString(instance, "dialog_key", "DialogKey");
    m.entities.push_back(std::move(entity));
  }
}

void LoadIntGridInteractables(const json& layer, GameMap& m, size_t expected) {
  if (layer.contains("intGridValues") && layer["intGridValues"].is_array()) {
    for (const auto& value_entry : layer["intGridValues"]) {
      const int value = value_entry.value("value", 0);
      const std::string identifier = value_entry.value("identifier", std::string{});
      if (value != 0 && !identifier.empty() && !m.interactable_type_ids.count(value)) {
        m.interactable_type_ids[value] = identifier;
      }
    }
  }

  if (!layer.contains("intGridCsv") || !layer["intGridCsv"].is_array() ||
      layer["intGridCsv"].size() != expected) {
    return;
  }

  std::unordered_map<std::string, int> type_counts;
  size_t i = 0;
  for (const auto& v : layer["intGridCsv"]) {
    const int value = v.get<int>();
    if (value != 0 && m.interactables[i] == 0) {
      m.interactables[i] = value;
      const std::string type_name = m.interactable_type_ids.count(value)
                                        ? m.interactable_type_ids[value]
                                        : std::string{"Unknown"};
      const int count = ++type_counts[type_name];
      std::ostringstream oss;
      oss << type_name << std::setw(3) << std::setfill('0') << count;
      m.interactable_names[i] = oss.str();
    }
    ++i;
  }
}

}  // namespace

// START REMOVE-ALL STUDY NOTES
// LoadLdtkLevel does the heavy lifting of translating an LDtk export into a
// runtime-friendly format:
//  1. Open the JSON file and validate the top-level structure.
//  2. Pick the requested level from the "levels" array.
//  3. Search the layer instances for the wall grid, entity instances, and leftover
//     IntGrid interactables (entities win when both occupy a cell).
//  4. Copy the intGridCsv values into GameMap::walls.
//  5. Record metadata such as cell size, level name, and named interactable types.
//  6. Return an empty string on success or an error string if the file is invalid.
//
// This separation is useful because the rest of the game does not need to know
// about the raw LDtk schema; it only needs functions such as IsWall(), InBounds(),
// and HasInteractable() from GameMap.
// END REMOVE-ALL STUDY NOTES
std::string LoadLdtkLevel(const std::string& path, const int level_index, GameMap& out) {
  GameMap m{};
  m.source_path = path;

  std::ifstream in(path);
  if (!in) {
    return "Could not open file: " + path;
  }

  json root;
  try {
    in >> root;
  } catch (const std::exception& e) {
    return std::string("JSON parse error: ") + e.what();
  }

  if (!root.contains("levels") || !root["levels"].is_array()) {
    return "LDtk file missing \"levels\" array";
  }
  const auto& levels = root["levels"];
  if (level_index < 0 || level_index >= static_cast<int>(levels.size())) {
    std::ostringstream oss;
    oss << "level_index " << level_index << " out of range (levels size " << levels.size() << ")";
    return oss.str();
  }

  const json& level = levels[level_index];
  m.level_identifier = level.value("identifier", std::string{});

  if (!level.contains("layerInstances") || !level["layerInstances"].is_array()) {
    return "Level missing layerInstances";
  }

  const json* wall_layer = nullptr;
  const json* interact_layer = nullptr;
  const json* entities_layer = nullptr;
  for (const auto& layer : level["layerInstances"]) {
    const std::string type = layer.value("__type", std::string{});
    const std::string lid = layer.value("__identifier", std::string{});
    if (type == "Entities") {
      entities_layer = &layer;
      continue;
    }
    if (type != "IntGrid") {
      continue;
    }
    if (!layer.contains("intGridCsv") || !layer["intGridCsv"].is_array()) {
      continue;
    }
    if (LayerIdentifierMatches(lid)) {
      wall_layer = &layer;
    }
    if (InteractablesLayerIdentifierMatches(lid)) {
      interact_layer = &layer;
    }
  }
  if (wall_layer == nullptr) {
    return "No Walls IntGrid layer with intGridCsv found in level";
  }

  const auto& layer = *wall_layer;
  m.c_wid = layer.value("__cWid", 0);
  m.c_hei = layer.value("__cHei", 0);
  m.grid_px = layer.value("__gridSize", root.value("defaultGridSize", 32));

  if (m.c_wid <= 0 || m.c_hei <= 0) {
    return "Invalid __cWid/__cHei";
  }

  const auto& csv = layer["intGridCsv"];
  const size_t expected = static_cast<size_t>(m.c_wid) * static_cast<size_t>(m.c_hei);
  if (csv.size() != expected) {
    std::ostringstream oss;
    oss << "intGridCsv size " << csv.size() << " != " << expected;
    return oss.str();
  }

  m.walls.resize(expected);
  size_t i = 0;
  for (const auto& v : csv) {
    m.walls[i++] = v.get<int>();
  }

  m.interactables.assign(expected, 0);
  m.interactable_names.assign(expected, std::string{});
  m.interactable_type_ids.clear();
  m.entities.clear();

  if (entities_layer != nullptr) {
    LoadEntityInstances(*entities_layer, m);
    int slot = 1;
    for (const auto& entity : m.entities) {
      ApplyInteractableFromEntity(m, entity, slot);
      ++slot;
    }
  }

  if (interact_layer != nullptr) {
    LoadIntGridInteractables(*interact_layer, m, expected);
  }

  out = std::move(m);
  return {};
}
