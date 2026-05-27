#include "ldtk_load.hpp"

#include <fstream>
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

  const json* chosen = nullptr;
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
      chosen = &layer;
      break;
    }
  }
  if (chosen == nullptr) {
    for (const auto& layer : level["layerInstances"]) {
      if (layer.value("__type", std::string{}) == "IntGrid" && layer.contains("intGridCsv") &&
          layer["intGridCsv"].is_array()) {
        chosen = &layer;
        break;
      }
    }
  }
  if (chosen == nullptr) {
    return "No IntGrid layer with intGridCsv found in level";
  }

  const auto& layer = *chosen;
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

  out = std::move(m);
  return {};
}
