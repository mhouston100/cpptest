#include "dialog.hpp"

#include "day.hpp"
#include "job.hpp"

#include <filesystem>
#include <fstream>

// START REMOVE-ALL STUDY NOTES
// This module owns the dialog content pipeline. It reads JSON definitions, keeps a
// registry of dialog trees, and resolves nearby interactables into a specific
// instance or type-based response. This keeps the gameplay loop free of content parsing details.
// END REMOVE-ALL STUDY NOTES

using nlohmann::json;
using cpptest::ApplyForJob;
using cpptest::Course;
using cpptest::FindCourse;
using cpptest::FindJobListing;
using cpptest::JobListing;
using cpptest::TakeCourse;
using cpptest::ApplyDayDelta;
using cpptest::CollectNpcsOnMap;
using cpptest::GetFlag;
using cpptest::GetRelationship;
using cpptest::NpcRegistry;
using cpptest::PlayerSave;
using cpptest::ResolveNpcTalkTarget;
using cpptest::SetFlag;
using cpptest::TalkTarget;
using cpptest::WorldNpcPose;

namespace {

DialogCondition ParseCondition(const json& node) {
  DialogCondition cond;
  if (!node.is_object()) {
    return cond;
  }
  cond.flag = node.value("flag", std::string{});
  if (node.contains("equals") && node["equals"].is_boolean()) {
    cond.flag_equals = node["equals"].get<bool>();
  }
  if (node.contains("relationship_min") && node["relationship_min"].is_number_integer()) {
    cond.relationship_min = node["relationship_min"].get<int>();
  }
  if (node.contains("relationship_max") && node["relationship_max"].is_number_integer()) {
    cond.relationship_max = node["relationship_max"].get<int>();
  }
  if (node.contains("money_min") && node["money_min"].is_number_integer()) {
    cond.money_min = node["money_min"].get<int>();
  }
  auto parse_flags = [](const json& arr, std::vector<std::string>& out) {
    if (!arr.is_array()) {
      return;
    }
    for (const auto& item : arr) {
      if (item.is_string()) {
        out.push_back(item.get<std::string>());
      }
    }
  };
  if (node.contains("flags_on")) {
    parse_flags(node["flags_on"], cond.flags_on);
  }
  if (node.contains("flags_off")) {
    parse_flags(node["flags_off"], cond.flags_off);
  }
  return cond;
}

std::vector<DialogEffect> ParseEffects(const json& node) {
  std::vector<DialogEffect> effects;
  if (!node.is_array()) {
    return effects;
  }
  for (const auto& item : node) {
    if (!item.is_object()) {
      continue;
    }
    DialogEffect effect;
    effect.set_flag = item.value("set_flag", std::string{});
    if (item.contains("value") && item["value"].is_boolean()) {
      effect.flag_value = item["value"].get<bool>();
    }
    if (item.contains("relationship") && item["relationship"].is_number_integer()) {
      effect.relationship_delta = item["relationship"].get<int>();
    }
    effect.minutes = item.value("minutes", 0);
    effect.energy = item.value("energy", 0);
    effect.stress = item.value("stress", 0);
    effect.money = item.value("money", 0);
    effect.health = item.value("health", 0);
    effect.start_incident = item.value("incident", std::string{});
    effect.apply_job = item.value("apply_job", std::string{});
    effect.take_course = item.value("take_course", std::string{});
    effects.push_back(effect);
  }
  return effects;
}

void ParseNextStep(const json& node, DialogChoice& choice) {
  if (!node.contains("next_step")) {
    return;
  }
  const auto& next = node["next_step"];
  if (next.is_number_integer()) {
    choice.next_step = next.get<int>();
  } else if (next.is_string()) {
    choice.next_id = next.get<std::string>();
  }
}

}  // namespace

std::string ReplaceDialogTokens(std::string text, const std::string& instance_name,
                                const std::string& type_name) {
  auto replace_all = [&](const std::string& token, const std::string& value) {
    size_t pos = 0;
    while ((pos = text.find(token, pos)) != std::string::npos) {
      text.replace(pos, token.size(), value);
      pos += value.size();
    }
  };
  replace_all("${instance}", instance_name);
  replace_all("${type}", type_name);
  replace_all("${name}", instance_name);
  return text;
}

std::string FormatDialogText(const DialogStep& step, const TalkTarget& talk, const PlayerSave& save) {
  std::string text = ReplaceDialogTokens(step.text, talk.display_name, talk.dialog_key);
  const int rel = talk.kind == MapEntityKind::Npc ? GetRelationship(save, talk.id) : talk.relationship;
  auto replace_all = [&](const std::string& token, const std::string& value) {
    size_t pos = 0;
    while ((pos = text.find(token, pos)) != std::string::npos) {
      text.replace(pos, token.size(), value);
      pos += value.size();
    }
  };
  replace_all("${relationship}", std::to_string(rel));
  return text;
}

bool LoadDialogTreeFromJson(const nlohmann::json& root, DialogTree& out, const std::string& source_name) {
  if (!root.contains("steps") || !root["steps"].is_array()) {
    TraceLog(LOG_WARNING, "Dialog file %s missing steps array", source_name.c_str());
    return false;
  }

  out.steps.clear();
  for (const auto& step_json : root["steps"]) {
    if (!step_json.is_object()) {
      TraceLog(LOG_WARNING, "Dialog step in %s is invalid", source_name.c_str());
      return false;
    }

    if (!step_json.contains("text") || !step_json["text"].is_string()) {
      TraceLog(LOG_WARNING, "Dialog step in %s missing text", source_name.c_str());
      return false;
    }

    DialogStep step;
    step.id = step_json.value("id", std::string{});
    step.text = step_json["text"].get<std::string>();
    step.entry = step_json.value("entry", true);
    if (step_json.contains("cond")) {
      step.cond = ParseCondition(step_json["cond"]);
    }
    if (step_json.contains("effects")) {
      step.effects = ParseEffects(step_json["effects"]);
    }

    if (step_json.contains("choices")) {
      if (!step_json["choices"].is_array()) {
        TraceLog(LOG_WARNING, "Choices array in %s is invalid", source_name.c_str());
        return false;
      }
      for (const auto& choice_json : step_json["choices"]) {
        if (!choice_json.is_object() || !choice_json.contains("label") ||
            !choice_json["label"].is_string()) {
          TraceLog(LOG_WARNING, "Dialog choice in %s is invalid", source_name.c_str());
          return false;
        }
        DialogChoice choice;
        choice.label = choice_json["label"].get<std::string>();
        ParseNextStep(choice_json, choice);
        if (choice_json.contains("cond")) {
          choice.cond = ParseCondition(choice_json["cond"]);
        }
        if (choice_json.contains("effects")) {
          choice.effects = ParseEffects(choice_json["effects"]);
        }
        step.choices.push_back(std::move(choice));
      }
    }

    out.steps.push_back(std::move(step));
  }

  out.current_step = 0;
  return !out.steps.empty();
}

std::unordered_map<std::string, DialogTree> LoadDialogRegistry(const std::string& dialog_dir) {
  std::unordered_map<std::string, DialogTree> registry;
  std::filesystem::path dir_path(dialog_dir);

  if (!std::filesystem::exists(dir_path) || !std::filesystem::is_directory(dir_path)) {
    const std::filesystem::path alternate_path = dir_path.parent_path() / dir_path.filename();
    if (std::filesystem::exists(alternate_path) && std::filesystem::is_directory(alternate_path)) {
      dir_path = alternate_path;
    } else {
      TraceLog(LOG_WARNING, "Dialog directory not found: %s", dialog_dir.c_str());
      return registry;
    }
  }

  for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".json") {
      continue;
    }

    std::ifstream in(entry.path());
    if (!in) {
      TraceLog(LOG_WARNING, "Could not open dialog file: %s", entry.path().c_str());
      continue;
    }

    json root;
    try {
      in >> root;
    } catch (const std::exception& e) {
      TraceLog(LOG_WARNING, "JSON parse error in %s: %s", entry.path().c_str(), e.what());
      continue;
    }

    const std::string type_name = entry.path().stem().string();
    if (root.is_object() && root.contains("steps")) {
      DialogTree tree;
      if (!LoadDialogTreeFromJson(root, tree, entry.path().string())) {
        continue;
      }
      registry[type_name] = std::move(tree);
      TraceLog(LOG_INFO, "Loaded dialog for type '%s' from %s", type_name.c_str(), entry.path().c_str());
    } else if (root.is_object()) {
      for (auto it = root.begin(); it != root.end(); ++it) {
        const std::string key = it.key();
        const json& val = it.value();
        DialogTree tree;
        if (!LoadDialogTreeFromJson(val, tree, entry.path().string() + ":" + key)) {
          TraceLog(LOG_WARNING, "Skipping dialog entry %s in %s", key.c_str(), entry.path().c_str());
          continue;
        }
        registry[key] = tree;
        TraceLog(LOG_INFO, "Loaded dialog key '%s' from %s", key.c_str(), entry.path().c_str());
        if (key == "default" || key == type_name) {
          registry[type_name] = tree;
          TraceLog(LOG_INFO, "Registered type fallback '%s' from %s", type_name.c_str(), entry.path().c_str());
        }
      }
    } else {
      TraceLog(LOG_WARNING, "Dialog file %s has unexpected root type", entry.path().c_str());
    }
  }

  return registry;
}

DialogTree MakeFallbackDialogTree(const std::string& instance_name, const std::string& type_name) {
  DialogTree tree;
  DialogStep first;
  first.text = type_name + " " + instance_name + ": There's nothing special here.";
  DialogStep second;
  second.text = "Press ESC to close.";
  tree.steps.push_back(std::move(first));
  tree.steps.push_back(std::move(second));
  tree.current_step = 0;
  return tree;
}

DialogTree MakeDialogTreeForInstance(const std::string& instance_name, const std::string& type_name,
                                     const std::unordered_map<std::string, DialogTree>& registry) {
  auto it_inst = registry.find(instance_name);
  if (it_inst != registry.end()) {
    TraceLog(LOG_INFO, "Dialog lookup: matched instance '%s'", instance_name.c_str());
    DialogTree dialog = it_inst->second;
    for (auto& step : dialog.steps) {
      step.text = ReplaceDialogTokens(step.text, instance_name, type_name);
    }
    return dialog;
  }
  auto it_type = registry.find(type_name);
  if (it_type != registry.end()) {
    TraceLog(LOG_INFO, "Dialog lookup: matched type '%s' for instance '%s'", type_name.c_str(), instance_name.c_str());
    DialogTree dialog = it_type->second;
    for (auto& step : dialog.steps) {
      step.text = ReplaceDialogTokens(step.text, instance_name, type_name);
    }
    return dialog;
  }

  TraceLog(LOG_INFO, "Dialog lookup: no match for instance '%s' (type '%s'), using fallback", instance_name.c_str(), type_name.c_str());
  DialogTree dialog = MakeFallbackDialogTree(instance_name, type_name);
  for (auto& step : dialog.steps) {
    step.text = ReplaceDialogTokens(step.text, instance_name, type_name);
  }
  return dialog;
}

bool FindAdjacentTalkTarget(const Vector2& player, const GameMap& m, const std::string& map_id,
                            const NpcRegistry& registry, PlayerSave& save, TalkTarget& out) {
  int cx = 0;
  int cy = 0;
  m.WorldToCell(player.x, player.y, cx, cy);

  std::vector<WorldNpcPose> poses;
  CollectNpcsOnMap(m, map_id, registry, save, poses);

  constexpr int kDirs[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
  for (const auto& d : kDirs) {
    const int nx = cx + d[0];
    const int ny = cy + d[1];
    for (const auto& pose : poses) {
      if (pose.cell_x == nx && pose.cell_y == ny) {
        out = ResolveNpcTalkTarget(pose.id, registry, save);
        return true;
      }
    }
    const MapEntity* entity = m.FindAt(nx, ny);
    if (entity != nullptr && entity->kind == MapEntityKind::Prop) {
      out = ResolveTalkTarget(*entity, registry, save);
      return true;
    }
  }
  return false;
}

bool DialogConditionPasses(const DialogCondition& cond, const PlayerSave& save,
                           const std::string& npc_id) {
  if (!cond.flag.empty() && GetFlag(save, cond.flag) != cond.flag_equals) {
    return false;
  }
  if (cond.relationship_min >= 0 && GetRelationship(save, npc_id) < cond.relationship_min) {
    return false;
  }
  if (cond.relationship_max >= 0 && GetRelationship(save, npc_id) > cond.relationship_max) {
    return false;
  }
  if (cond.money_min >= 0 && save.money < cond.money_min) {
    return false;
  }
  for (const auto& flag : cond.flags_on) {
    if (!GetFlag(save, flag)) {
      return false;
    }
  }
  for (const auto& flag : cond.flags_off) {
    if (GetFlag(save, flag)) {
      return false;
    }
  }
  return true;
}

void ApplyDialogEffects(const std::vector<DialogEffect>& effects, PlayerSave& save,
                        const std::string& npc_id) {
  for (const auto& effect : effects) {
    if (!effect.set_flag.empty()) {
      SetFlag(save, effect.set_flag, effect.flag_value);
    }
    if (effect.relationship_delta != 0) {
      AddRelationship(save, npc_id, effect.relationship_delta);
    }
    if (effect.minutes != 0 || effect.energy != 0 || effect.stress != 0 || effect.money != 0 ||
        effect.health != 0) {
      ApplyDayDelta(save, effect.minutes, effect.energy, effect.stress, effect.money, effect.health);
    }
    if (!effect.start_incident.empty()) {
      save.queued_incident = effect.start_incident;
    }
    if (!effect.apply_job.empty()) {
      if (const JobListing* job = FindJobListing(effect.apply_job)) {
        static_cast<void>(ApplyForJob(save, *job));
      }
    }
    if (!effect.take_course.empty()) {
      if (const Course* course = FindCourse(effect.take_course)) {
        static_cast<void>(TakeCourse(save, *course));
      }
    }
  }
}

int FindStepIndexById(const DialogTree& tree, const std::string& id) {
  if (id.empty()) {
    return -1;
  }
  for (int i = 0; i < static_cast<int>(tree.steps.size()); ++i) {
    if (tree.steps[static_cast<size_t>(i)].id == id) {
      return i;
    }
  }
  return -1;
}

int FindNextValidStep(const DialogTree& tree, int start_index, const PlayerSave& save,
                      const std::string& npc_id, bool entry_only) {
  if (start_index < 0) {
    start_index = 0;
  }
  for (int i = start_index; i < static_cast<int>(tree.steps.size()); ++i) {
    const DialogStep& step = tree.steps[static_cast<size_t>(i)];
    if (entry_only && !step.entry) {
      continue;
    }
    if (DialogConditionPasses(step.cond, save, npc_id)) {
      return i;
    }
  }
  return -1;
}

int ResolveChoiceTarget(const DialogTree& tree, const DialogChoice& choice) {
  if (!choice.next_id.empty()) {
    return FindStepIndexById(tree, choice.next_id);
  }
  return choice.next_step;
}

namespace {

void EnterStep(DialogTree& tree, int index, PlayerSave& save, const std::string& npc_id) {
  tree.current_step = index;
  if (index >= 0 && index < static_cast<int>(tree.steps.size())) {
    ApplyDialogEffects(tree.steps[static_cast<size_t>(index)].effects, save, npc_id);
  }
}

}  // namespace

bool OpenDialogOnValidStep(DialogTree& tree, PlayerSave& save, const std::string& npc_id) {
  int index = FindNextValidStep(tree, 0, save, npc_id, true);
  if (index < 0) {
    index = FindNextValidStep(tree, 0, save, npc_id, false);
  }
  if (index < 0) {
    return false;
  }
  EnterStep(tree, index, save, npc_id);
  return true;
}

DialogAdvance AdvanceDialogNoChoice(DialogTree& tree, PlayerSave& save, const std::string& npc_id) {
  if (tree.current_step >= 0 && tree.current_step < static_cast<int>(tree.steps.size())) {
    const DialogStep& step = tree.steps[static_cast<size_t>(tree.current_step)];
    if (step.choices.empty() && !step.entry) {
      return DialogAdvance::Close;
    }
  }
  const int next = FindNextValidStep(tree, tree.current_step + 1, save, npc_id, false);
  if (next < 0) {
    return DialogAdvance::Close;
  }
  EnterStep(tree, next, save, npc_id);
  return DialogAdvance::Stay;
}

DialogAdvance PickDialogChoice(DialogTree& tree, int choice_index, PlayerSave& save,
                               const std::string& npc_id) {
  if (tree.current_step < 0 || tree.current_step >= static_cast<int>(tree.steps.size())) {
    return DialogAdvance::Close;
  }
  const DialogStep& step = tree.steps[static_cast<size_t>(tree.current_step)];
  if (choice_index < 0 || choice_index >= static_cast<int>(step.choices.size())) {
    return DialogAdvance::Close;
  }
  const DialogChoice& choice = step.choices[static_cast<size_t>(choice_index)];
  ApplyDialogEffects(choice.effects, save, npc_id);
  int dest = ResolveChoiceTarget(tree, choice);
  if (dest >= 0 && dest < static_cast<int>(tree.steps.size())) {
    if (!DialogConditionPasses(tree.steps[static_cast<size_t>(dest)].cond, save, npc_id)) {
      dest = FindNextValidStep(tree, dest, save, npc_id, false);
    }
  } else {
    dest = -1;
  }
  if (dest < 0) {
    return DialogAdvance::Close;
  }
  EnterStep(tree, dest, save, npc_id);
  return DialogAdvance::Stay;
}

void VisibleDialogChoices(const DialogStep& step, const PlayerSave& save, const std::string& npc_id,
                          std::vector<int>& out_indices, std::vector<std::string>& out_labels) {
  out_indices.clear();
  out_labels.clear();
  for (int i = 0; i < static_cast<int>(step.choices.size()); ++i) {
    const DialogChoice& choice = step.choices[static_cast<size_t>(i)];
    if (!DialogConditionPasses(choice.cond, save, npc_id)) {
      continue;
    }
    out_indices.push_back(i);
    out_labels.push_back(choice.label);
  }
}
