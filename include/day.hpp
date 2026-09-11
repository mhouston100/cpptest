#pragma once

#include <string>

#include "npc.hpp"

namespace cpptest {

constexpr int kMinutesPerDay = 24 * 60;
constexpr int kWakeMinutes = 8 * 60;
constexpr int kLateWorkMinutes = 18 * 60;
constexpr int kVeryLateMinutes = 22 * 60;
constexpr int kSleepHintMinutes = 20 * 60;
constexpr int kLatestMinutes = 26 * 60;

constexpr int kTalkMinutes = 15;
constexpr int kTalkEnergy = -2;
constexpr int kTravelMinutes = 30;
constexpr int kTravelEnergy = -4;

struct SleepSummary {
  int from_day = 1;
  int next_day = 2;
  int bedtime_minutes = kWakeMinutes;
  bool stayed_late = false;
  bool very_late = false;
  bool worked_late = false;
  int energy_before = 0;
  int energy_after = 0;
  int stress_before = 0;
  int stress_after = 0;
  int health_before = 0;
  int health_after = 0;
  int money = 0;
};

[[nodiscard]] int ClampMeter(int value);
[[nodiscard]] int ClampMoney(int value);
[[nodiscard]] int ClampDayMinutes(int minutes);
[[nodiscard]] std::string FormatClock(int minutes);
[[nodiscard]] bool IsLate(int minutes);
[[nodiscard]] bool IsVeryLate(int minutes);
[[nodiscard]] bool DayShouldHintSleep(const PlayerSave& save);
[[nodiscard]] bool DayShouldForceSleep(const PlayerSave& save);
void EnsureDaySave(PlayerSave& save);
void ApplyDayDelta(PlayerSave& save, int minutes, int energy, int stress, int money,
                   int health = 0);
[[nodiscard]] SleepSummary MakeSleepSummary(const PlayerSave& save);
void ApplySleep(PlayerSave& save, const SleepSummary& summary);

}  // namespace cpptest
