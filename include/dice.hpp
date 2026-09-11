#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace cpptest {

struct PlayerSave;

constexpr int kDiceCount = 5;
constexpr int kDieFaces = 6;

struct DieDef {
  std::array<std::string, kDieFaces> tags{};
};

struct DiceBag {
  std::vector<DieDef> dice;
};

struct IncidentOutcome {
  int money = 0;
  int stress = 0;
  std::string flag;
};

struct IncidentDef {
  std::string id;
  std::string title;
  std::string prompt;
  std::string skill = "web";
  int target = 16;
  int skill_bonus = 5;
  int minutes = 120;
  int energy = -18;
  int rolls = 3;
  IncidentOutcome success;
  IncidentOutcome fail;
};

struct IncidentPlay {
  IncidentDef def;
  std::array<int, kDiceCount> pips{};
  std::array<bool, kDiceCount> held{};
  int rolls_left = 0;
  bool rolled = false;
  bool resolved = false;
  bool success = false;
  int pip_sum = 0;
  int bonus = 0;
  int score = 0;
  bool tagged = false;
};

void EnsureDiceBag(PlayerSave& save);
[[nodiscard]] std::string DieFaceTag(const DieDef& die, int pips);
[[nodiscard]] std::string LoadIncidentRegistry(const std::string& dir,
                                               std::unordered_map<std::string, IncidentDef>& out);
[[nodiscard]] const IncidentDef* FindIncident(const std::unordered_map<std::string, IncidentDef>& registry,
                                              const std::string& id);
bool BeginIncident(IncidentPlay& play, const IncidentDef& def);
void RollIncidentDice(IncidentPlay& play, const DiceBag& bag);
void ToggleIncidentHold(IncidentPlay& play, int index);
void RefreshIncidentScore(IncidentPlay& play, const DiceBag& bag);
void ResolveIncident(IncidentPlay& play, PlayerSave& save);

}  // namespace cpptest
