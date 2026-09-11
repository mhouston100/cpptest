#include "ldtk_load.hpp"

#include <fstream>
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

}  // namespace

// START REMOVE-ALL STUDY NOTES
// LoadLdtkLevel translates one LDtk level into GameMap:
//  1. Open the JSON file and pick the requested level.
//  2. Copy the Walls IntGrid into GameMap::walls (collision only).
//  3. Copy Entities-layer instances into GameMap::entities (spawn, warp, talk).
//  4. Return an empty string on success or an error string if the file is invalid.
//
// IntGrid is not used for people, props, or doors. Those are entity instances with
// fields. The rest of the game asks GameMap for IsWall() or FindAt(), not raw LDtk.
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

  if (entities_layer != nullptr) {
    LoadEntityInstances(*entities_layer, m);
  }

  out = std::move(m);
  return {};
}
