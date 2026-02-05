#pragma once

#include <string>
#include <vector>
#include "voxel_renderer.h"

struct GltfMesh {
    voxel::VoxelRenderer::MeshData mesh;
    bool has_uv = false;
};

struct GltfAnimationClip {
    std::string name;
    float duration = 0.0f;
};

struct GltfAnimationLibrary {
    std::vector<GltfAnimationClip> clips;
};

bool LoadGltfMesh(const std::string& path, GltfMesh* out_mesh, std::string* error);
bool LoadGltfAnimationLibrary(const std::string& path,
                              GltfAnimationLibrary* out_library,
                              std::string* error);