#include "options.hpp"

#include <raylib.h>

namespace cpptest {

GameOptions SanitizeGameOptions(GameOptions options) {
  if (options.width < 640 || options.height < 360) {
    options.width = kDefaultWindowW;
    options.height = kDefaultWindowH;
  }
  switch (options.window_mode) {
    case WindowMode::Windowed:
    case WindowMode::Borderless:
    case WindowMode::Fullscreen:
      break;
    default:
      options.window_mode = WindowMode::Windowed;
      break;
  }
  return options;
}

unsigned int GameOptionsConfigFlags(const GameOptions& options) {
  unsigned int flags = 0;
  if (options.vsync) {
    flags |= FLAG_VSYNC_HINT;
  }
  switch (options.window_mode) {
    case WindowMode::Borderless:
      flags |= FLAG_WINDOW_UNDECORATED;
      break;
    case WindowMode::Fullscreen:
      flags |= FLAG_FULLSCREEN_MODE;
      break;
    case WindowMode::Windowed:
    default:
      flags |= FLAG_WINDOW_RESIZABLE;
      break;
  }
  return flags;
}

void ApplyGameOptionsRuntime(const GameOptions& options) {
  if (options.vsync) {
    SetWindowState(FLAG_VSYNC_HINT);
  } else {
    ClearWindowState(FLAG_VSYNC_HINT);
  }
}

}  // namespace cpptest
