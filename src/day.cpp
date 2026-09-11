#include "day.hpp"

#include <algorithm>
#include <cstdio>

namespace cpptest {
namespace {

void ClearDailyFlags(PlayerSave& save) {
  SetFlag(save, "rack_inspected", false);
  SetFlag(save, "worked_late", false);
}

int MorningEnergy(const PlayerSave& save, const bool stayed_late, const bool very_late,
                  const bool worked_late) {
  int energy = 92;
  if (very_late) {
    energy = 48;
  } else if (stayed_late) {
    energy = 68;
  }
  if (worked_late) {
    energy -= 12;
  }
  energy -= save.stress / 8;
  return ClampMeter(energy);
}

int MorningStress(const PlayerSave& save, const bool very_late) {
  int stress = save.stress - 25;
  if (very_late) {
    stress += 8;
  }
  return ClampMeter(stress);
}

int MorningHealth(const PlayerSave& save, const int morning_energy) {
  int health = save.health;
  if (save.stress >= 70) {
    health -= 5;
  }
  if (morning_energy < 50) {
    health -= 4;
  }
  return ClampMeter(health);
}

}  // namespace

int ClampMeter(int value) {
  return std::clamp(value, 0, 100);
}

int ClampMoney(int value) {
  return std::max(0, value);
}

int ClampDayMinutes(int minutes) {
  return std::clamp(minutes, 0, kLatestMinutes);
}

std::string FormatClock(int minutes) {
  const int wrapped = ((minutes % kMinutesPerDay) + kMinutesPerDay) % kMinutesPerDay;
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%02d:%02d", wrapped / 60, wrapped % 60);
  return buf;
}

bool IsLate(int minutes) {
  return minutes >= kLateWorkMinutes;
}

bool IsVeryLate(int minutes) {
  return minutes >= kVeryLateMinutes;
}

bool DayShouldHintSleep(const PlayerSave& save) {
  return save.minutes >= kSleepHintMinutes;
}

bool DayShouldForceSleep(const PlayerSave& save) {
  return save.minutes >= kLatestMinutes;
}

void EnsureDaySave(PlayerSave& save) {
  if (save.day < 1) {
    save.day = 1;
  }
  save.minutes = ClampDayMinutes(save.minutes);
  save.energy = ClampMeter(save.energy);
  save.stress = ClampMeter(save.stress);
  save.health = ClampMeter(save.health);
  save.money = ClampMoney(save.money);
}

void ApplyDayDelta(PlayerSave& save, int minutes, int energy, int stress, int money, int health) {
  EnsureDaySave(save);
  const int started = save.minutes;
  save.minutes = ClampDayMinutes(save.minutes + std::max(0, minutes));
  if (minutes > 0 && money > 0 && (IsLate(started) || IsLate(save.minutes))) {
    SetFlag(save, "worked_late", true);
  }
  save.energy = ClampMeter(save.energy + energy);
  save.stress = ClampMeter(save.stress + stress);
  save.money = ClampMoney(save.money + money);
  save.health = ClampMeter(save.health + health);
}

SleepSummary MakeSleepSummary(const PlayerSave& save) {
  SleepSummary summary;
  summary.from_day = save.day < 1 ? 1 : save.day;
  summary.next_day = summary.from_day + 1;
  summary.bedtime_minutes = ClampDayMinutes(save.minutes);
  summary.stayed_late = IsLate(summary.bedtime_minutes);
  summary.very_late = IsVeryLate(summary.bedtime_minutes);
  summary.worked_late = GetFlag(save, "worked_late");
  summary.energy_before = ClampMeter(save.energy);
  summary.stress_before = ClampMeter(save.stress);
  summary.health_before = ClampMeter(save.health);
  summary.money = ClampMoney(save.money);
  summary.energy_after =
      MorningEnergy(save, summary.stayed_late, summary.very_late, summary.worked_late);
  summary.stress_after = MorningStress(save, summary.very_late);
  summary.health_after = MorningHealth(save, summary.energy_after);
  return summary;
}

void ApplySleep(PlayerSave& save, const SleepSummary& summary) {
  save.day = summary.next_day < 1 ? 1 : summary.next_day;
  save.minutes = kWakeMinutes;
  save.energy = ClampMeter(summary.energy_after);
  save.stress = ClampMeter(summary.stress_after);
  save.health = ClampMeter(summary.health_after);
  save.money = ClampMoney(summary.money);
  ClearDailyFlags(save);
  EnsureDaySave(save);
}

}  // namespace cpptest
