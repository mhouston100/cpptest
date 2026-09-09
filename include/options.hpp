#pragma once

#include <string>

#include "display.hpp"

namespace cpptest {

enum class WindowMode { Windowed, Borderless, Fullscreen };

struct GameOptions {
  WindowMode window_mode = WindowMode::Windowed;
  int width = kDefaultWindowW;
  int height = kDefaultWindowH;
  bool vsync = true;
};

struct VideoPreset {
  int width = 0;
  int height = 0;
};

constexpr VideoPreset kVideoPresets[] = {
    {1280, 720},
    {1600, 900},
    {1920, 1080},
    {2560, 1440},
};

[[nodiscard]] GameOptions SanitizeGameOptions(GameOptions options);
[[nodiscard]] unsigned int GameOptionsConfigFlags(const GameOptions& options);
[[nodiscard]] const char* WindowModeName(WindowMode mode);
[[nodiscard]] int VideoPresetIndex(int width, int height);
void ApplyGameOptionsRuntime(const GameOptions& options);
void ApplyGameOptions(const GameOptions& options);
[[nodiscard]] GameOptions LoadGameOptions();
bool SaveGameOptions(const GameOptions& options);

}  // namespace cpptest
