#include "render.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>
#include <rlgl.h>

// START REMOVE-ALL STUDY NOTES
// The world renderer is isolated here so the main loop only asks for drawing
// operations instead of managing the shading and primitive drawing details.
// END REMOVE-ALL STUDY NOTES

float g_tileWorld = 1.f;

constexpr const char* kGroundVs = R"(
#version 330
in vec3 vertexPosition;
uniform mat4 matProjection;
uniform mat4 matView;
uniform mat4 matModel;
out vec3 fragWorldPos;

void main() {
  fragWorldPos = vec3(matModel * vec4(vertexPosition, 1.0));
  gl_Position = matProjection * matView * matModel * vec4(vertexPosition, 1.0);
}
)";

constexpr const char* kGroundFs = R"(
#version 330
in vec3 fragWorldPos;
uniform vec4 mapEdge;
out vec4 finalColor;

void main() {
  float L = mapEdge.x;
  float fade = mapEdge.y;
  float d = max(abs(fragWorldPos.x), abs(fragWorldPos.z));
  float t = smoothstep(L, L + fade, d);
  vec3 base = vec3(0.12549019607843137, 0.14901960784313725, 0.18823529411764706);
  finalColor = vec4(mix(base, vec3(0.0), t), 1.0);
}
)";

namespace {

std::string JoinPath(const char* dir, const char* file) {
  std::string path = dir != nullptr ? dir : "";
  if (!path.empty() && path.back() != '/' && path.back() != '\\') {
    path += '/';
  }
  path += file;
  return path;
}

std::string ResolveDataPath(const std::string& rel) {
  if (FileExists(rel.c_str())) {
    return rel;
  }
  const std::string next_to_binary = JoinPath(GetApplicationDirectory(), rel.c_str());
  if (FileExists(next_to_binary.c_str())) {
    return next_to_binary;
  }
  return rel;
}

Rectangle TileSrc(const MapVisuals& visuals, int tile) {
  const int count = std::max(1, visuals.columns * visuals.rows);
  tile = ((tile % count) + count) % count;
  const float tw = static_cast<float>(visuals.atlas.width) / static_cast<float>(visuals.columns);
  const float th = static_cast<float>(visuals.atlas.height) / static_cast<float>(visuals.rows);
  const int col = tile % visuals.columns;
  const int row = tile / visuals.columns;
  return {static_cast<float>(col) * tw, static_cast<float>(row) * th, tw, th};
}

void TexCoord(const Texture2D& tex, const Rectangle& src, float u, float v) {
  const float u0 = src.x / static_cast<float>(tex.width);
  const float v0 = src.y / static_cast<float>(tex.height);
  const float u1 = (src.x + src.width) / static_cast<float>(tex.width);
  const float v1 = (src.y + src.height) / static_cast<float>(tex.height);
  rlTexCoord2f(u0 + (u1 - u0) * u, v0 + (v1 - v0) * v);
}

void DrawFloorTile(const MapVisuals& visuals, float x, float z, float size, int tile) {
  const Rectangle src = TileSrc(visuals, tile);
  const float h = size * 0.5f;
  const float y = 0.002f;
  rlSetTexture(visuals.atlas.id);
  rlBegin(RL_QUADS);
  rlColor4ub(255, 255, 255, 255);
  rlNormal3f(0.f, 1.f, 0.f);
  TexCoord(visuals.atlas, src, 0.f, 0.f);
  rlVertex3f(x - h, y, z - h);
  TexCoord(visuals.atlas, src, 0.f, 1.f);
  rlVertex3f(x - h, y, z + h);
  TexCoord(visuals.atlas, src, 1.f, 1.f);
  rlVertex3f(x + h, y, z + h);
  TexCoord(visuals.atlas, src, 1.f, 0.f);
  rlVertex3f(x + h, y, z - h);
  rlEnd();
  rlSetTexture(0);
}

void DrawWallTile(const MapVisuals& visuals, float x, float z, float size, float height, int tile) {
  const Rectangle src = TileSrc(visuals, tile);
  const float h = size * 0.5f;
  const float y0 = 0.01f;
  const float y1 = y0 + height;
  rlSetTexture(visuals.atlas.id);
  rlBegin(RL_QUADS);
  rlColor4ub(255, 255, 255, 255);

  rlNormal3f(0.f, 0.f, 1.f);
  TexCoord(visuals.atlas, src, 0.f, 1.f);
  rlVertex3f(x - h, y0, z + h);
  TexCoord(visuals.atlas, src, 1.f, 1.f);
  rlVertex3f(x + h, y0, z + h);
  TexCoord(visuals.atlas, src, 1.f, 0.f);
  rlVertex3f(x + h, y1, z + h);
  TexCoord(visuals.atlas, src, 0.f, 0.f);
  rlVertex3f(x - h, y1, z + h);

  rlNormal3f(0.f, 0.f, -1.f);
  TexCoord(visuals.atlas, src, 0.f, 1.f);
  rlVertex3f(x + h, y0, z - h);
  TexCoord(visuals.atlas, src, 1.f, 1.f);
  rlVertex3f(x - h, y0, z - h);
  TexCoord(visuals.atlas, src, 1.f, 0.f);
  rlVertex3f(x - h, y1, z - h);
  TexCoord(visuals.atlas, src, 0.f, 0.f);
  rlVertex3f(x + h, y1, z - h);

  rlNormal3f(-1.f, 0.f, 0.f);
  TexCoord(visuals.atlas, src, 0.f, 1.f);
  rlVertex3f(x - h, y0, z - h);
  TexCoord(visuals.atlas, src, 1.f, 1.f);
  rlVertex3f(x - h, y0, z + h);
  TexCoord(visuals.atlas, src, 1.f, 0.f);
  rlVertex3f(x - h, y1, z + h);
  TexCoord(visuals.atlas, src, 0.f, 0.f);
  rlVertex3f(x - h, y1, z - h);

  rlNormal3f(1.f, 0.f, 0.f);
  TexCoord(visuals.atlas, src, 0.f, 1.f);
  rlVertex3f(x + h, y0, z + h);
  TexCoord(visuals.atlas, src, 1.f, 1.f);
  rlVertex3f(x + h, y0, z - h);
  TexCoord(visuals.atlas, src, 1.f, 0.f);
  rlVertex3f(x + h, y1, z - h);
  TexCoord(visuals.atlas, src, 0.f, 0.f);
  rlVertex3f(x + h, y1, z + h);

  rlNormal3f(0.f, 1.f, 0.f);
  TexCoord(visuals.atlas, src, 0.f, 0.f);
  rlVertex3f(x - h, y1, z - h);
  TexCoord(visuals.atlas, src, 0.f, 1.f);
  rlVertex3f(x - h, y1, z + h);
  TexCoord(visuals.atlas, src, 1.f, 1.f);
  rlVertex3f(x + h, y1, z + h);
  TexCoord(visuals.atlas, src, 1.f, 0.f);
  rlVertex3f(x + h, y1, z - h);

  rlEnd();
  rlSetTexture(0);
}

int WallTileForCell(const MapVisuals& visuals, int ix, int iy) {
  if (visuals.wall_tiles.empty()) {
    return 0;
  }
  const unsigned hash = static_cast<unsigned>(ix) * 73856093u ^ static_cast<unsigned>(iy) * 19349663u;
  return visuals.wall_tiles[hash % visuals.wall_tiles.size()];
}

}  // namespace

MapVisuals LoadMapVisuals() {
  MapVisuals visuals{};
  visuals.columns = 4;
  visuals.rows = 3;
  visuals.floor_tile = 0;
  visuals.wall_tiles = {0, 1, 2, 4, 5, 8};

  std::string atlas_path = "map/officewall.jpg";
  const std::string config_path = ResolveDataPath("map/tiles.json");
  std::ifstream in(config_path);
  if (in) {
    nlohmann::json root;
    try {
      in >> root;
      if (root.is_object()) {
        atlas_path = root.value("atlas", atlas_path);
        visuals.columns = std::max(1, root.value("columns", visuals.columns));
        visuals.rows = std::max(1, root.value("rows", visuals.rows));
        visuals.floor_tile = root.value("floor", visuals.floor_tile);
        if (root.contains("walls") && root["walls"].is_array()) {
          visuals.wall_tiles.clear();
          for (const auto& item : root["walls"]) {
            if (item.is_number_integer()) {
              visuals.wall_tiles.push_back(item.get<int>());
            }
          }
        }
      }
    } catch (const std::exception&) {
      TraceLog(LOG_WARNING, "Tiles: ignoring invalid %s", config_path.c_str());
    }
  }
  if (visuals.wall_tiles.empty()) {
    visuals.wall_tiles.push_back(0);
  }

  const std::string resolved_atlas = ResolveDataPath(atlas_path);
  if (!FileExists(resolved_atlas.c_str())) {
    TraceLog(LOG_WARNING, "Tiles: atlas not found (%s), using cube fallback", resolved_atlas.c_str());
    return visuals;
  }
  visuals.atlas = LoadTexture(resolved_atlas.c_str());
  if (visuals.atlas.id == 0) {
    TraceLog(LOG_WARNING, "Tiles: failed to load %s", resolved_atlas.c_str());
    return visuals;
  }
  SetTextureFilter(visuals.atlas, TEXTURE_FILTER_POINT);
  visuals.ok = true;
  TraceLog(LOG_INFO, "Tiles: loaded atlas %s", resolved_atlas.c_str());
  return visuals;
}

void UnloadMapVisuals(MapVisuals& visuals) {
  if (visuals.ok) {
    UnloadTexture(visuals.atlas);
  }
  visuals = {};
}

void DrawTiledMap(const MapVisuals& visuals, const GameMap& m) {
  if (!visuals.ok || m.c_wid <= 0 || m.c_hei <= 0) {
    DrawWallCells(m);
    return;
  }
  const float tw = g_tileWorld;
  const float hx = static_cast<float>(m.c_wid) * 0.5f;
  const float hz = static_cast<float>(m.c_hei) * 0.5f;
  const float wall_h = tw * 0.85f;
  const float wall_w = tw * 0.98f;
  for (int iy = 0; iy < m.c_hei; ++iy) {
    for (int ix = 0; ix < m.c_wid; ++ix) {
      const float wx = (static_cast<float>(ix) + 0.5f - hx) * tw;
      const float wz = (static_cast<float>(iy) + 0.5f - hz) * tw;
      if (m.IsWall(ix, iy)) {
        DrawWallTile(visuals, wx, wz, wall_w, wall_h, WallTileForCell(visuals, ix, iy));
      } else {
        DrawFloorTile(visuals, wx, wz, tw, visuals.floor_tile);
      }
    }
  }
}

GroundDrawResources LoadGroundDrawResources() {
  GroundDrawResources r{};
  r.shader = LoadShaderFromMemory(kGroundVs, kGroundFs);
  if (!IsShaderValid(r.shader)) {
    return r;
  }
  r.loc_map_edge = GetShaderLocation(r.shader, "mapEdge");
  if (r.loc_map_edge < 0) {
    UnloadShader(r.shader);
    r.shader = {};
    return r;
  }
  Mesh mesh = GenMeshPlane(1.f, 1.f, 1, 1);
  r.model = LoadModelFromMesh(mesh);
  r.model.materials[0].shader = r.shader;
  r.ok = true;
  return r;
}

void UnloadGroundDrawResources(GroundDrawResources& r) {
  if (r.ok) {
    UnloadModel(r.model);
    UnloadShader(r.shader);
    r.ok = false;
  }
}

void DrawGroundWithMapEdge(const GroundDrawResources& res, const GameMap& m, const float tile_world) {
  if (m.c_wid <= 0 || m.c_hei <= 0) {
    DrawPlane({0.f, 0.f, 0.f}, {40.f, 40.f}, Color{32, 38, 48, 255});
    return;
  }
  const float max_cells = static_cast<float>(std::max(m.c_wid, m.c_hei));
  const float plane_size = (max_cells + 4.f) * tile_world * 2.f;
  const float L = max_cells * 0.5f * tile_world;
  constexpr float k_fade_span_tiles = 2.75f;
  const float fade_w = std::max(0.02f, k_fade_span_tiles * tile_world);
  const float map_edge[4] = {L, fade_w, 0.f, 0.f};

  if (!res.ok) {
    DrawPlane({0.f, 0.f, 0.f}, {plane_size, plane_size}, Color{32, 38, 48, 255});
    return;
  }
  SetShaderValue(res.shader, res.loc_map_edge, map_edge, SHADER_UNIFORM_VEC4);
  DrawModelEx(res.model, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, 0.f,
              {plane_size, 1.f, plane_size}, WHITE);
}

void DrawWorldGridForMap(const GameMap& m, const Color line_color) {
  const float tw = g_tileWorld;
  const float hx = static_cast<float>(m.c_wid) * 0.5f;
  const float hz = static_cast<float>(m.c_hei) * 0.5f;
  constexpr float y = 0.001f;
  for (int ix = 0; ix <= m.c_wid; ++ix) {
    const float x = (static_cast<float>(ix) - hx) * tw;
    DrawLine3D({x, y, -hz * tw}, {x, y, hz * tw}, line_color);
  }
  for (int iy = 0; iy <= m.c_hei; ++iy) {
    const float z = (static_cast<float>(iy) - hz) * tw;
    DrawLine3D({-hx * tw, y, z}, {hx * tw, y, z}, line_color);
  }
}

void DrawWallCells(const GameMap& m) {
  const float tw = g_tileWorld;
  const float hx = static_cast<float>(m.c_wid) * 0.5f;
  const float hz = static_cast<float>(m.c_hei) * 0.5f;
  const float wall_h = tw * 0.55f;
  const float wall_w = tw * 0.92f;
  const Color fill{48, 52, 68, 255};
  const Color outline{22, 24, 32, 255};
  for (int iy = 0; iy < m.c_hei; ++iy) {
    for (int ix = 0; ix < m.c_wid; ++ix) {
      if (!m.IsWall(ix, iy)) {
        continue;
      }
      const float wx = (static_cast<float>(ix) + 0.5f - hx) * tw;
      const float wz = (static_cast<float>(iy) + 0.5f - hz) * tw;
      const Vector3 c{wx, wall_h * 0.5f + 0.01f, wz};
      DrawCube(c, wall_w, wall_h, wall_w, fill);
      DrawCubeWires(c, wall_w, wall_h, wall_w, outline);
    }
  }
}

static void DrawEntityMarker(const GameMap& m, int ix, int iy, float size, const Color fill,
                             const Color outline) {
  const float tw = g_tileWorld;
  const float hx = static_cast<float>(m.c_wid) * 0.5f;
  const float hz = static_cast<float>(m.c_hei) * 0.5f;
  const float wx = (static_cast<float>(ix) + 0.5f - hx) * tw;
  const float wz = (static_cast<float>(iy) + 0.5f - hz) * tw;
  const Vector3 c{wx, size * 0.5f + 0.01f, wz};
  DrawCube(c, size, size, size, fill);
  DrawCubeWires(c, size, size, size, outline);
}

void DrawMapEntities(const GameMap& m) {
  const float tw = g_tileWorld;
  const float talk_size = tw * 0.6f;
  const float marker_size = tw * 0.45f;
  for (const auto& entity : m.entities) {
    if (entity.kind == MapEntityKind::Prop) {
      DrawEntityMarker(m, entity.cell_x, entity.cell_y, talk_size, Color{244, 58, 58, 255},
                       Color{180, 40, 40, 255});
    } else if (entity.kind == MapEntityKind::Spawn) {
      DrawEntityMarker(m, entity.cell_x, entity.cell_y, marker_size, Color{74, 222, 128, 255},
                       Color{22, 101, 52, 255});
    } else if (entity.kind == MapEntityKind::Warp) {
      DrawEntityMarker(m, entity.cell_x, entity.cell_y, marker_size, Color{56, 189, 248, 255},
                       Color{12, 74, 110, 255});
    }
  }
}

void DrawNpcMarkers(const GameMap& m, const std::vector<cpptest::WorldNpcPose>& poses) {
  const float talk_size = g_tileWorld * 0.6f;
  for (const auto& pose : poses) {
    DrawEntityMarker(m, pose.cell_x, pose.cell_y, talk_size, Color{245, 158, 11, 255},
                     Color{146, 64, 14, 255});
  }
}

void DrawPlayerBlock(const Vector3& base_center, const Color fill, const Color outline) {
  const float s = g_tileWorld * 0.35f;
  const float h = g_tileWorld * 0.85f;
  const Vector3 c{base_center.x, base_center.y + h * 0.5f, base_center.z};
  DrawCube(c, s * 2.f, h, s * 2.f, fill);
  DrawCubeWires(c, s * 2.f, h, s * 2.f, outline);
}

