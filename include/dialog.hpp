#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <raylib.h>

#include "game_map.hpp"
#include "npc.hpp"

// START REMOVE-ALL STUDY NOTES
// Dialogue data is treated as a content feature rather than a gameplay function.
// The loader reads JSON files and builds a tree of steps/choices. This keeps the
// UI and player interaction logic independent from the raw dialog schema.
// END REMOVE-ALL STUDY NOTES

struct DialogCondition {
  std::string flag;
  bool flag_equals = true;
  std::vector<std::string> flags_on;
  std::vector<std::string> flags_off;
  int relationship_min = -1;
  int relationship_max = -1;
  int money_min = -1;
};

struct DialogEffect {
  std::string set_flag;
  bool flag_value = true;
  int relationship_delta = 0;
  int minutes = 0;
  int energy = 0;
  int stress = 0;
  int money = 0;
  int health = 0;
  std::string start_incident;
  std::string apply_job;
  std::string take_course;
};

struct DialogChoice {
  std::string label;
  int next_step = -1;
  std::string next_id;
  DialogCondition cond;
  std::vector<DialogEffect> effects;
};

struct DialogStep {
  std::string id;
  std::string text;
  DialogCondition cond;
  std::vector<DialogEffect> effects;
  std::vector<DialogChoice> choices;
  bool entry = true;
};

struct DialogTree {
  std::vector<DialogStep> steps;
  int current_step = 0;
};

std::string ReplaceDialogTokens(std::string text, const std::string& instance_name,
                                const std::string& type_name);
std::string FormatDialogText(const DialogStep& step, const cpptest::TalkTarget& talk,
                             const cpptest::PlayerSave& save);
bool LoadDialogTreeFromJson(const nlohmann::json& root, DialogTree& out,
                            const std::string& source_name);
std::unordered_map<std::string, DialogTree> LoadDialogRegistry(const std::string& dialog_dir);
DialogTree MakeFallbackDialogTree(const std::string& instance_name,
                                 const std::string& type_name);
DialogTree MakeDialogTreeForInstance(const std::string& instance_name,
                                     const std::string& type_name,
                                     const std::unordered_map<std::string, DialogTree>& registry);
[[nodiscard]] bool FindAdjacentTalkTarget(const Vector2& player, const GameMap& m,
                                          const std::string& map_id,
                                          const cpptest::NpcRegistry& registry,
                                          cpptest::PlayerSave& save, cpptest::TalkTarget& out);

[[nodiscard]] bool DialogConditionPasses(const DialogCondition& cond, const cpptest::PlayerSave& save,
                                         const std::string& npc_id);
void ApplyDialogEffects(const std::vector<DialogEffect>& effects, cpptest::PlayerSave& save,
                        const std::string& npc_id);
[[nodiscard]] int FindStepIndexById(const DialogTree& tree, const std::string& id);
[[nodiscard]] int FindNextValidStep(const DialogTree& tree, int start_index,
                                    const cpptest::PlayerSave& save, const std::string& npc_id,
                                    bool entry_only = false);
[[nodiscard]] int ResolveChoiceTarget(const DialogTree& tree, const DialogChoice& choice);
[[nodiscard]] bool OpenDialogOnValidStep(DialogTree& tree, cpptest::PlayerSave& save,
                                         const std::string& npc_id);
enum class DialogAdvance { Stay, Close };
[[nodiscard]] DialogAdvance AdvanceDialogNoChoice(DialogTree& tree, cpptest::PlayerSave& save,
                                                  const std::string& npc_id);
[[nodiscard]] DialogAdvance PickDialogChoice(DialogTree& tree, int choice_index,
                                             cpptest::PlayerSave& save, const std::string& npc_id);
void VisibleDialogChoices(const DialogStep& step, const cpptest::PlayerSave& save,
                          const std::string& npc_id, std::vector<int>& out_indices,
                          std::vector<std::string>& out_labels);
