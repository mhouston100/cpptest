#pragma once

#include "display.hpp"

namespace cpptest {

enum class WindowMode { Windowed, Borderless, Fullscreen };

struct GameOptions {
  WindowMode window_mode = WindowMode::Windowed;
  int width = kDefaultWindowW;
  int height = kDefaultWindowH;
  bool vsync = true;
};

[[nodiscard]] GameOptions SanitizeGameOptions(GameOptions options);
[[nodiscard]] unsigned int GameOptionsConfigFlags(const GameOptions& options);
void ApplyGameOptionsRuntime(const GameOptions& options);

}  // namespace cpptest
