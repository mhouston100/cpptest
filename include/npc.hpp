#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "game_map.hpp"
#include "dice.hpp"

namespace cpptest {

struct NpcPlace {
  std::string map_id;
  int cell_x = 0;
  int cell_y = 0;
};

struct NpcDef {
  std::string id;
  std::string display_name;
  std::string dialog_key;
  int starting_relationship = 50;
  NpcPlace work;
  NpcPlace home;
};

struct NpcRegistry {
  std::vector<NpcDef> npcs;
};

// Per-NPC save data. Relationship and live location live here so it is the same
// person on every floor. map_id empty means "use the LDtk pad on the current map."
struct NpcState {
  int relationship = 50;
  std::string map_id;
  int cell_x = 0;
  int cell_y = 0;
};

struct WorldNpcPose {
  std::string id;
  int cell_x = 0;
  int cell_y = 0;
};

struct PlayerSave {
  std::unordered_map<std::string, bool> flags;
  std::unordered_map<std::string, NpcState> npcs;
  int day = 1;
  int minutes = 8 * 60;
  int energy = 80;
  int stress = 15;
  int money = 50;
  int health = 80;
  DiceBag dice;
  std::string queued_incident;
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
[[nodiscard]] TalkTarget ResolveNpcTalkTarget(const std::string& npc_id, const NpcRegistry& registry,
                                              PlayerSave& save);
[[nodiscard]] bool NpcHasWorldLocation(const NpcState& state);
void SetNpcLocation(PlayerSave& save, const std::string& npc_id, const std::string& map_id, int cell_x,
                    int cell_y);
void CaptureNpcPads(const GameMap& map, const std::string& map_id, const NpcRegistry& registry,
                    PlayerSave& save);
void SyncNpcSchedules(PlayerSave& save, const NpcRegistry& registry);
void CollectNpcsOnMap(const GameMap& map, const std::string& map_id, const NpcRegistry& registry,
                      const PlayerSave& save, std::vector<WorldNpcPose>& out);
void AddRelationship(PlayerSave& save, const std::string& npc_id, int delta);
[[nodiscard]] bool GetFlag(const PlayerSave& save, const std::string& flag);
void SetFlag(PlayerSave& save, const std::string& flag, bool value);
[[nodiscard]] int GetRelationship(const PlayerSave& save, const std::string& npc_id);

}  // namespace cpptest
