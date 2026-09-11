#include "npc.hpp"

#include <algorithm>
#include <fstream>

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

std::string SavePathNextToBinary() {
  return JoinPath(GetApplicationDirectory(), "save.json");
}

NpcState& EnsureNpcState(PlayerSave& save, const std::string& id, int starting_relationship) {
  auto it = save.npcs.find(id);
  if (it == save.npcs.end()) {
    NpcState state;
    state.relationship = ClampRelationship(starting_relationship);
    it = save.npcs.emplace(id, state).first;
  }
  it->second.relationship = ClampRelationship(it->second.relationship);
  return it->second;
}

}  // namespace

int ClampRelationship(int value) {
  return std::clamp(value, 0, 100);
}

std::string LoadNpcRegistry(const std::string& path, NpcRegistry& out) {
  NpcRegistry registry{};
  const std::string resolved = ResolveDataPath(path);
  std::ifstream in(resolved);
  if (!in) {
    return "Could not open NPC registry: " + resolved;
  }

  json root;
  try {
    in >> root;
  } catch (const std::exception& e) {
    return std::string("NPC registry JSON parse error: ") + e.what();
  }

  if (!root.is_object() || !root.contains("npcs") || !root["npcs"].is_array()) {
    return "NPC registry missing \"npcs\" array";
  }

  for (const auto& entry_json : root["npcs"]) {
    if (!entry_json.is_object()) {
      continue;
    }
    NpcDef def;
    def.id = entry_json.value("id", std::string{});
    def.display_name = entry_json.value("display_name", std::string{});
    def.dialog_key = entry_json.value("dialog_key", std::string{});
    def.starting_relationship = ClampRelationship(entry_json.value("relationship", 50));
    if (def.id.empty()) {
      return "NPC registry entry needs id";
    }
    if (def.display_name.empty()) {
      def.display_name = def.id;
    }
    if (def.dialog_key.empty()) {
      def.dialog_key = def.id;
    }
    registry.npcs.push_back(std::move(def));
  }

  if (registry.npcs.empty()) {
    return "NPC registry has no npcs";
  }

  out = std::move(registry);
  return {};
}

const NpcDef* FindNpc(const NpcRegistry& registry, const std::string& id) {
  if (id.empty()) {
    return nullptr;
  }
  for (const auto& def : registry.npcs) {
    if (def.id == id) {
      return &def;
    }
  }
  return nullptr;
}

void HydrateNpcSave(PlayerSave& save, const NpcRegistry& registry) {
  for (const auto& def : registry.npcs) {
    EnsureNpcState(save, def.id, def.starting_relationship);
  }
}

PlayerSave LoadPlayerSave() {
  PlayerSave save{};
  std::string path = SavePathNextToBinary();
  if (!FileExists(path.c_str())) {
    path = "save.json";
  }
  if (!FileExists(path.c_str())) {
    return save;
  }

  std::ifstream in(path);
  if (!in) {
    return save;
  }

  json root;
  try {
    in >> root;
  } catch (const json::exception&) {
    TraceLog(LOG_WARNING, "Save: ignoring invalid JSON in %s", path.c_str());
    return save;
  }
  if (!root.is_object()) {
    return save;
  }

  if (root.contains("flags") && root["flags"].is_object()) {
    for (auto it = root["flags"].begin(); it != root["flags"].end(); ++it) {
      if (it.value().is_boolean()) {
        save.flags[it.key()] = it.value().get<bool>();
      }
    }
  }

  if (root.contains("npcs") && root["npcs"].is_object()) {
    for (auto it = root["npcs"].begin(); it != root["npcs"].end(); ++it) {
      if (!it.value().is_object()) {
        continue;
      }
      NpcState state;
      state.relationship = ClampRelationship(it.value().value("relationship", 50));
      save.npcs[it.key()] = state;
    }
  }

  return save;
}

bool SavePlayerSave(const PlayerSave& save) {
  json flags = json::object();
  for (const auto& [key, value] : save.flags) {
    flags[key] = value;
  }
  json npcs = json::object();
  for (const auto& [id, state] : save.npcs) {
    npcs[id] = {{"relationship", ClampRelationship(state.relationship)}};
  }
  const json root = {{"flags", flags}, {"npcs", npcs}};
  const std::string payload = root.dump(2);

  const std::string paths[] = {SavePathNextToBinary(), std::string{"save.json"}};
  for (const std::string& path : paths) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
      continue;
    }
    out << payload << '\n';
    if (out) {
      return true;
    }
  }
  TraceLog(LOG_WARNING, "Save: could not write save.json");
  return false;
}

void AddRelationship(PlayerSave& save, const std::string& npc_id, int delta) {
  if (npc_id.empty() || delta == 0) {
    return;
  }
  NpcState& state = EnsureNpcState(save, npc_id, 50);
  state.relationship = ClampRelationship(state.relationship + delta);
}

bool GetFlag(const PlayerSave& save, const std::string& flag) {
  if (flag.empty()) {
    return false;
  }
  const auto it = save.flags.find(flag);
  return it != save.flags.end() && it->second;
}

void SetFlag(PlayerSave& save, const std::string& flag, bool value) {
  if (flag.empty()) {
    return;
  }
  save.flags[flag] = value;
}

int GetRelationship(const PlayerSave& save, const std::string& npc_id) {
  const auto it = save.npcs.find(npc_id);
  if (it == save.npcs.end()) {
    return 50;
  }
  return ClampRelationship(it->second.relationship);
}

TalkTarget ResolveTalkTarget(const MapEntity& entity, const NpcRegistry& registry, PlayerSave& save) {
  TalkTarget target;
  target.kind = entity.kind;
  if (entity.kind == MapEntityKind::Npc) {
    target.id = entity.npc_id;
    const NpcDef* def = FindNpc(registry, entity.npc_id);
    const int starting = def != nullptr ? def->starting_relationship : 50;
    const NpcState& state = EnsureNpcState(save, entity.npc_id.empty() ? std::string{"Npc"} : entity.npc_id,
                                          starting);
    if (def != nullptr) {
      target.display_name = def->display_name;
      target.dialog_key = !entity.dialog_key.empty() ? entity.dialog_key : def->dialog_key;
    } else {
      target.display_name = !entity.npc_id.empty() ? entity.npc_id : std::string{"Npc"};
      target.dialog_key = !entity.dialog_key.empty() ? entity.dialog_key : target.display_name;
    }
    target.relationship = state.relationship;
    return target;
  }

  std::string name;
  std::string type;
  TalkIdentity(entity, name, type);
  target.id = name;
  target.display_name = name;
  target.dialog_key = type;
  return target;
}

}  // namespace cpptest
