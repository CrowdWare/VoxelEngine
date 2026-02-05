#pragma once

#include <map>
#include <string>
#include <vector>
#include "gltf_loader.h"

struct TileDef {
    std::string key;
    std::string name;
    std::string icon;
    std::string texture;
    std::string model;
    std::string type = "block"; // block | prop
    int height_cm = 60;
    int scale_percent = 100;
    int height_blocks = 1;
    std::string material;
    std::string placement;
    std::string category;
};

struct TileCatalog {
    std::vector<TileDef> tiles;
    std::vector<voxel::VoxelRenderer::MeshData> meshes;
    std::vector<bool> mesh_has_uv;
    std::vector<std::string> texture_paths;
    std::map<std::string, int> index_by_key;
};

bool LoadTileCatalog(const std::string& repo_root,
                     const std::string& tiles_root_rel,
                     const std::string& default_texture_rel,
                     TileCatalog* out_catalog,
                     std::string* error_message);

bool PopulateTileResources(const std::string& repo_root,
                           const std::string& default_texture_rel,
                           std::vector<TileDef>* tiles,
                           TileCatalog* out_catalog);

std::string ResolveTileKey(uint8_t tile_id,
                           const TileCatalog& catalog,
                           const std::vector<std::string>& legacy_keys);
