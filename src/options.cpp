#include "options.hpp"

#include <fstream>
#include <iterator>
#include <string>

#include <nlohmann/json.hpp>
#include <raylib.h>

namespace cpptest {
namespace {

std::string JoinPath(const char* dir, const char* file) {
  std::string path = dir != nullptr ? dir : "";
  if (!path.empty() && path.back() != '/' && path.back() != '\\') {
    path += '/';
  }
  path += file;
  return path;
}

std::string OptionsPathNextToBinary() {
  return JoinPath(GetApplicationDirectory(), "options.json");
}

WindowMode WindowModeFromString(const std::string& name) {
  if (name == "borderless") {
    return WindowMode::Borderless;
  }
  if (name == "fullscreen") {
    return WindowMode::Fullscreen;
  }
  if (name == "windowed") {
    return WindowMode::Windowed;
  }
  return WindowMode::Windowed;
}

const char* WindowModeToString(const WindowMode mode) {
  switch (mode) {
    case WindowMode::Borderless:
      return "borderless";
    case WindowMode::Fullscreen:
      return "fullscreen";
    case WindowMode::Windowed:
    default:
      return "windowed";
  }
}

}  // namespace

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
      flags |= FLAG_BORDERLESS_WINDOWED_MODE;
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

const char* WindowModeName(const WindowMode mode) {
  switch (mode) {
    case WindowMode::Borderless:
      return "Borderless";
    case WindowMode::Fullscreen:
      return "Fullscreen";
    case WindowMode::Windowed:
    default:
      return "Windowed";
  }
}

int VideoPresetIndex(const int width, const int height) {
  for (int i = 0; i < static_cast<int>(std::size(kVideoPresets)); ++i) {
    if (kVideoPresets[i].width == width && kVideoPresets[i].height == height) {
      return i;
    }
  }
  return -1;
}

void ApplyGameOptionsRuntime(const GameOptions& options) {
  if (options.vsync) {
    SetWindowState(FLAG_VSYNC_HINT);
  } else {
    ClearWindowState(FLAG_VSYNC_HINT);
  }
}

void ApplyGameOptions(const GameOptions& options) {
  const GameOptions sanitized = SanitizeGameOptions(options);
  ApplyGameOptionsRuntime(sanitized);

  switch (sanitized.window_mode) {
    case WindowMode::Fullscreen:
      if (IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) {
        ToggleBorderlessWindowed();
      }
      SetWindowSize(sanitized.width, sanitized.height);
      if (!IsWindowFullscreen()) {
        ToggleFullscreen();
      }
      break;
    case WindowMode::Borderless:
      if (IsWindowFullscreen()) {
        ToggleFullscreen();
      }
      if (!IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) {
        ToggleBorderlessWindowed();
      }
      break;
    case WindowMode::Windowed:
    default:
      if (IsWindowFullscreen()) {
        ToggleFullscreen();
      }
      if (IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) {
        ToggleBorderlessWindowed();
      }
      SetWindowState(FLAG_WINDOW_RESIZABLE);
      SetWindowSize(sanitized.width, sanitized.height);
      break;
  }
}

GameOptions LoadGameOptions() {
  GameOptions options{};
  std::string path = OptionsPathNextToBinary();
  if (!FileExists(path.c_str())) {
    path = "options.json";
  }
  if (!FileExists(path.c_str())) {
    return SanitizeGameOptions(options);
  }

  std::ifstream in(path);
  if (!in) {
    return SanitizeGameOptions(options);
  }

  nlohmann::json root;
  try {
    in >> root;
  } catch (const nlohmann::json::exception&) {
    TraceLog(LOG_WARNING, "Options: ignoring invalid JSON in %s", path.c_str());
    return SanitizeGameOptions(options);
  }
  if (!root.is_object()) {
    return SanitizeGameOptions(options);
  }

  if (root.contains("width") && root["width"].is_number_integer()) {
    options.width = root["width"].get<int>();
  }
  if (root.contains("height") && root["height"].is_number_integer()) {
    options.height = root["height"].get<int>();
  }
  if (root.contains("vsync") && root["vsync"].is_boolean()) {
    options.vsync = root["vsync"].get<bool>();
  }
  if (root.contains("window_mode") && root["window_mode"].is_string()) {
    options.window_mode = WindowModeFromString(root["window_mode"].get<std::string>());
  }
  return SanitizeGameOptions(options);
}

bool SaveGameOptions(const GameOptions& options) {
  const GameOptions sanitized = SanitizeGameOptions(options);
  const nlohmann::json root = {
      {"window_mode", WindowModeToString(sanitized.window_mode)},
      {"width", sanitized.width},
      {"height", sanitized.height},
      {"vsync", sanitized.vsync},
  };
  const std::string payload = root.dump(2);

  const std::string paths[] = {OptionsPathNextToBinary(), std::string{"options.json"}};
  for (const std::string& path : paths) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
      continue;
    }
    out << payload << '\n';
    if (out) {
      return true;
    }
  }
  TraceLog(LOG_WARNING, "Options: could not write options.json");
  return false;
}

}  // namespace cpptest
