#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <raylib.h>

#include "game_map.hpp"

// START REMOVE-ALL STUDY NOTES
// Dialogue data is treated as a content feature rather than a gameplay function.
// The loader reads JSON files and builds a tree of steps/choices. This keeps the
// UI and player interaction logic independent from the raw dialog schema.
// END REMOVE-ALL STUDY NOTES

struct DialogChoice {
  std::string label;
  int next_step = -1;
};

struct DialogStep {
  std::string text;
  std::vector<DialogChoice> choices;
};

struct DialogTree {
  std::vector<DialogStep> steps;
  int current_step = 0;
};

std::string ReplaceDialogTokens(std::string text, const std::string& instance_name,
                                const std::string& type_name);
bool LoadDialogTreeFromJson(const nlohmann::json& root, DialogTree& out,
                            const std::string& source_name);
std::unordered_map<std::string, DialogTree> LoadDialogRegistry(const std::string& dialog_dir);
DialogTree MakeFallbackDialogTree(const std::string& instance_name,
                                 const std::string& type_name);
DialogTree MakeDialogTreeForInstance(const std::string& instance_name,
                                     const std::string& type_name,
                                     const std::unordered_map<std::string, DialogTree>& registry);
bool GetAdjacentTalkable(const Vector2& player, const GameMap& m, std::string& out_name,
                         std::string& out_type);
