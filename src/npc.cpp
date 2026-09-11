#include "npc.hpp"

#include "day.hpp"

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

NpcPlace ParsePlace(const json& node) {
  NpcPlace place;
  if (!node.is_object()) {
    return place;
  }
  place.map_id = node.value("map", std::string{});
  if (node.contains("cell") && node["cell"].is_array() && node["cell"].size() >= 2) {
    place.cell_x = node["cell"][0].is_number_integer() ? node["cell"][0].get<int>() : 0;
    place.cell_y = node["cell"][1].is_number_integer() ? node["cell"][1].get<int>() : 0;
  }
  return place;
}

bool SameLocation(const NpcState& state, const std::string& map_id, int cell_x, int cell_y) {
  return state.map_id == map_id && state.cell_x == cell_x && state.cell_y == cell_y;
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
    if (entry_json.contains("work")) {
      def.work = ParsePlace(entry_json["work"]);
    }
    if (entry_json.contains("home")) {
      def.home = ParsePlace(entry_json["home"]);
    }
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
      state.map_id = it.value().value("map_id", std::string{});
      state.cell_x = it.value().value("cell_x", 0);
      state.cell_y = it.value().value("cell_y", 0);
      save.npcs[it.key()] = state;
    }
  }

  save.day = root.value("day", 1);
  save.minutes = root.value("minutes", kWakeMinutes);
  save.energy = root.value("energy", 80);
  save.stress = root.value("stress", 15);
  save.money = root.value("money", 50);
  save.health = root.value("health", 80);
  if (root.contains("dice") && root["dice"].is_array()) {
    save.dice.dice.clear();
    for (const auto& die_json : root["dice"]) {
      DieDef die;
      if (die_json.is_object() && die_json.contains("tags") && die_json["tags"].is_array()) {
        const auto& tags = die_json["tags"];
        for (int i = 0; i < kDieFaces && i < static_cast<int>(tags.size()); ++i) {
          if (tags[static_cast<size_t>(i)].is_string()) {
            die.tags[static_cast<size_t>(i)] = tags[static_cast<size_t>(i)].get<std::string>();
          }
        }
      }
      save.dice.dice.push_back(die);
    }
  }
  EnsureDaySave(save);
  EnsureDiceBag(save);
  return save;
}

bool SavePlayerSave(const PlayerSave& save) {
  json flags = json::object();
  for (const auto& [key, value] : save.flags) {
    flags[key] = value;
  }
  json npcs = json::object();
  for (const auto& [id, state] : save.npcs) {
    json npc = {{"relationship", ClampRelationship(state.relationship)}};
    if (!state.map_id.empty()) {
      npc["map_id"] = state.map_id;
      npc["cell_x"] = state.cell_x;
      npc["cell_y"] = state.cell_y;
    }
    npcs[id] = std::move(npc);
  }
  json dice = json::array();
  for (const auto& die : save.dice.dice) {
    json tags = json::array();
    for (const auto& tag : die.tags) {
      tags.push_back(tag);
    }
    dice.push_back({{"tags", tags}});
  }
  const json root = {{"flags", flags},
                    {"npcs", npcs},
                    {"day", save.day},
                    {"minutes", save.minutes},
                    {"energy", save.energy},
                    {"stress", save.stress},
                    {"money", save.money},
                    {"health", save.health},
                    {"dice", dice}};
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

TalkTarget ResolveNpcTalkTarget(const std::string& npc_id, const NpcRegistry& registry,
                                PlayerSave& save) {
  TalkTarget target;
  target.kind = MapEntityKind::Npc;
  target.id = npc_id.empty() ? std::string{"Npc"} : npc_id;
  const NpcDef* def = FindNpc(registry, target.id);
  const int starting = def != nullptr ? def->starting_relationship : 50;
  const NpcState& state = EnsureNpcState(save, target.id, starting);
  if (def != nullptr) {
    target.display_name = def->display_name;
    target.dialog_key = def->dialog_key;
  } else {
    target.display_name = target.id;
    target.dialog_key = target.id;
  }
  target.relationship = state.relationship;
  return target;
}

TalkTarget ResolveTalkTarget(const MapEntity& entity, const NpcRegistry& registry, PlayerSave& save) {
  if (entity.kind == MapEntityKind::Npc) {
    TalkTarget target = ResolveNpcTalkTarget(entity.npc_id, registry, save);
    if (!entity.dialog_key.empty()) {
      target.dialog_key = entity.dialog_key;
    }
    return target;
  }

  TalkTarget target;
  target.kind = entity.kind;
  std::string name;
  std::string type;
  TalkIdentity(entity, name, type);
  target.id = name;
  target.display_name = name;
  target.dialog_key = type;
  return target;
}

bool NpcHasWorldLocation(const NpcState& state) {
  return !state.map_id.empty();
}

void SetNpcLocation(PlayerSave& save, const std::string& npc_id, const std::string& map_id, int cell_x,
                    int cell_y) {
  if (npc_id.empty() || map_id.empty()) {
    return;
  }
  NpcState& state = EnsureNpcState(save, npc_id, 50);
  if (SameLocation(state, map_id, cell_x, cell_y)) {
    return;
  }
  state.map_id = map_id;
  state.cell_x = cell_x;
  state.cell_y = cell_y;
}

void CaptureNpcPads(const GameMap& map, const std::string& map_id, const NpcRegistry& registry,
                    PlayerSave& save) {
  if (map_id.empty()) {
    return;
  }
  for (const auto& entity : map.entities) {
    if (entity.kind != MapEntityKind::Npc || entity.npc_id.empty()) {
      continue;
    }
    const NpcDef* def = FindNpc(registry, entity.npc_id);
    const int starting = def != nullptr ? def->starting_relationship : 50;
    NpcState& state = EnsureNpcState(save, entity.npc_id, starting);
    if (!NpcHasWorldLocation(state)) {
      state.map_id = map_id;
      state.cell_x = entity.cell_x;
      state.cell_y = entity.cell_y;
    }
  }
}

void SyncNpcSchedules(PlayerSave& save, const NpcRegistry& registry) {
  const bool go_home = IsLate(save.minutes);
  for (const auto& def : registry.npcs) {
    const NpcPlace& place = go_home && !def.home.map_id.empty() ? def.home : def.work;
    if (place.map_id.empty()) {
      continue;
    }
    SetNpcLocation(save, def.id, place.map_id, place.cell_x, place.cell_y);
  }
}

void CollectNpcsOnMap(const GameMap& map, const std::string& map_id, const NpcRegistry& registry,
                      const PlayerSave& save, std::vector<WorldNpcPose>& out) {
  out.clear();
  std::vector<std::string> placed;
  auto already = [&](const std::string& id) {
    return std::find(placed.begin(), placed.end(), id) != placed.end();
  };

  for (const auto& def : registry.npcs) {
    const auto it = save.npcs.find(def.id);
    if (it == save.npcs.end() || !NpcHasWorldLocation(it->second)) {
      continue;
    }
    placed.push_back(def.id);
    if (it->second.map_id == map_id) {
      out.push_back(WorldNpcPose{def.id, it->second.cell_x, it->second.cell_y});
    }
  }

  for (const auto& [id, state] : save.npcs) {
    if (!NpcHasWorldLocation(state) || already(id)) {
      continue;
    }
    placed.push_back(id);
    if (state.map_id == map_id) {
      out.push_back(WorldNpcPose{id, state.cell_x, state.cell_y});
    }
  }

  for (const auto& entity : map.entities) {
    if (entity.kind != MapEntityKind::Npc || entity.npc_id.empty() || already(entity.npc_id)) {
      continue;
    }
    placed.push_back(entity.npc_id);
    out.push_back(WorldNpcPose{entity.npc_id, entity.cell_x, entity.cell_y});
  }
}

}  // namespace cpptest
