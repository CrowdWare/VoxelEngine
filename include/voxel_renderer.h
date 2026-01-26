/*
 * Copyright (C) 2026 CrowdWare
 *
 * This file is part of VoxelEngine.
 *
 *  VoxelEngine is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  VoxelEngine is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with VoxelEngine.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef VOXEL_RENDERER_H
#define VOXEL_RENDERER_H

#include <vulkan/vulkan.h>
#include <vector>

namespace voxel {

class VoxelRenderer {
public:
    struct Block {
        float x;
        float y;
        float z;
    };

    VoxelRenderer();
    bool init(VkDevice device,
              VkPhysicalDevice physical_device,
              VkQueue queue,
              uint32_t queue_family,
              VkRenderPass render_pass,
              const char* vertex_shader_path,
              const char* fragment_shader_path,
              const char* pick_vertex_shader_path,
              const char* pick_fragment_shader_path,
              const char* ground_texture_path,
              const char* cube_texture_path);
    void shutdown();
    void render(VkCommandBuffer cmd, int width, int height);
    void setCamera(float x, float y, float z, float yaw_radians, float pitch_radians);
    void setBlocks(const std::vector<Block>& blocks, float block_size);
    void setSelection(const std::vector<unsigned char>& selected_flags);
    void resizePickResources(uint32_t width, uint32_t height);
    bool pickRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, std::vector<unsigned char>* out_flags);

private:
    struct Vertex {
        float pos[3];
        float color[3];
        float normal[3];
        float uv[2];
    };

public:
    struct Mat4 {
        float m[16];
    };

private:
    bool createShaderModule(const char* path, VkShaderModule* out_module);
    bool createVertexBuffer(const Vertex* vertices, size_t count, VkBuffer* out_buffer, VkDeviceMemory* out_memory);
    uint32_t findMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties) const;
    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* out_buffer, VkDeviceMemory* out_memory);
    bool createTextureImage(const char* path, VkImage* out_image, VkDeviceMemory* out_memory, VkImageView* out_view);
    void transitionImageLayout(VkCommandBuffer cmd, VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout);
    void copyBufferToImage(VkCommandBuffer cmd, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

    Mat4 mat4Identity() const;
    Mat4 mat4Multiply(const Mat4& a, const Mat4& b) const;
    Mat4 mat4Perspective(float fovy_radians, float aspect, float znear, float zfar) const;
    Mat4 mat4LookAt(float eye_x, float eye_y, float eye_z,
                    float at_x, float at_y, float at_z,
                    float up_x, float up_y, float up_z) const;
    Mat4 mat4Translate(float x, float y, float z) const;

    VkDevice device_;
    VkPhysicalDevice physical_device_;
    VkQueue queue_;
    uint32_t queue_family_;
    VkRenderPass render_pass_;
    VkPipelineLayout pipeline_layout_;
    VkPipeline pipeline_;
    VkShaderModule vert_shader_;
    VkShaderModule frag_shader_;
    VkDescriptorSetLayout descriptor_set_layout_;
    VkDescriptorPool descriptor_pool_;
    VkDescriptorSet descriptor_set_;
    VkSampler texture_sampler_;
    VkImage ground_texture_image_;
    VkDeviceMemory ground_texture_memory_;
    VkImageView ground_texture_view_;
    VkImage cube_texture_image_;
    VkDeviceMemory cube_texture_memory_;
    VkImageView cube_texture_view_;
    VkBuffer ground_buffer_;
    VkDeviceMemory ground_memory_;
    VkBuffer cube_buffer_;
    VkDeviceMemory cube_memory_;
    uint32_t ground_vertex_count_;
    uint32_t cube_vertex_count_;
    float camera_pos_[3];
    float camera_yaw_;
    float camera_pitch_;
    std::vector<Block> blocks_;
    float block_scale_;
    std::vector<unsigned char> selected_flags_;

    VkRenderPass pick_render_pass_;
    VkPipelineLayout pick_pipeline_layout_;
    VkPipeline pick_pipeline_;
    VkShaderModule pick_vert_shader_;
    VkShaderModule pick_frag_shader_;
    VkImage pick_image_;
    VkDeviceMemory pick_image_memory_;
    VkImageView pick_image_view_;
    VkImage pick_depth_image_;
    VkDeviceMemory pick_depth_memory_;
    VkImageView pick_depth_view_;
    VkFramebuffer pick_framebuffer_;
    VkExtent2D pick_extent_;
    VkCommandPool pick_command_pool_;
    VkCommandBuffer pick_command_buffer_;
    VkFence pick_fence_;
};

} // namespace voxel

#endif
