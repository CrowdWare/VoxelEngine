/*
 * Copyright (C) 2026 CrowdWare
 *
 * This file is part of RaidShared.
 */

#include "gltf_loader.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"

#include <vector>
#include <cmath>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <sys/stat.h>

struct AnimationCacheEntry {
    GltfAnimationLibrary library;
    std::string error;
    bool loaded = false;
};

static std::map<std::string, AnimationCacheEntry> g_animation_cache;

static bool MeshDebugEnabled() {
    const char* value = std::getenv("DEBUG_MESHES");
    if (!value)
        return false;
    std::string v(value);
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(v[i])));
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

static void DebugMeshLog(const std::string& message) {
    if (MeshDebugEnabled())
        std::fprintf(stderr, "%s\n", message.c_str());
}

static void SplitPathFragment(const std::string& path, std::string* base, std::string* fragment) {
    if (base)
        base->clear();
    if (fragment)
        fragment->clear();
    size_t hash = path.find('#');
    if (hash == std::string::npos) {
        if (base)
            *base = path;
        return;
    }
    if (base)
        *base = path.substr(0, hash);
    if (fragment)
        *fragment = path.substr(hash + 1);
}

static bool FileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

static std::string GetParentDir(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos)
        return std::string(".");
    return path.substr(0, slash);
}

static std::string JoinPath(const std::string& a, const std::string& b) {
    if (a.empty())
        return b;
    if (b.empty())
        return a;
    char last = a[a.size() - 1];
    if (last == '/' || last == '\\')
        return a + b;
    return a + "/" + b;
}

static std::string ToLowerCopy(const std::string& text) {
    std::string out = text;
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
    return out;
}

static std::string CanonicalNodeName(const std::string& name) {
    std::string s = ToLowerCopy(name);
    // Strip common namespace separators used by DCC/Mixamo exports.
    size_t colon = s.find_last_of(':');
    if (colon != std::string::npos && colon + 1 < s.size())
        s = s.substr(colon + 1);
    size_t pipe = s.find_last_of('|');
    if (pipe != std::string::npos && pipe + 1 < s.size())
        s = s.substr(pipe + 1);
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (std::isalnum(c))
            out.push_back(static_cast<char>(c));
    }
    return out;
}

static bool IsDataUri(const std::string& uri) {
    return uri.compare(0, 5, "data:") == 0;
}

static bool IsAbsolutePath(const std::string& path) {
    if (path.empty())
        return false;
    if (path[0] == '/' || path[0] == '\\')
        return true;
    if (path.size() > 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':')
        return true;
    return false;
}

static std::string ResolveRelativeUri(const std::string& gltf_path, const std::string& uri) {
    if (uri.empty() || IsDataUri(uri) || IsAbsolutePath(uri))
        return uri;
    return JoinPath(GetParentDir(gltf_path), uri);
}

static std::string ResolveBaseColorTexturePath(const tinygltf::Model& model,
                                               const tinygltf::Primitive& prim,
                                               const std::string& base_path) {
    if (prim.material < 0 || prim.material >= static_cast<int>(model.materials.size()))
        return std::string();
    const tinygltf::Material& material = model.materials[prim.material];
    const int tex_index = material.pbrMetallicRoughness.baseColorTexture.index;
    if (tex_index < 0 || tex_index >= static_cast<int>(model.textures.size()))
        return std::string();
    const tinygltf::Texture& tex = model.textures[tex_index];
    if (tex.source < 0 || tex.source >= static_cast<int>(model.images.size()))
        return std::string();
    const tinygltf::Image& image = model.images[tex.source];
    if (image.uri.empty() || IsDataUri(image.uri))
        return std::string();
    return ResolveRelativeUri(base_path, image.uri);
}

static bool LoadTinyGltfModelFromFile(const std::string& base_path,
                                      tinygltf::Model* out_model,
                                      std::string* out_error) {
    if (!out_model)
        return false;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    bool ok = false;
    const std::string lower = ToLowerCopy(base_path);
    if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".gltf") {
        ok = loader.LoadASCIIFromFile(out_model, &err, &warn, base_path);
    } else if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".glb") {
        ok = loader.LoadBinaryFromFile(out_model, &err, &warn, base_path);
    } else {
        ok = loader.LoadBinaryFromFile(out_model, &err, &warn, base_path);
        if (!ok) {
            err.clear();
            warn.clear();
            ok = loader.LoadASCIIFromFile(out_model, &err, &warn, base_path);
        }
    }

    if (!warn.empty())
        std::fprintf(stderr, "glTF warning (%s): %s\n", base_path.c_str(), warn.c_str());
    if (!ok) {
        if (out_error)
            *out_error = err.empty() ? "Failed to load glTF/glb" : err;
        return false;
    }

    std::fprintf(stderr, "glTF path: %s\n", base_path.c_str());
    for (size_t i = 0; i < out_model->buffers.size(); ++i) {
        const std::string uri = out_model->buffers[i].uri;
        const std::string resolved = ResolveRelativeUri(base_path, uri);
        std::fprintf(stderr,
                     "glTF buffer[%zu]: uri='%s' resolved='%s' bytes=%zu\n",
                     i,
                     uri.empty() ? "<embedded>" : uri.c_str(),
                     resolved.empty() ? "<embedded>" : resolved.c_str(),
                     out_model->buffers[i].data.size());
        if (!uri.empty() && !IsDataUri(uri) && !FileExists(resolved)) {
            if (out_error)
                *out_error = std::string("Missing glTF buffer file: ") + resolved;
            return false;
        }
    }
    for (size_t i = 0; i < out_model->images.size(); ++i) {
        const std::string uri = out_model->images[i].uri;
        const std::string resolved = ResolveRelativeUri(base_path, uri);
        std::fprintf(stderr,
                     "glTF image[%zu]: uri='%s' resolved='%s'\n",
                     i,
                     uri.empty() ? "<embedded>" : uri.c_str(),
                     resolved.empty() ? "<embedded>" : resolved.c_str());
        if (!uri.empty() && !IsDataUri(uri) && !FileExists(resolved)) {
            if (out_error)
                *out_error = std::string("Missing glTF image file: ") + resolved;
            return false;
        }
    }
    size_t max_joints = 0;
    for (size_t i = 0; i < out_model->skins.size(); ++i)
        max_joints = std::max(max_joints, out_model->skins[i].joints.size());
    std::fprintf(stderr, "glTF skin joints: skins=%zu max_joints=%zu\n", out_model->skins.size(), max_joints);
    return true;
}

static std::vector<std::string> SplitMeshSelectors(const std::string& fragment) {
    std::vector<std::string> selectors;
    if (fragment.empty()) {
        selectors.push_back(std::string());
        return selectors;
    }
    size_t start = 0;
    while (start < fragment.size()) {
        size_t next = fragment.find('#', start);
        std::string part = (next == std::string::npos)
                               ? fragment.substr(start)
                               : fragment.substr(start, next - start);
        if (!part.empty())
            selectors.push_back(part);
        if (next == std::string::npos)
            break;
        start = next + 1;
    }
    if (selectors.empty())
        selectors.push_back(std::string());
    return selectors;
}

static int FindMeshIndexByName(const tinygltf::Model& model, const std::string& name) {
    if (name.empty())
        return 0;
    for (size_t i = 0; i < model.meshes.size(); ++i) {
        if (model.meshes[i].name == name)
            return static_cast<int>(i);
    }
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        if (model.nodes[i].name == name && model.nodes[i].mesh >= 0)
            return model.nodes[i].mesh;
    }
    return -1;
}


static bool ReadAccessor(const tinygltf::Model& model,
                         const tinygltf::Accessor& accessor,
                         std::vector<float>* out,
                         int expected_components) {
    if (!out)
        return false;
    out->clear();
    if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
        return false;
    if (accessor.type == TINYGLTF_TYPE_SCALAR && expected_components != 1)
        return false;
    if (accessor.type == TINYGLTF_TYPE_VEC2 && expected_components != 2)
        return false;
    if (accessor.type == TINYGLTF_TYPE_VEC3 && expected_components != 3)
        return false;
    if (accessor.type == TINYGLTF_TYPE_VEC4 && expected_components != 4)
        return false;

    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
        return false;
    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
        return false;
    const tinygltf::Buffer& buffer = model.buffers[view.buffer];
    size_t stride = accessor.ByteStride(view);
    if (stride == 0)
        stride = expected_components * sizeof(float);
    const size_t base_offset = static_cast<size_t>(view.byteOffset + accessor.byteOffset);
    const size_t required_bytes = (accessor.count == 0) ? 0 : (stride * (accessor.count - 1) + expected_components * sizeof(float));
    if (base_offset + required_bytes > buffer.data.size())
        return false;
    const unsigned char* data = buffer.data.data() + base_offset;

    out->resize(accessor.count * expected_components);
    for (size_t i = 0; i < accessor.count; ++i) {
        const float* src = reinterpret_cast<const float*>(data + stride * i);
        for (int c = 0; c < expected_components; ++c)
            (*out)[i * expected_components + c] = src[c];
    }
    return true;
}

static bool ReadIndices(const tinygltf::Model& model,
                        const tinygltf::Accessor& accessor,
                        std::vector<uint32_t>* out) {
    if (!out)
        return false;
    out->clear();
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
        return false;
    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
        return false;
    const tinygltf::Buffer& buffer = model.buffers[view.buffer];
    size_t stride = accessor.ByteStride(view);
    size_t component = 0;
    switch (accessor.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: component = 1; break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: component = 2; break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: component = 4; break;
        default: return false;
    }
    if (stride == 0) {
        size_t comps = 1;
        stride = component * comps;
    }
    const size_t base_offset = static_cast<size_t>(view.byteOffset + accessor.byteOffset);
    const size_t required_bytes = (accessor.count == 0) ? 0 : (stride * (accessor.count - 1) + component);
    if (base_offset + required_bytes > buffer.data.size())
        return false;
    const unsigned char* data = buffer.data.data() + base_offset;

    out->resize(accessor.count);
    for (size_t i = 0; i < accessor.count; ++i) {
        const unsigned char* src = data + stride * i;
        uint32_t value = 0;
        if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            value = *reinterpret_cast<const uint16_t*>(src);
        } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            value = *reinterpret_cast<const uint32_t*>(src);
        } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            value = *reinterpret_cast<const uint8_t*>(src);
        } else {
            return false;
        }
        (*out)[i] = value;
    }
    return true;
}

static bool ReadAccessorUint4(const tinygltf::Model& model,
                              const tinygltf::Accessor& accessor,
                              std::vector<uint32_t>* out) {
    if (!out)
        return false;
    out->clear();
    if (accessor.type != TINYGLTF_TYPE_VEC4)
        return false;
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
        return false;
    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
        return false;
    const tinygltf::Buffer& buffer = model.buffers[view.buffer];
    size_t component = 0;
    switch (accessor.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: component = 1; break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: component = 2; break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: component = 4; break;
        default: return false;
    }
    size_t stride = accessor.ByteStride(view);
    if (stride == 0)
        stride = component * 4;
    const size_t base_offset = static_cast<size_t>(view.byteOffset + accessor.byteOffset);
    const size_t required_bytes = (accessor.count == 0) ? 0 : (stride * (accessor.count - 1) + component * 4);
    if (base_offset + required_bytes > buffer.data.size())
        return false;
    const unsigned char* data = buffer.data.data() + base_offset;
    out->resize(accessor.count * 4);
    for (size_t i = 0; i < accessor.count; ++i) {
        const unsigned char* src = data + stride * i;
        for (int c = 0; c < 4; ++c) {
            const unsigned char* comp_src = src + c * component;
            uint32_t value = 0;
            if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                value = *reinterpret_cast<const uint8_t*>(comp_src);
            else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                value = *reinterpret_cast<const uint16_t*>(comp_src);
            else
                value = *reinterpret_cast<const uint32_t*>(comp_src);
            (*out)[i * 4 + c] = value;
        }
    }
    return true;
}

static void ComputeNormals(const std::vector<float>& positions,
                           std::vector<float>* normals) {
    size_t count = positions.size() / 3;
    normals->assign(count * 3, 0.0f);
    for (size_t i = 0; i + 2 < count; i += 3) {
        const float* p0 = &positions[i * 3];
        const float* p1 = &positions[(i + 1) * 3];
        const float* p2 = &positions[(i + 2) * 3];
        float ux = p1[0] - p0[0];
        float uy = p1[1] - p0[1];
        float uz = p1[2] - p0[2];
        float vx = p2[0] - p0[0];
        float vy = p2[1] - p0[1];
        float vz = p2[2] - p0[2];
        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;
        float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-6f) {
            nx /= len;
            ny /= len;
            nz /= len;
        }
        for (int v = 0; v < 3; ++v) {
            (*normals)[(i + v) * 3 + 0] = nx;
            (*normals)[(i + v) * 3 + 1] = ny;
            (*normals)[(i + v) * 3 + 2] = nz;
        }
    }
}

bool LoadGltfMesh(const std::string& path, GltfMesh* out_mesh, std::string* error) {
    if (!out_mesh)
        return false;
    out_mesh->mesh = voxel::VoxelRenderer::MeshData();
    out_mesh->has_uv = false;
    out_mesh->base_color_texture_path.clear();

    std::string base_path;
    std::string mesh_selector;
    SplitPathFragment(path, &base_path, &mesh_selector);
    if (base_path.empty())
        base_path = path;
    std::vector<std::string> selectors = SplitMeshSelectors(mesh_selector);

    tinygltf::Model model;
    std::string load_error;
    if (!LoadTinyGltfModelFromFile(base_path, &model, &load_error)) {
        if (error)
            *error = load_error;
        return false;
    }
    if (model.meshes.empty()) {
        if (error)
            *error = "No mesh primitives";
        return false;
    }
    std::vector<float> out_pos;
    std::vector<float> out_norm;
    std::vector<float> out_uv;
    std::vector<float> out_col;
    std::vector<uint32_t> out_joints;
    std::vector<float> out_weights;
    bool any_uv = false;

    if (MeshDebugEnabled()) {
        std::string selector_list;
        for (size_t i = 0; i < selectors.size(); ++i) {
            if (i > 0)
                selector_list += ", ";
            selector_list += selectors[i].empty() ? "<default>" : selectors[i];
        }
        std::fprintf(stderr, "glTF mesh selectors for %s: %s\n", base_path.c_str(), selector_list.c_str());
    }

    for (size_t sel_index = 0; sel_index < selectors.size(); ++sel_index) {
        const std::string& selector = selectors[sel_index];
        std::vector<int> mesh_indices;
        if (selector.empty()) {
            mesh_indices.reserve(model.meshes.size());
            for (size_t mi = 0; mi < model.meshes.size(); ++mi)
                mesh_indices.push_back(static_cast<int>(mi));
        } else {
            int mesh_index = FindMeshIndexByName(model, selector);
            if (mesh_index < 0) {
                if (error)
                    *error = std::string("Mesh not found: ") + selector;
                return false;
            }
            mesh_indices.push_back(mesh_index);
        }

        bool any_primitive_loaded = false;
        for (size_t mi = 0; mi < mesh_indices.size(); ++mi) {
            const int mesh_index = mesh_indices[mi];
            if (mesh_index < 0 || mesh_index >= static_cast<int>(model.meshes.size()))
                continue;
            if (MeshDebugEnabled()) {
                std::fprintf(stderr, "  selector '%s' -> mesh[%d] '%s'\n",
                             selector.empty() ? "<all>" : selector.c_str(),
                             mesh_index,
                             model.meshes[mesh_index].name.c_str());
            }
            const tinygltf::Mesh& mesh = model.meshes[mesh_index];
            if (mesh.primitives.empty())
                continue;
            for (size_t prim_i = 0; prim_i < mesh.primitives.size(); ++prim_i) {
            const tinygltf::Primitive& prim = mesh.primitives[prim_i];
            if (out_mesh->base_color_texture_path.empty()) {
                std::string tex_path = ResolveBaseColorTexturePath(model, prim, base_path);
                if (!tex_path.empty()) {
                    out_mesh->base_color_texture_path = tex_path;
                    std::fprintf(stderr, "glTF baseColor texture: %s\n", tex_path.c_str());
                }
            }
            auto it_pos = prim.attributes.find("POSITION");
            if (it_pos == prim.attributes.end())
                continue;

            std::vector<float> positions;
            if (!ReadAccessor(model, model.accessors[it_pos->second], &positions, 3))
                continue;

            std::vector<float> normals;
            auto it_norm = prim.attributes.find("NORMAL");
            if (it_norm != prim.attributes.end())
                ReadAccessor(model, model.accessors[it_norm->second], &normals, 3);

            std::vector<float> uvs;
            auto it_uv = prim.attributes.find("TEXCOORD_0");
            if (it_uv != prim.attributes.end()) {
                if (ReadAccessor(model, model.accessors[it_uv->second], &uvs, 2))
                    any_uv = true;
            }

            std::vector<float> colors;
            auto it_col = prim.attributes.find("COLOR_0");
            if (it_col != prim.attributes.end())
                ReadAccessor(model, model.accessors[it_col->second], &colors, 4);

            std::vector<uint32_t> joints;
            auto it_joints = prim.attributes.find("JOINTS_0");
            if (it_joints != prim.attributes.end())
                ReadAccessorUint4(model, model.accessors[it_joints->second], &joints);

            std::vector<float> weights;
            auto it_weights = prim.attributes.find("WEIGHTS_0");
            if (it_weights != prim.attributes.end())
                ReadAccessor(model, model.accessors[it_weights->second], &weights, 4);

            std::vector<uint32_t> indices;
            bool has_indices = (prim.indices >= 0);
            if (has_indices)
                ReadIndices(model, model.accessors[prim.indices], &indices);

            auto push_vertex = [&](uint32_t idx) {
                out_pos.push_back(positions[idx * 3 + 0]);
                out_pos.push_back(positions[idx * 3 + 1]);
                out_pos.push_back(positions[idx * 3 + 2]);
                if (!normals.empty()) {
                    out_norm.push_back(normals[idx * 3 + 0]);
                    out_norm.push_back(normals[idx * 3 + 1]);
                    out_norm.push_back(normals[idx * 3 + 2]);
                }
                if (!uvs.empty()) {
                    out_uv.push_back(uvs[idx * 2 + 0]);
                    out_uv.push_back(uvs[idx * 2 + 1]);
                }
                if (!colors.empty()) {
                    out_col.push_back(colors[idx * 4 + 0]);
                    out_col.push_back(colors[idx * 4 + 1]);
                    out_col.push_back(colors[idx * 4 + 2]);
                    out_col.push_back(colors[idx * 4 + 3]);
                }
                if (!joints.empty() && !weights.empty()) {
                    out_joints.push_back(joints[idx * 4 + 0]);
                    out_joints.push_back(joints[idx * 4 + 1]);
                    out_joints.push_back(joints[idx * 4 + 2]);
                    out_joints.push_back(joints[idx * 4 + 3]);
                    out_weights.push_back(weights[idx * 4 + 0]);
                    out_weights.push_back(weights[idx * 4 + 1]);
                    out_weights.push_back(weights[idx * 4 + 2]);
                    out_weights.push_back(weights[idx * 4 + 3]);
                } else {
                    out_joints.push_back(0);
                    out_joints.push_back(0);
                    out_joints.push_back(0);
                    out_joints.push_back(0);
                    out_weights.push_back(1.0f);
                    out_weights.push_back(0.0f);
                    out_weights.push_back(0.0f);
                    out_weights.push_back(0.0f);
                }
            };

            if (has_indices) {
                for (size_t i = 0; i < indices.size(); ++i)
                    push_vertex(indices[i]);
            } else {
                for (size_t i = 0; i < positions.size() / 3; ++i)
                    push_vertex((uint32_t)i);
            }
                any_primitive_loaded = true;
            }
        }
        if (!any_primitive_loaded) {
            if (error)
                *error = "Missing POSITION";
            return false;
        }
    }

    if (any_uv)
        out_mesh->has_uv = true;

    if (out_norm.empty()) {
        ComputeNormals(out_pos, &out_norm);
    }

    // Center block models if they are in 0..1 range.
    float min_x = 1e9f, min_y = 1e9f, min_z = 1e9f;
    float max_x = -1e9f, max_y = -1e9f, max_z = -1e9f;
    for (size_t i = 0; i < out_pos.size(); i += 3) {
        min_x = std::min(min_x, out_pos[i + 0]);
        min_y = std::min(min_y, out_pos[i + 1]);
        min_z = std::min(min_z, out_pos[i + 2]);
        max_x = std::max(max_x, out_pos[i + 0]);
        max_y = std::max(max_y, out_pos[i + 1]);
        max_z = std::max(max_z, out_pos[i + 2]);
    }
    if (min_x >= -0.001f && min_y >= -0.001f && min_z >= -0.001f &&
        max_x <= 1.001f && max_y <= 1.001f && max_z <= 1.001f) {
        for (size_t i = 0; i < out_pos.size(); i += 3) {
            out_pos[i + 0] -= 0.5f;
            out_pos[i + 1] -= 0.5f;
            out_pos[i + 2] -= 0.5f;
        }
    }

    out_mesh->mesh.positions = out_pos;
    out_mesh->mesh.normals = out_norm;
    out_mesh->mesh.uvs = out_uv;
    out_mesh->mesh.colors = out_col;
    out_mesh->mesh.joints = out_joints;
    out_mesh->mesh.weights = out_weights;
    return true;
}

static float SampleAnimationDuration(const tinygltf::Model& model, const tinygltf::Animation& anim) {
    float duration = 0.0f;
    for (const auto& channel : anim.channels) {
        if (channel.sampler < 0 || channel.sampler >= static_cast<int>(anim.samplers.size()))
            continue;
        const tinygltf::AnimationSampler& sampler = anim.samplers[channel.sampler];
        if (sampler.input < 0 || sampler.input >= static_cast<int>(model.accessors.size()))
            continue;
        const tinygltf::Accessor& accessor = model.accessors[sampler.input];
        if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
            continue;
        const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
        if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
            continue;
        const tinygltf::Buffer& buffer = model.buffers[view.buffer];
        size_t stride = accessor.ByteStride(view);
        if (stride == 0)
            stride = sizeof(float);
        if (accessor.count == 0)
            continue;
        const size_t base_offset = static_cast<size_t>(view.byteOffset + accessor.byteOffset);
        const size_t required_bytes = stride * (accessor.count - 1) + sizeof(float);
        if (base_offset + required_bytes > buffer.data.size())
            continue;
        const unsigned char* data = buffer.data.data() + base_offset;
        const float* last = reinterpret_cast<const float*>(data + stride * (accessor.count - 1));
        duration = std::max(duration, *last);
    }
    return duration;
}

struct AnimVec3Track {
    std::vector<float> times;
    std::vector<float> values; // xyz per key
};

struct AnimQuatTrack {
    std::vector<float> times;
    std::vector<float> values; // xyzw per key
};

struct NodeAnimTracks {
    AnimVec3Track translation;
    AnimQuatTrack rotation;
    AnimVec3Track scale;
};

struct Float3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    Float3() = default;
    Float3(float xx, float yy, float zz) : x(xx), y(yy), z(zz) {}
};

struct Float4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
    Float4() = default;
    Float4(float xx, float yy, float zz, float ww) : x(xx), y(yy), z(zz), w(ww) {}
};

struct Mat4f {
    float m[16];
};

static Mat4f Mat4Identity() {
    Mat4f out = {};
    out.m[0] = out.m[5] = out.m[10] = out.m[15] = 1.0f;
    return out;
}

static Mat4f Mat4Multiply(const Mat4f& a, const Mat4f& b) {
    Mat4f r = {};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            r.m[col * 4 + row] =
                a.m[0 * 4 + row] * b.m[col * 4 + 0] +
                a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                a.m[2 * 4 + row] * b.m[col * 4 + 2] +
                a.m[3 * 4 + row] * b.m[col * 4 + 3];
        }
    }
    return r;
}

static Mat4f Mat4Translate(const Float3& t) {
    Mat4f m = Mat4Identity();
    m.m[12] = t.x;
    m.m[13] = t.y;
    m.m[14] = t.z;
    return m;
}

static Mat4f Mat4Scale(const Float3& s) {
    Mat4f m = {};
    m.m[0] = s.x;
    m.m[5] = s.y;
    m.m[10] = s.z;
    m.m[15] = 1.0f;
    return m;
}

static Float4 QuatNormalize(const Float4& q) {
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len <= 1e-8f)
        return Float4{0.0f, 0.0f, 0.0f, 1.0f};
    const float inv = 1.0f / len;
    return Float4{q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

static Mat4f Mat4FromQuat(const Float4& q_in) {
    const Float4 q = QuatNormalize(q_in);
    const float x = q.x;
    const float y = q.y;
    const float z = q.z;
    const float w = q.w;
    Mat4f m = Mat4Identity();
    m.m[0] = 1.0f - 2.0f * (y * y + z * z);
    m.m[1] = 2.0f * (x * y + z * w);
    m.m[2] = 2.0f * (x * z - y * w);
    m.m[4] = 2.0f * (x * y - z * w);
    m.m[5] = 1.0f - 2.0f * (x * x + z * z);
    m.m[6] = 2.0f * (y * z + x * w);
    m.m[8] = 2.0f * (x * z + y * w);
    m.m[9] = 2.0f * (y * z - x * w);
    m.m[10] = 1.0f - 2.0f * (x * x + y * y);
    return m;
}

static Float3 SampleTrackVec3(const AnimVec3Track& track, float t, const Float3& fallback) {
    const size_t count = track.times.size();
    if (count == 0 || track.values.size() < count * 3)
        return fallback;
    if (count == 1 || t <= track.times.front())
        return Float3{track.values[0], track.values[1], track.values[2]};
    if (t >= track.times.back()) {
        const size_t i = (count - 1) * 3;
        return Float3{track.values[i + 0], track.values[i + 1], track.values[i + 2]};
    }
    size_t k = 0;
    while (k + 1 < count && !(t >= track.times[k] && t <= track.times[k + 1]))
        ++k;
    const float t0 = track.times[k];
    const float t1 = track.times[k + 1];
    const float a = (t1 > t0) ? ((t - t0) / (t1 - t0)) : 0.0f;
    const size_t i0 = k * 3;
    const size_t i1 = (k + 1) * 3;
    return Float3{
        track.values[i0 + 0] + (track.values[i1 + 0] - track.values[i0 + 0]) * a,
        track.values[i0 + 1] + (track.values[i1 + 1] - track.values[i0 + 1]) * a,
        track.values[i0 + 2] + (track.values[i1 + 2] - track.values[i0 + 2]) * a,
    };
}

static Float4 SampleTrackQuat(const AnimQuatTrack& track, float t, const Float4& fallback) {
    const size_t count = track.times.size();
    if (count == 0 || track.values.size() < count * 4)
        return fallback;
    if (count == 1 || t <= track.times.front())
        return QuatNormalize(Float4{track.values[0], track.values[1], track.values[2], track.values[3]});
    if (t >= track.times.back()) {
        const size_t i = (count - 1) * 4;
        return QuatNormalize(Float4{track.values[i + 0], track.values[i + 1], track.values[i + 2], track.values[i + 3]});
    }
    size_t k = 0;
    while (k + 1 < count && !(t >= track.times[k] && t <= track.times[k + 1]))
        ++k;
    const float t0 = track.times[k];
    const float t1 = track.times[k + 1];
    const float a = (t1 > t0) ? ((t - t0) / (t1 - t0)) : 0.0f;
    const size_t i0 = k * 4;
    const size_t i1 = (k + 1) * 4;
    Float4 q0{track.values[i0 + 0], track.values[i0 + 1], track.values[i0 + 2], track.values[i0 + 3]};
    Float4 q1{track.values[i1 + 0], track.values[i1 + 1], track.values[i1 + 2], track.values[i1 + 3]};
    float dot = q0.x * q1.x + q0.y * q1.y + q0.z * q1.z + q0.w * q1.w;
    if (dot < 0.0f) {
        q1.x = -q1.x;
        q1.y = -q1.y;
        q1.z = -q1.z;
        q1.w = -q1.w;
    }
    Float4 q{
        q0.x + (q1.x - q0.x) * a,
        q0.y + (q1.y - q0.y) * a,
        q0.z + (q1.z - q0.z) * a,
        q0.w + (q1.w - q0.w) * a,
    };
    return QuatNormalize(q);
}

static Mat4f ComposeTRS(const Float3& t, const Float4& r, const Float3& s) {
    return Mat4Multiply(Mat4Translate(t), Mat4Multiply(Mat4FromQuat(r), Mat4Scale(s)));
}

static bool ReadAccessorMat4(const tinygltf::Model& model,
                             const tinygltf::Accessor& accessor,
                             std::vector<float>* out) {
    if (!out)
        return false;
    out->clear();
    if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || accessor.type != TINYGLTF_TYPE_MAT4)
        return false;
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
        return false;
    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
        return false;
    const tinygltf::Buffer& buffer = model.buffers[view.buffer];
    size_t stride = accessor.ByteStride(view);
    if (stride == 0)
        stride = sizeof(float) * 16;
    const size_t base_offset = static_cast<size_t>(view.byteOffset + accessor.byteOffset);
    const size_t required_bytes = (accessor.count == 0) ? 0 : (stride * (accessor.count - 1) + sizeof(float) * 16);
    if (base_offset + required_bytes > buffer.data.size())
        return false;
    out->resize(accessor.count * 16);
    const unsigned char* data = buffer.data.data() + base_offset;
    for (size_t i = 0; i < accessor.count; ++i) {
        const float* src = reinterpret_cast<const float*>(data + stride * i);
        for (int k = 0; k < 16; ++k)
            (*out)[i * 16 + k] = src[k];
    }
    return true;
}

bool LoadGltfAnimationLibrary(const std::string& path,
                              GltfAnimationLibrary* out_library,
                              std::string* error) {
    if (!out_library)
        return false;

    std::string base_path;
    std::string fragment;
    SplitPathFragment(path, &base_path, &fragment);
    if (base_path.empty())
        base_path = path;

    auto cache_it = g_animation_cache.find(base_path);
    if (cache_it != g_animation_cache.end() && cache_it->second.loaded) {
        *out_library = cache_it->second.library;
        if (error)
            *error = cache_it->second.error;
        return cache_it->second.error.empty();
    }

    AnimationCacheEntry entry;
    entry.loaded = true;

    tinygltf::Model model;
    std::string load_error;
    if (!LoadTinyGltfModelFromFile(base_path, &model, &load_error)) {
        entry.error = load_error;
        if (error)
            *error = entry.error;
        g_animation_cache[base_path] = entry;
        return false;
    }

    if (model.animations.empty()) {
        entry.error = "No animations";
        if (error)
            *error = entry.error;
        g_animation_cache[base_path] = entry;
        return false;
    }

    for (const auto& anim : model.animations) {
        GltfAnimationClip clip;
        clip.name = anim.name.empty() ? "default" : anim.name;
        clip.duration = SampleAnimationDuration(model, anim);
        entry.library.clips.push_back(clip);

        int bound_channels = 0;
        int missing_targets = 0;
        for (size_t ci = 0; ci < anim.channels.size(); ++ci) {
            const tinygltf::AnimationChannel& channel = anim.channels[ci];
            if (channel.target_node >= 0 && channel.target_node < static_cast<int>(model.nodes.size()))
                bound_channels += 1;
            else
                missing_targets += 1;
        }
        std::fprintf(stderr,
                     "Animation binding '%s': channels_bound=%d missing_targets=%d\n",
                     clip.name.c_str(),
                     bound_channels,
                     missing_targets);
    }

    *out_library = entry.library;
    g_animation_cache[base_path] = entry;
    if (error)
        *error = entry.error;
    return true;
}

bool LoadGltfSkinningFrames(const std::string& model_path,
                            const std::string& animation_path,
                            GltfSkinningFrames* out_frames,
                            std::string* error) {
    if (!out_frames)
        return false;
    *out_frames = GltfSkinningFrames();

    std::string model_base;
    std::string model_fragment;
    SplitPathFragment(model_path, &model_base, &model_fragment);
    if (model_base.empty())
        model_base = model_path;

    tinygltf::Model model;
    std::string model_error;
    if (!LoadTinyGltfModelFromFile(model_base, &model, &model_error)) {
        if (error)
            *error = model_error;
        return false;
    }
    if (model.skins.empty()) {
        if (error)
            *error = "No skins in model";
        return false;
    }

    const tinygltf::Skin& skin = model.skins[0];
    if (skin.joints.empty()) {
        if (error)
            *error = "Skin has no joints";
        return false;
    }

    std::vector<Mat4f> inverse_bind;
    inverse_bind.resize(skin.joints.size(), Mat4Identity());
    if (skin.inverseBindMatrices >= 0 && skin.inverseBindMatrices < static_cast<int>(model.accessors.size())) {
        std::vector<float> ibm;
        if (ReadAccessorMat4(model, model.accessors[skin.inverseBindMatrices], &ibm)) {
            const size_t count = std::min(skin.joints.size(), ibm.size() / 16);
            for (size_t i = 0; i < count; ++i)
                std::memcpy(inverse_bind[i].m, &ibm[i * 16], sizeof(float) * 16);
        }
    }

    std::vector<int> parents(model.nodes.size(), -1);
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        for (size_t c = 0; c < model.nodes[i].children.size(); ++c) {
            int child = model.nodes[i].children[c];
            if (child >= 0 && child < static_cast<int>(parents.size()))
                parents[child] = static_cast<int>(i);
        }
    }

    std::vector<Float3> default_t(model.nodes.size(), Float3{0.0f, 0.0f, 0.0f});
    std::vector<Float4> default_r(model.nodes.size(), Float4{0.0f, 0.0f, 0.0f, 1.0f});
    std::vector<Float3> default_s(model.nodes.size(), Float3{1.0f, 1.0f, 1.0f});
    std::vector<Mat4f> default_matrix(model.nodes.size(), Mat4Identity());
    std::vector<bool> has_matrix(model.nodes.size(), false);
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        const tinygltf::Node& n = model.nodes[i];
        if (n.translation.size() == 3)
            default_t[i] = Float3{(float)n.translation[0], (float)n.translation[1], (float)n.translation[2]};
        if (n.rotation.size() == 4)
            default_r[i] = QuatNormalize(Float4{(float)n.rotation[0], (float)n.rotation[1], (float)n.rotation[2], (float)n.rotation[3]});
        if (n.scale.size() == 3)
            default_s[i] = Float3{(float)n.scale[0], (float)n.scale[1], (float)n.scale[2]};
        if (n.matrix.size() == 16) {
            for (int k = 0; k < 16; ++k)
                default_matrix[i].m[k] = (float)n.matrix[k];
            has_matrix[i] = true;
        } else {
            default_matrix[i] = ComposeTRS(default_t[i], default_r[i], default_s[i]);
        }
    }

    std::string anim_base;
    std::string anim_fragment;
    SplitPathFragment(animation_path, &anim_base, &anim_fragment);
    if (anim_base.empty())
        anim_base = animation_path;

    tinygltf::Model anim_model = model;
    if (!anim_base.empty() && anim_base != model_base) {
        std::string anim_error;
        if (!LoadTinyGltfModelFromFile(anim_base, &anim_model, &anim_error)) {
            if (error)
                *error = anim_error;
            return false;
        }
    }
    if (anim_model.animations.empty()) {
        if (error)
            *error = "No animations";
        return false;
    }

    std::unordered_map<std::string, int> model_node_by_name;
    std::unordered_map<std::string, int> model_node_by_canonical_name;
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        if (!model.nodes[i].name.empty()) {
            model_node_by_name[model.nodes[i].name] = static_cast<int>(i);
            const std::string canonical = CanonicalNodeName(model.nodes[i].name);
            if (!canonical.empty() && model_node_by_canonical_name.find(canonical) == model_node_by_canonical_name.end())
                model_node_by_canonical_name[canonical] = static_cast<int>(i);
        }
    }

    const tinygltf::Animation& anim = anim_model.animations[0];
    std::vector<NodeAnimTracks> tracks(model.nodes.size());
    float duration = SampleAnimationDuration(anim_model, anim);
    int mapped_by_name = 0;
    int mapped_by_canonical_name = 0;
    int skipped_unmapped = 0;
    for (size_t ci = 0; ci < anim.channels.size(); ++ci) {
        const tinygltf::AnimationChannel& ch = anim.channels[ci];
        if (ch.sampler < 0 || ch.sampler >= static_cast<int>(anim.samplers.size()))
            continue;
        const tinygltf::AnimationSampler& sampler = anim.samplers[ch.sampler];
        if (sampler.input < 0 || sampler.input >= static_cast<int>(anim_model.accessors.size()) ||
            sampler.output < 0 || sampler.output >= static_cast<int>(anim_model.accessors.size()))
            continue;

        int model_node = -1;
        if (&anim_model == &model) {
            model_node = ch.target_node;
        } else if (ch.target_node >= 0 && ch.target_node < static_cast<int>(anim_model.nodes.size())) {
            const std::string& n = anim_model.nodes[ch.target_node].name;
            auto it = model_node_by_name.find(n);
            if (it != model_node_by_name.end())
                model_node = it->second;
            if (model_node < 0) {
                const std::string canonical = CanonicalNodeName(n);
                auto it2 = model_node_by_canonical_name.find(canonical);
                if (it2 != model_node_by_canonical_name.end()) {
                    model_node = it2->second;
                    mapped_by_canonical_name += 1;
                }
            }
            if (model_node >= 0)
                mapped_by_name += 1;
        }
        if (model_node < 0 || model_node >= static_cast<int>(tracks.size())) {
            skipped_unmapped += 1;
            continue;
        }

        std::vector<float> in_times;
        if (!ReadAccessor(anim_model, anim_model.accessors[sampler.input], &in_times, 1))
            continue;
        if (!in_times.empty())
            duration = std::max(duration, in_times.back());

        if (ch.target_path == "translation") {
            std::vector<float> out_vals;
            if (!ReadAccessor(anim_model, anim_model.accessors[sampler.output], &out_vals, 3))
                continue;
            tracks[model_node].translation.times = in_times;
            tracks[model_node].translation.values = out_vals;
        } else if (ch.target_path == "rotation") {
            std::vector<float> out_vals;
            if (!ReadAccessor(anim_model, anim_model.accessors[sampler.output], &out_vals, 4))
                continue;
            tracks[model_node].rotation.times = in_times;
            tracks[model_node].rotation.values = out_vals;
        } else if (ch.target_path == "scale") {
            std::vector<float> out_vals;
            if (!ReadAccessor(anim_model, anim_model.accessors[sampler.output], &out_vals, 3))
                continue;
            tracks[model_node].scale.times = in_times;
            tracks[model_node].scale.values = out_vals;
        }
    }

    if (&anim_model != &model && skipped_unmapped > 0) {
        std::fprintf(stderr,
                     "Animation remap: skipped %d channels with no node-name match (mapped_by_name=%d canonical=%d)\n",
                     skipped_unmapped,
                     mapped_by_name,
                     mapped_by_canonical_name);
    }

    const float sample_fps = 30.0f;
    uint32_t frame_count = 1;
    if (duration > 0.0001f)
        frame_count = std::max<uint32_t>(2u, static_cast<uint32_t>(std::ceil(duration * sample_fps)) + 1u);

    out_frames->joint_count = static_cast<uint32_t>(skin.joints.size());
    out_frames->frame_count = frame_count;
    out_frames->duration = duration;
    out_frames->palettes.resize(static_cast<size_t>(frame_count) * skin.joints.size() * 16u, 0.0f);

    std::vector<Mat4f> local(model.nodes.size(), Mat4Identity());
    std::vector<Mat4f> global(model.nodes.size(), Mat4Identity());
    for (uint32_t fi = 0; fi < frame_count; ++fi) {
        const float t = (frame_count > 1) ? (duration * (float(fi) / float(frame_count - 1))) : 0.0f;

        for (size_t ni = 0; ni < model.nodes.size(); ++ni) {
            if (has_matrix[ni]) {
                local[ni] = default_matrix[ni];
                continue;
            }
            const NodeAnimTracks& tr = tracks[ni];
            const Float3 tt = SampleTrackVec3(tr.translation, t, default_t[ni]);
            const Float4 rr = SampleTrackQuat(tr.rotation, t, default_r[ni]);
            const Float3 ss = SampleTrackVec3(tr.scale, t, default_s[ni]);
            local[ni] = ComposeTRS(tt, rr, ss);
        }

        std::vector<uint8_t> global_ready(model.nodes.size(), 0u);
        std::function<void(size_t)> compute_global = [&](size_t ni) {
            if (global_ready[ni])
                return;
            const int p = parents[ni];
            if (p >= 0) {
                compute_global(static_cast<size_t>(p));
                global[ni] = Mat4Multiply(global[static_cast<size_t>(p)], local[ni]);
            } else {
                global[ni] = local[ni];
            }
            global_ready[ni] = 1u;
        };
        for (size_t ni = 0; ni < model.nodes.size(); ++ni)
            compute_global(ni);

        for (size_t ji = 0; ji < skin.joints.size(); ++ji) {
            const int node_index = skin.joints[ji];
            Mat4f joint_mat = Mat4Identity();
            if (node_index >= 0 && node_index < static_cast<int>(global.size()))
                joint_mat = Mat4Multiply(global[(size_t)node_index], inverse_bind[ji]);
            const size_t dst = (static_cast<size_t>(fi) * skin.joints.size() + ji) * 16u;
            std::memcpy(&out_frames->palettes[dst], joint_mat.m, sizeof(float) * 16);
        }
    }

    return true;
}