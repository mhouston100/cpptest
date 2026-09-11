#pragma once

#include <string>
#include <vector>

namespace cpptest {

struct MapCatalogEntry {
  std::string id;
  std::string path;
  int level_index = 0;
};

struct MapCatalog {
  std::string start_id;
  std::vector<MapCatalogEntry> maps;
};

// Loads a map table from JSON. Returns empty string on success.
[[nodiscard]] std::string LoadMapCatalog(const std::string& path, MapCatalog& out);
[[nodiscard]] const MapCatalogEntry* FindCatalogMap(const MapCatalog& catalog, const std::string& key);
[[nodiscard]] const MapCatalogEntry* CatalogStart(const MapCatalog& catalog);
[[nodiscard]] int CatalogIndexOf(const MapCatalog& catalog, const std::string& id);

}  // namespace cpptest
