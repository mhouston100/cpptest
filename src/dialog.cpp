#include "dialog.hpp"

#include <filesystem>
#include <fstream>

// START REMOVE-ALL STUDY NOTES
// This module owns the dialog content pipeline. It reads JSON definitions, keeps a
// registry of dialog trees, and resolves nearby interactables into a specific
// instance or type-based response. This keeps the gameplay loop free of content parsing details.
// END REMOVE-ALL STUDY NOTES

using nlohmann::json;

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
    step.text = step_json["text"].get<std::string>();

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
        choice.next_step = choice_json.value("next_step", -1);
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
  return DialogTree{{
      {type_name + " " + instance_name + ": There's nothing special here.", {}},
      {"Press ESC to close.", {}},
  }, 0};
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

bool GetAdjacentInteractable(const Vector2& player, const GameMap& m, int& out_item,
                            std::string& out_name, std::string& out_type) {
  int cx = 0;
  int cy = 0;
  int tx = 0;
  int ty = 0;
  bool found = false;

  // Reuse the player's cell conversion to determine the current tile underfoot.
  // Then look one tile in each of the four cardinal directions for an interactable.
  constexpr int kDirs[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
  cx = static_cast<int>(std::floor(player.x + static_cast<float>(m.c_wid) * 0.5f - 0.5f + 1e-4f));
  cy = static_cast<int>(std::floor(player.y + static_cast<float>(m.c_hei) * 0.5f - 0.5f + 1e-4f));
  cx = std::clamp(cx, 0, m.c_wid - 1);
  cy = std::clamp(cy, 0, m.c_hei - 1);

  for (const auto& d : kDirs) {
    const int nx = cx + d[0];
    const int ny = cy + d[1];
    if (m.InBounds(nx, ny) && m.HasInteractable(nx, ny)) {
      tx = nx;
      ty = ny;
      found = true;
      break;
    }
  }

  if (!found) {
    return false;
  }

  out_item = m.Interactable(tx, ty);
  out_name = m.InteractableName(tx, ty);
  out_type = m.InteractableType(tx, ty);
  return true;
}
