cmake_minimum_required(VERSION 3.20)
project(cpptest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(json)

add_executable(cpptest src/main.cpp src/ldtk_load.cpp)
target_include_directories(cpptest PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(cpptest PRIVATE raylib nlohmann_json::nlohmann_json)

add_custom_command(
  TARGET cpptest
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/map"
          "$<TARGET_FILE_DIR:cpptest>/map"
  COMMENT "Copy LDtk maps next to executable")
