#pragma once

namespace cpptest {

enum class GameMode { Playing, Dialog, Menu, Fading, Sleep, Incident };

[[nodiscard]] constexpr const char* GameModeName(const GameMode mode) {
  switch (mode) {
    case GameMode::Playing:
      return "Playing";
    case GameMode::Dialog:
      return "Dialog";
    case GameMode::Menu:
      return "Menu";
    case GameMode::Fading:
      return "Fading";
    case GameMode::Sleep:
      return "Sleep";
    case GameMode::Incident:
      return "Incident";
  }
  return "Playing";
}

[[nodiscard]] constexpr GameMode ResolveGameMode(const bool fading, const bool in_dialog,
                                                const bool in_menu = false,
                                                const bool sleeping = false,
                                                const bool in_incident = false) {
  if (fading) {
    return GameMode::Fading;
  }
  if (in_menu) {
    return GameMode::Menu;
  }
  if (sleeping) {
    return GameMode::Sleep;
  }
  if (in_incident) {
    return GameMode::Incident;
  }
  if (in_dialog) {
    return GameMode::Dialog;
  }
  return GameMode::Playing;
}

}  // namespace cpptest
