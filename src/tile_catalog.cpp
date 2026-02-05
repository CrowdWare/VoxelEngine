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

static std::vector<TileDef> LoadTileDefinitions(const std::string& repo_root,
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
        if (LoadGltfMesh(model_path, &mesh, &mesh_error)) {
            meshes.push_back(mesh.mesh);
            mesh_has_uv.push_back(mesh.has_uv);
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