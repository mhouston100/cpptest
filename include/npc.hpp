#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "game_map.hpp"

namespace cpptest {

struct NpcDef {
  std::string id;
  std::string display_name;
  std::string dialog_key;
  int starting_relationship = 50;
};

struct NpcRegistry {
  std::vector<NpcDef> npcs;
};

// Per-NPC save data. Relationship lives here so it is the same person on every
// floor. Later, optional map_id + cell can override the LDtk pad when NPCs walk.
struct NpcState {
  int relationship = 50;
};

struct PlayerSave {
  std::unordered_map<std::string, bool> flags;
  std::unordered_map<std::string, NpcState> npcs;
};

struct TalkTarget {
  MapEntityKind kind = MapEntityKind::Prop;
  std::string id;
  std::string display_name;
  std::string dialog_key;
  int relationship = 50;
};

[[nodiscard]] int ClampRelationship(int value);
[[nodiscard]] std::string LoadNpcRegistry(const std::string& path, NpcRegistry& out);
[[nodiscard]] const NpcDef* FindNpc(const NpcRegistry& registry, const std::string& id);
void HydrateNpcSave(PlayerSave& save, const NpcRegistry& registry);
[[nodiscard]] PlayerSave LoadPlayerSave();
bool SavePlayerSave(const PlayerSave& save);
[[nodiscard]] TalkTarget ResolveTalkTarget(const MapEntity& entity, const NpcRegistry& registry,
                                           PlayerSave& save);

}  // namespace cpptest
