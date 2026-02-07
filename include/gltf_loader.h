#pragma once

#include <string>
#include <vector>
#include "voxel_renderer.h"

struct GltfMesh {
    voxel::VoxelRenderer::MeshData mesh;
    bool has_uv = false;
    std::string base_color_texture_path;
};

struct GltfAnimationClip {
    std::string name;
    float duration = 0.0f;
};

struct GltfAnimationLibrary {
    std::vector<GltfAnimationClip> clips;
};

struct GltfSkinningFrames {
    uint32_t joint_count = 0;
    uint32_t frame_count = 0;
    float duration = 0.0f;
    std::vector<float> palettes; // frame_count * joint_count * 16
};

bool LoadGltfMesh(const std::string& path, GltfMesh* out_mesh, std::string* error);
bool LoadGltfAnimationLibrary(const std::string& path,
                              GltfAnimationLibrary* out_library,
                              std::string* error);
bool LoadGltfSkinningFrames(const std::string& model_path,
                            const std::string& animation_path,
                            GltfSkinningFrames* out_frames,
                            std::string* error);