#include "dice.hpp"

#include "day.hpp"
#include "npc.hpp"

#include <algorithm>
#include <filesystem>
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

DieDef PlainDie() {
  return DieDef{};
}

DieDef WebDie() {
  DieDef die;
  die.tags[4] = "web";
  die.tags[5] = "web";
  return die;
}

IncidentOutcome ParseOutcome(const json& node) {
  IncidentOutcome out;
  if (!node.is_object()) {
    return out;
  }
  out.money = node.value("money", 0);
  out.stress = node.value("stress", 0);
  out.flag = node.value("flag", std::string{});
  return out;
}

}  // namespace

void EnsureDiceBag(PlayerSave& save) {
  if (static_cast<int>(save.dice.dice.size()) >= kDiceCount) {
    save.dice.dice.resize(static_cast<size_t>(kDiceCount));
    return;
  }
  DiceBag bag;
  bag.dice.push_back(WebDie());
  while (static_cast<int>(bag.dice.size()) < kDiceCount) {
    bag.dice.push_back(PlainDie());
  }
  save.dice = std::move(bag);
}

std::string DieFaceTag(const DieDef& die, int pips) {
  if (pips < 1 || pips > kDieFaces) {
    return {};
  }
  return die.tags[static_cast<size_t>(pips - 1)];
}

std::string LoadIncidentRegistry(const std::string& dir,
                                 std::unordered_map<std::string, IncidentDef>& out) {
  std::unordered_map<std::string, IncidentDef> registry;
  std::filesystem::path dir_path = ResolveDataPath(dir);
  if (!std::filesystem::exists(dir_path) || !std::filesystem::is_directory(dir_path)) {
    return "Could not open incidents directory: " + dir_path.string();
  }

  for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".json") {
      continue;
    }
    std::ifstream in(entry.path());
    if (!in) {
      continue;
    }
    json root;
    try {
      in >> root;
    } catch (const std::exception& e) {
      return std::string("Incident JSON parse error: ") + e.what();
    }
    if (!root.is_object()) {
      continue;
    }
    IncidentDef def;
    def.id = root.value("id", entry.path().stem().string());
    def.title = root.value("title", def.id);
    def.prompt = root.value("prompt", std::string{});
    def.skill = root.value("skill", std::string{"web"});
    def.target = root.value("target", 16);
    def.skill_bonus = root.value("skill_bonus", 5);
    def.minutes = root.value("minutes", 120);
    def.energy = root.value("energy", -18);
    def.rolls = std::max(1, root.value("rolls", 3));
    if (root.contains("success")) {
      def.success = ParseOutcome(root["success"]);
    }
    if (root.contains("fail")) {
      def.fail = ParseOutcome(root["fail"]);
    }
    if (def.id.empty()) {
      return "Incident file missing id: " + entry.path().string();
    }
    registry[def.id] = std::move(def);
  }

  if (registry.empty()) {
    return "No incidents found in " + dir_path.string();
  }
  out = std::move(registry);
  return {};
}

const IncidentDef* FindIncident(const std::unordered_map<std::string, IncidentDef>& registry,
                                const std::string& id) {
  const auto it = registry.find(id);
  if (it == registry.end()) {
    return nullptr;
  }
  return &it->second;
}

bool BeginIncident(IncidentPlay& play, const IncidentDef& def) {
  play = IncidentPlay{};
  play.def = def;
  play.rolls_left = std::max(1, def.rolls);
  return true;
}

void RefreshIncidentScore(IncidentPlay& play, const DiceBag& bag) {
  play.pip_sum = 0;
  play.tagged = false;
  play.bonus = 0;
  play.score = 0;
  if (!play.rolled) {
    return;
  }
  const int n = std::min(kDiceCount, static_cast<int>(bag.dice.size()));
  for (int i = 0; i < n; ++i) {
    play.pip_sum += play.pips[static_cast<size_t>(i)];
    const std::string tag = DieFaceTag(bag.dice[static_cast<size_t>(i)], play.pips[static_cast<size_t>(i)]);
    if (!play.def.skill.empty() && tag == play.def.skill) {
      play.tagged = true;
    }
  }
  if (play.tagged) {
    play.bonus = play.def.skill_bonus;
  }
  play.score = play.pip_sum + play.bonus;
}

void RollIncidentDice(IncidentPlay& play, const DiceBag& bag) {
  if (play.resolved || play.rolls_left <= 0) {
    return;
  }
  const int n = std::min(kDiceCount, static_cast<int>(bag.dice.size()));
  for (int i = 0; i < n; ++i) {
    if (play.rolled && play.held[static_cast<size_t>(i)]) {
      continue;
    }
    play.pips[static_cast<size_t>(i)] = GetRandomValue(1, kDieFaces);
  }
  play.rolled = true;
  play.rolls_left -= 1;
  RefreshIncidentScore(play, bag);
}

void ToggleIncidentHold(IncidentPlay& play, int index) {
  if (play.resolved || !play.rolled || index < 0 || index >= kDiceCount) {
    return;
  }
  play.held[static_cast<size_t>(index)] = !play.held[static_cast<size_t>(index)];
}

void ResolveIncident(IncidentPlay& play, PlayerSave& save) {
  if (play.resolved || !play.rolled) {
    return;
  }
  RefreshIncidentScore(play, save.dice);
  play.success = play.score >= play.def.target;
  play.resolved = true;
  const IncidentOutcome& outcome = play.success ? play.def.success : play.def.fail;
  ApplyDayDelta(save, play.def.minutes, play.def.energy, outcome.stress, outcome.money);
  if (play.success && !outcome.flag.empty()) {
    SetFlag(save, outcome.flag, true);
  }
}

}  // namespace cpptest
