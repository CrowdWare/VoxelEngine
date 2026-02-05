/*
 * Copyright (C) 2026 CrowdWare
 *
 * This file is part of RaidShared.
 */

#include "tile_catalog.h"

#include "sml_parser.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <chrono>
#include <cstdint>

#if defined(_WIN32)
#include <direct.h>
#else
#include <dirent.h>
#endif

static bool LoadFileText(const std::string& path, std::string* out_text) {
    std::ifstream file(path.c_str());
    if (!file.is_open())
        return false;
    std::ostringstream ss;
    ss << file.rdbuf();
    *out_text = ss.str();
    return true;
}

static bool FileExists(const std::string& path) {
    std::ifstream file(path.c_str());
    return file.good();
}

static bool DirExists(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
    return (st.st_mode & S_IFDIR) != 0;
}

static bool EnsureDir(const std::string& path) {
#if defined(_WIN32)
    if (_mkdir(path.c_str()) == 0)
        return true;
    return errno == EEXIST;
#else
    if (mkdir(path.c_str(), 0755) == 0)
        return true;
    return errno == EEXIST;
#endif
}

static std::string GetParentDir(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos)
        return ".";
    return path.substr(0, slash);
}

static std::string JoinPath(const std::string& a, const std::string& b) {
    if (a.empty())
        return b;
    if (b.empty())
        return a;
    if (a.back() == '/' || a.back() == '\\')
        return a + b;
    return a + "/" + b;
}

static std::string StripResPrefix(const std::string& path) {
    const std::string prefix = "res://";
    if (path.compare(0, prefix.size(), prefix) == 0)
        return path.substr(prefix.size());
    return path;
}

static std::string StripDotSlash(const std::string& path) {
    if (path.compare(0, 2, "./") == 0)
        return path.substr(2);
    return path;
}

static bool EndsWith(const std::string& s, const std::string& suffix) {
    if (suffix.size() > s.size())
        return false;
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}

static bool MeshLoadTimingEnabled() {
    const char* value = std::getenv("DEBUG_MESH_LOAD");
    if (!value)
        return false;
    std::string v(value);
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

static bool MeshCacheEnabled() {
    const char* value = std::getenv("MESH_CACHE");
    if (!value)
        return true;
    std::string v(value);
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

static bool MeshCacheDebugEnabled() {
    const char* value = std::getenv("DEBUG_MESH_CACHE");
    if (!value)
        return false;
    std::string v(value);
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

static std::string CacheBaseDir(const std::string& repo_root) {
    return JoinPath(repo_root, "build/cache/meshes");
}

static std::string HashPath(const std::string& value) {
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < value.size(); ++i) {
        hash ^= static_cast<unsigned char>(value[i]);
        hash *= 1099511628211ull;
    }
    std::ostringstream ss;
    ss << std::hex << hash;
    return ss.str();
}

static std::string MeshCachePath(const std::string& repo_root, const std::string& model_path) {
    return JoinPath(CacheBaseDir(repo_root), HashPath(model_path) + ".meshbin");
}

static bool WriteU32(std::ostream& out, uint32_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    return out.good();
}

static bool ReadU32(std::istream& in, uint32_t* value) {
    in.read(reinterpret_cast<char*>(value), sizeof(*value));
    return in.good();
}

static bool WriteF32Vec(std::ostream& out, const std::vector<float>& data) {
    if (!WriteU32(out, static_cast<uint32_t>(data.size())))
        return false;
    if (!data.empty())
        out.write(reinterpret_cast<const char*>(data.data()), sizeof(float) * data.size());
    return out.good();
}

static bool ReadF32Vec(std::istream& in, std::vector<float>* data) {
    uint32_t size = 0;
    if (!ReadU32(in, &size))
        return false;
    data->assign(size, 0.0f);
    if (size > 0)
        in.read(reinterpret_cast<char*>(data->data()), sizeof(float) * size);
    return in.good();
}

static bool LoadMeshCache(const std::string& repo_root,
                          const std::string& model_path,
                          GltfMesh* out_mesh) {
    if (!MeshCacheEnabled() || !out_mesh)
        return false;
    const std::string cache_path = MeshCachePath(repo_root, model_path);
    std::ifstream in(cache_path.c_str(), std::ios::binary);
    if (!in.is_open()) {
        if (MeshCacheDebugEnabled())
            std::fprintf(stderr, "Mesh cache miss (no file): %s\n", cache_path.c_str());
        return false;
    }
    uint32_t magic = 0;
    uint32_t version = 0;
    if (!ReadU32(in, &magic) || !ReadU32(in, &version)) {
        if (MeshCacheDebugEnabled())
            std::fprintf(stderr, "Mesh cache invalid header: %s\n", cache_path.c_str());
        return false;
    }
    if (magic != 0x4853454D || version != 1)
        return false;
    uint32_t has_uv = 0;
    if (!ReadU32(in, &has_uv))
        return false;
    if (!ReadF32Vec(in, &out_mesh->mesh.positions))
        return false;
    if (!ReadF32Vec(in, &out_mesh->mesh.normals))
        return false;
    if (!ReadF32Vec(in, &out_mesh->mesh.uvs))
        return false;
    if (!ReadF32Vec(in, &out_mesh->mesh.colors))
        return false;
    out_mesh->has_uv = (has_uv != 0);
    if (MeshCacheDebugEnabled())
        std::fprintf(stderr, "Mesh cache hit: %s\n", cache_path.c_str());
    return true;
}

static void SaveMeshCache(const std::string& repo_root,
                          const std::string& model_path,
                          const GltfMesh& mesh) {
    if (!MeshCacheEnabled())
        return;
    const std::string cache_path = MeshCachePath(repo_root, model_path);
    const std::string cache_dir = CacheBaseDir(repo_root);
    EnsureDir(JoinPath(repo_root, "build"));
    EnsureDir(JoinPath(repo_root, "build/cache"));
    EnsureDir(cache_dir);
    std::ofstream out(cache_path.c_str(), std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        if (MeshCacheDebugEnabled())
            std::fprintf(stderr, "Mesh cache write failed: %s\n", cache_path.c_str());
        return;
    }
    WriteU32(out, 0x4853454D); // MESH
    WriteU32(out, 1);
    WriteU32(out, mesh.has_uv ? 1u : 0u);
    WriteF32Vec(out, mesh.mesh.positions);
    WriteF32Vec(out, mesh.mesh.normals);
    WriteF32Vec(out, mesh.mesh.uvs);
    WriteF32Vec(out, mesh.mesh.colors);
    if (MeshCacheDebugEnabled())
        std::fprintf(stderr, "Mesh cache write: %s\n", cache_path.c_str());
}

static std::string NormalizeTileModel(const std::string& model_in) {
    std::string model = model_in;
    std::string fragment;
    size_t hash = model.find('#');
    if (hash != std::string::npos) {
        fragment = model.substr(hash);
        model = model.substr(0, hash);
    }
    if (model.empty())
        return "block.glb" + fragment;
    if (model.compare(0, 8, "texture:") == 0)
        return "block.glb" + fragment;
    model = StripResPrefix(model);
    if (model.compare(0, 9, "../build/") == 0)
        model = model.substr(3);
    if (model.empty())
        return "block.glb" + fragment;
    return model + fragment;
}

static std::string ResolveWorkspacePath(const std::string& repo_root, const std::string& rel) {
    if (rel.empty())
        return rel;
    if (rel[0] == '/' || rel[0] == '\\')
        return rel;
    return JoinPath(repo_root, rel);
}

static std::string MapLegacyTexturePath(const std::string& path) {
    if (path.empty())
        return path;
    const std::string legacy_prefix = "assets/textures/";
    if (path.compare(0, legacy_prefix.size(), legacy_prefix) == 0)
        return "Assets/textures/" + path.substr(legacy_prefix.size());
    return path;
}

static std::string ResolveModelPath(const std::string& repo_root, const std::string& path) {
    if (path.empty())
        return path;
    std::string base = path;
    std::string fragment;
    size_t hash = base.find('#');
    if (hash != std::string::npos) {
        fragment = base.substr(hash);
        base = base.substr(0, hash);
    }
    if (base[0] == '/' || base[0] == '\\')
        return base + fragment;
    if (base[0] == '.' || base.find('/') != std::string::npos || base.find('\\') != std::string::npos) {
        std::string candidate = ResolveWorkspacePath(repo_root, base);
        if (FileExists(candidate))
            return candidate + fragment;
        return base + fragment;
    }
    std::string candidate = ResolveWorkspacePath(repo_root, JoinPath("build/blocks_cache", base));
    if (FileExists(candidate))
        return candidate + fragment;
    candidate = ResolveWorkspacePath(repo_root, JoinPath("RaidBuilder/assets/blocks", base));
    if (FileExists(candidate))
        return candidate + fragment;
    return base + fragment;
}

static int ComputeHeightBlocks(int height_cm, int scale_percent, int block_cm) {
    if (height_cm <= 0)
        height_cm = block_cm;
    if (scale_percent <= 0)
        scale_percent = 100;
    int eff_cm = height_cm * scale_percent;
    int denom = block_cm * 100;
    if (denom <= 0)
        denom = 1;
    return (eff_cm + denom - 1) / denom;
}

static std::vector<std::string> ListSubdirs(const std::string& root_dir) {
    std::vector<std::string> dirs;
#if defined(_WIN32)
    (void)root_dir;
    return dirs;
#else
    DIR* dir = opendir(root_dir.c_str());
    if (!dir)
        return dirs;
    struct dirent* ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name.empty() || name == "." || name == "..")
            continue;
        std::string full = JoinPath(root_dir, name);
        if (DirExists(full))
            dirs.push_back(full);
    }
    closedir(dir);
    std::sort(dirs.begin(), dirs.end());
#endif
    return dirs;
}

static bool ParseTilesFile(const std::string& path,
                           const std::string& category,
                           std::vector<TileDef>* out_tiles,
                           std::string* error_message) {
    if (!out_tiles)
        return false;
    std::string text;
    if (!LoadFileText(path, &text)) {
        if (error_message)
            *error_message = "Could not read tiles file";
        return false;
    }

    class TilesHandler : public sml::SmlHandler {
    public:
        std::vector<std::string> stack;
        TileDef tile;
        std::vector<TileDef> tiles;
        std::string category;

        explicit TilesHandler(const std::string& cat) : category(cat) {}

        void startElement(const std::string& name) override { stack.push_back(name); }
        void onProperty(const std::string& name, const sml::PropertyValue& value) override {
            if (stack.empty() || stack.back() != "Tile")
                return;
            if (name == "key" && value.type == sml::PropertyValue::String)
                tile.key = value.string_value;
            else if (name == "name" && value.type == sml::PropertyValue::String)
                tile.name = value.string_value;
            else if (name == "icon" && value.type == sml::PropertyValue::String)
                tile.icon = value.string_value;
            else if (name == "texture" && value.type == sml::PropertyValue::String)
                tile.texture = value.string_value;
            else if (name == "model" && value.type == sml::PropertyValue::String)
                tile.model = value.string_value;
            else if (name == "animation" && value.type == sml::PropertyValue::String)
                tile.animation = value.string_value;
            else if (name == "type" && value.type == sml::PropertyValue::String)
                tile.type = value.string_value;
            else if (name == "material" && value.type == sml::PropertyValue::EnumType)
                tile.material = value.string_value;
            else if (name == "placement" && value.type == sml::PropertyValue::EnumType)
                tile.placement = value.string_value;
            else if (name == "height_cm" && value.type == sml::PropertyValue::Int)
                tile.height_cm = value.int_value;
            else if (name == "scale_percent" && value.type == sml::PropertyValue::Int)
                tile.scale_percent = value.int_value;
        }
        void endElement(const std::string& name) override {
            if (name == "Tile") {
                if (!tile.key.empty()) {
                    tile.category = category;
                    tile.height_blocks = ComputeHeightBlocks(tile.height_cm, tile.scale_percent, 60);
                    tiles.push_back(tile);
                }
                tile = TileDef();
            }
            if (!stack.empty())
                stack.pop_back();
        }
    };

    TilesHandler handler(category);
    try {
        sml::SmlSaxParser parser(text);
        parser.registerEnumValue("material", "texture");
        parser.registerEnumValue("material", "vertex");
        parser.registerEnumValue("placement", "ground");
        parser.registerEnumValue("placement", "wall");
        parser.registerEnumValue("placement", "ceiling");
        parser.parse(handler);
    } catch (const sml::SmlParseException& e) {
        if (error_message)
            *error_message = e.what();
        return false;
    }

    out_tiles->insert(out_tiles->end(), handler.tiles.begin(), handler.tiles.end());
    return true;
}

std::vector<TileDef> LoadTileDefinitions(const std::string& repo_root,
                                         const std::string& tiles_root_rel,
                                         std::string* error_message) {
    std::vector<TileDef> tiles;
    std::string tiles_root = ResolveWorkspacePath(repo_root, tiles_root_rel);
    if (tiles_root.empty() || !DirExists(tiles_root)) {
        std::fprintf(stderr, "Tile catalog root not found: %s (repo_root=%s)\n",
                     tiles_root.c_str(), repo_root.c_str());
        return tiles;
    }
    std::fprintf(stderr, "Tile catalog root: %s\n", tiles_root.c_str());

    std::vector<std::string> categories = ListSubdirs(tiles_root);
    for (size_t i = 0; i < categories.size(); ++i) {
        const std::string& cat_dir = categories[i];
        size_t slash = cat_dir.find_last_of('/');
        std::string cat_name = (slash == std::string::npos) ? cat_dir : cat_dir.substr(slash + 1);
        std::string tiles_file = JoinPath(cat_dir, "tiles.sml");
        if (!FileExists(tiles_file))
            continue;
        std::string err;
        if (!ParseTilesFile(tiles_file, cat_name, &tiles, &err)) {
            if (error_message && error_message->empty())
                *error_message = err;
            std::fprintf(stderr, "Tile catalog error in %s: %s\n", tiles_file.c_str(), err.c_str());
        }
    }
    return tiles;
}

static int TileTexIndexFor(const TileDef& tile, bool mesh_has_uv) {
    if (!mesh_has_uv)
        return -2;
    if (tile.texture.empty())
        return -2;
    return 0;
}

bool LoadTileCatalog(const std::string& repo_root,
                     const std::string& tiles_root_rel,
                     const std::string& default_texture_rel,
                     TileCatalog* out_catalog,
                     std::string* error_message) {
    if (!out_catalog)
        return false;
    out_catalog->tiles.clear();
    out_catalog->meshes.clear();
    out_catalog->mesh_has_uv.clear();
    out_catalog->texture_paths.clear();
    out_catalog->animation_paths.clear();
    out_catalog->animation_libraries.clear();
    out_catalog->index_by_key.clear();

    std::vector<TileDef> tiles = LoadTileDefinitions(repo_root, tiles_root_rel, error_message);
    if (tiles.empty())
        return false;
    if (!PopulateTileResources(repo_root, default_texture_rel, &tiles, out_catalog))
        return false;
    return true;
}

bool PopulateTileResources(const std::string& repo_root,
                           const std::string& default_texture_rel,
                           std::vector<TileDef>* tiles,
                           TileCatalog* out_catalog) {
    if (!tiles || !out_catalog)
        return false;
    out_catalog->tiles.clear();
    out_catalog->meshes.clear();
    out_catalog->mesh_has_uv.clear();
    out_catalog->texture_paths.clear();
    out_catalog->animation_paths.clear();
    out_catalog->animation_libraries.clear();
    out_catalog->index_by_key.clear();

    std::vector<voxel::VoxelRenderer::MeshData> meshes;
    std::vector<bool> mesh_has_uv;
    std::vector<GltfAnimationLibrary> animations;
    meshes.reserve(tiles->size());
    mesh_has_uv.reserve(tiles->size());
    animations.reserve(tiles->size());

    const bool timing_enabled = MeshLoadTimingEnabled();
    auto mesh_start = std::chrono::steady_clock::now();
    std::map<std::string, GltfMesh> mesh_cache;
    for (size_t i = 0; i < tiles->size(); ++i) {
        std::string model = (*tiles)[i].model.empty() ? "block.glb" : (*tiles)[i].model;
        if (model.compare(0, 8, "texture:") == 0) {
            std::string tex_from_model = StripResPrefix(model.substr(8));
            if (!tex_from_model.empty())
                (*tiles)[i].texture = tex_from_model;
            model = "block.glb";
        }
        model = NormalizeTileModel(model);
        std::string model_path = ResolveModelPath(repo_root, model);
        GltfMesh mesh;
        std::string mesh_error;
        bool loaded_from_cache = false;
        bool mesh_ok = false;
        std::map<std::string, GltfMesh>::const_iterator it = mesh_cache.find(model_path);
        if (it != mesh_cache.end()) {
            mesh = it->second;
            mesh_ok = true;
            loaded_from_cache = true;
        } else {
            loaded_from_cache = LoadMeshCache(repo_root, model_path, &mesh);
            mesh_ok = loaded_from_cache;
            if (!mesh_ok)
                mesh_ok = LoadGltfMesh(model_path, &mesh, &mesh_error);
        }
        if (mesh_ok) {
            meshes.push_back(mesh.mesh);
            mesh_has_uv.push_back(mesh.has_uv);
            if (!loaded_from_cache && mesh_error.empty())
                SaveMeshCache(repo_root, model_path, mesh);
            mesh_cache[model_path] = mesh;
        } else {
            if (!mesh_error.empty())
                std::fprintf(stderr, "Failed to load model %s: %s\n", model_path.c_str(), mesh_error.c_str());
            else
                std::fprintf(stderr, "Failed to load model %s\n", model_path.c_str());
            meshes.push_back(voxel::VoxelRenderer::MeshData());
            mesh_has_uv.push_back(false);
        }

        GltfAnimationLibrary library;
        std::string animation_path = StripResPrefix((*tiles)[i].animation);
        if (!animation_path.empty())
            animation_path = StripDotSlash(animation_path);
        if (!animation_path.empty()) {
            std::string resolved = ResolveWorkspacePath(repo_root, animation_path);
            std::string animation_error;
            if (!LoadGltfAnimationLibrary(resolved, &library, &animation_error)) {
                if (!animation_error.empty())
                    std::fprintf(stderr, "Failed to load animation %s: %s\n", resolved.c_str(), animation_error.c_str());
                else
                    std::fprintf(stderr, "Failed to load animation %s\n", resolved.c_str());
                animation_path.clear();
            } else {
                animation_path = resolved;
            }
        }
        animations.push_back(library);
        (*tiles)[i].animation = animation_path;
    }
    if (timing_enabled) {
        auto mesh_end = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(mesh_end - mesh_start).count();
        std::fprintf(stderr, "Mesh load time: %lld ms for %zu tiles\n",
                     static_cast<long long>(ms), tiles->size());
    }

    std::vector<std::string> textures;
    textures.reserve(tiles->size());
    for (size_t i = 0; i < tiles->size(); ++i) {
        std::string tex = (*tiles)[i].texture.empty() ? default_texture_rel : (*tiles)[i].texture;
        tex = StripResPrefix(tex);
        tex = StripDotSlash(tex);
        tex = MapLegacyTexturePath(tex);
        std::string resolved = ResolveWorkspacePath(repo_root, tex);
        if (!FileExists(resolved)) {
            std::string raidbuilder_tex = ResolveWorkspacePath(repo_root, JoinPath("RaidBuilder", tex));
            if (FileExists(raidbuilder_tex)) {
                resolved = raidbuilder_tex;
            } else {
                std::string fallback = ResolveWorkspacePath(repo_root, MapLegacyTexturePath(default_texture_rel));
                std::fprintf(stderr, "Missing tile texture: %s (tile '%s'), using %s\n",
                            resolved.c_str(), (*tiles)[i].key.c_str(), fallback.c_str());
                resolved = fallback;
            }
        }
        textures.push_back(resolved);
    }

    std::map<std::string, int> index_by_key;
    for (size_t i = 0; i < tiles->size(); ++i)
        index_by_key[(*tiles)[i].key] = static_cast<int>(i);

    out_catalog->tiles = *tiles;
    out_catalog->meshes = meshes;
    out_catalog->mesh_has_uv = mesh_has_uv;
    out_catalog->texture_paths = textures;
    out_catalog->animation_paths.reserve(tiles->size());
    for (size_t i = 0; i < tiles->size(); ++i)
        out_catalog->animation_paths.push_back((*tiles)[i].animation);
    out_catalog->animation_libraries = animations;
    out_catalog->index_by_key = index_by_key;
    return true;
}

std::string ResolveTileKey(uint8_t tile_id,
                           const TileCatalog& catalog,
                           const std::vector<std::string>& legacy_keys) {
    if (tile_id < legacy_keys.size())
        return legacy_keys[tile_id];
    if (tile_id < catalog.tiles.size())
        return catalog.tiles[tile_id].key;
    return std::string();
}