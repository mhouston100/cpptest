#include "ldtk_load.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

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

}  // namespace

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
  for (const auto& layer : level["layerInstances"]) {
    const std::string type = layer.value("__type", std::string{});
    if (type != "IntGrid") {
      continue;
    }
    if (!layer.contains("intGridCsv") || !layer["intGridCsv"].is_array()) {
      continue;
    }
    const std::string lid = layer.value("__identifier", std::string{});
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

  if (interact_layer != nullptr) {
    if ((*interact_layer).contains("intGridValues") && (*interact_layer)["intGridValues"].is_array()) {
      for (const auto& value_entry : (*interact_layer)["intGridValues"]) {
        const int value = value_entry.value("value", 0);
        const std::string identifier = value_entry.value("identifier", std::string{});
        if (value != 0 && !identifier.empty()) {
          m.interactable_type_ids[value] = identifier;
        }
      }
    }

    const auto& interact_csv = (*interact_layer)["intGridCsv"];
    if (interact_csv.is_array() && interact_csv.size() == expected) {
      i = 0;
      std::unordered_map<std::string, int> type_counts;
      for (const auto& v : interact_csv) {
        const int value = v.get<int>();
        m.interactables[i] = value;
        if (value != 0) {
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
  }

  out = std::move(m);
  return {};
}
