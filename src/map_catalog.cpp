#include "map_catalog.hpp"

#include <fstream>
#include <filesystem>

#include <nlohmann/json.hpp>
#include <raylib.h>

namespace cpptest {
namespace {

using nlohmann::json;

std::string JoinPath(const char* dir, const char* file) {
  std::string path = dir != nullptr ? dir : "";
  if (!path.empty() && path.back() != '/' && path.back() != '\\') {
    path += '/';
  }
  path += file;
  return path;
}

std::string ResolveDataPath(const std::string& rel) {
  std::ifstream in(rel);
  if (in) {
    return rel;
  }
  const std::string next_to_binary = JoinPath(GetApplicationDirectory(), rel.c_str());
  std::ifstream alt(next_to_binary);
  if (alt) {
    return next_to_binary;
  }
  return rel;
}

std::string FileName(const std::string& path) {
  const std::filesystem::path p(path);
  return p.filename().string();
}

std::string FileStem(const std::string& path) {
  const std::filesystem::path p(path);
  return p.stem().string();
}

}  // namespace

std::string LoadMapCatalog(const std::string& path, MapCatalog& out) {
  MapCatalog catalog{};
  const std::string resolved = ResolveDataPath(path);
  std::ifstream in(resolved);
  if (!in) {
    return "Could not open map catalog: " + resolved;
  }

  json root;
  try {
    in >> root;
  } catch (const std::exception& e) {
    return std::string("Map catalog JSON parse error: ") + e.what();
  }

  if (!root.is_object() || !root.contains("maps") || !root["maps"].is_array()) {
    return "Map catalog missing \"maps\" array";
  }

  catalog.start_id = root.value("start", std::string{});
  for (const auto& entry_json : root["maps"]) {
    if (!entry_json.is_object()) {
      continue;
    }
    MapCatalogEntry entry;
    entry.id = entry_json.value("id", std::string{});
    entry.path = entry_json.value("path", std::string{});
    entry.level_index = entry_json.value("level_index", 0);
    if (entry.id.empty() || entry.path.empty()) {
      return "Map catalog entry needs id and path";
    }
    catalog.maps.push_back(std::move(entry));
  }

  if (catalog.maps.empty()) {
    return "Map catalog has no maps";
  }
  if (catalog.start_id.empty()) {
    catalog.start_id = catalog.maps.front().id;
  }
  if (FindCatalogMap(catalog, catalog.start_id) == nullptr) {
    return "Map catalog start id not found: " + catalog.start_id;
  }

  out = std::move(catalog);
  return {};
}

const MapCatalogEntry* FindCatalogMap(const MapCatalog& catalog, const std::string& key) {
  if (key.empty()) {
    return nullptr;
  }
  for (const auto& entry : catalog.maps) {
    if (entry.id == key) {
      return &entry;
    }
  }
  for (const auto& entry : catalog.maps) {
    if (entry.path == key || FileName(entry.path) == key || FileStem(entry.path) == key ||
        FileName(entry.path) == FileName(key) || FileStem(entry.path) == FileStem(key)) {
      return &entry;
    }
  }
  return nullptr;
}

const MapCatalogEntry* CatalogStart(const MapCatalog& catalog) {
  return FindCatalogMap(catalog, catalog.start_id);
}

int CatalogIndexOf(const MapCatalog& catalog, const std::string& id) {
  for (int i = 0; i < static_cast<int>(catalog.maps.size()); ++i) {
    if (catalog.maps[static_cast<size_t>(i)].id == id) {
      return i;
    }
  }
  return -1;
}

}  // namespace cpptest
