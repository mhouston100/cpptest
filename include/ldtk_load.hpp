#pragma once

#include <string>

#include "game_map.hpp"

// Loads one level from an LDtk .ldtk JSON file (level_index into top-level "levels" array).
// Returns empty string on success, otherwise an error message.
[[nodiscard]] std::string LoadLdtkLevel(const std::string& path, int level_index, GameMap& out);
