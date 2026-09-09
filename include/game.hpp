#pragma once

namespace cpptest {

enum class GameMode { Playing, Dialog, Menu, Fading };

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
  }
  return "Playing";
}

[[nodiscard]] constexpr GameMode ResolveGameMode(const bool fading, const bool in_dialog,
                                                const bool in_menu = false) {
  if (fading) {
    return GameMode::Fading;
  }
  if (in_menu) {
    return GameMode::Menu;
  }
  if (in_dialog) {
    return GameMode::Dialog;
  }
  return GameMode::Playing;
}

}  // namespace cpptest
