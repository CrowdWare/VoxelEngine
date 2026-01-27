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

#include "voxel_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "../third_party/stb_image.h"

namespace voxel {

VoxelRenderer::VoxelRenderer()
    : device_(VK_NULL_HANDLE)
    , physical_device_(VK_NULL_HANDLE)
    , queue_(VK_NULL_HANDLE)
    , queue_family_(0)
    , render_pass_(VK_NULL_HANDLE)
    , pipeline_layout_(VK_NULL_HANDLE)
    , pipeline_(VK_NULL_HANDLE)
    , vert_shader_(VK_NULL_HANDLE)
    , frag_shader_(VK_NULL_HANDLE)
    , descriptor_set_layout_(VK_NULL_HANDLE)
    , descriptor_pool_(VK_NULL_HANDLE)
    , descriptor_set_(VK_NULL_HANDLE)
    , texture_sampler_(VK_NULL_HANDLE)
    , ground_texture_image_(VK_NULL_HANDLE)
    , ground_texture_memory_(VK_NULL_HANDLE)
    , ground_texture_view_(VK_NULL_HANDLE)
    , ground_buffer_(VK_NULL_HANDLE)
    , ground_memory_(VK_NULL_HANDLE)
    , cube_buffer_(VK_NULL_HANDLE)
    , cube_memory_(VK_NULL_HANDLE)
    , ground_vertex_count_(0)
    , cube_vertex_count_(0)
    , camera_yaw_(0.0f)
    , camera_pitch_(0.0f)
    , block_scale_(1.0f)
    , pick_render_pass_(VK_NULL_HANDLE)
    , pick_pipeline_layout_(VK_NULL_HANDLE)
    , pick_pipeline_(VK_NULL_HANDLE)
    , pick_vert_shader_(VK_NULL_HANDLE)
    , pick_frag_shader_(VK_NULL_HANDLE)
    , pick_image_(VK_NULL_HANDLE)
    , pick_image_memory_(VK_NULL_HANDLE)
    , pick_image_view_(VK_NULL_HANDLE)
    , pick_depth_image_(VK_NULL_HANDLE)
    , pick_depth_memory_(VK_NULL_HANDLE)
    , pick_depth_view_(VK_NULL_HANDLE)
    , pick_framebuffer_(VK_NULL_HANDLE)
    , pick_extent_()
    , pick_command_pool_(VK_NULL_HANDLE)
    , pick_command_buffer_(VK_NULL_HANDLE)
    , pick_fence_(VK_NULL_HANDLE) {
    camera_pos_[0] = 6.0f;
    camera_pos_[1] = 6.0f;
    camera_pos_[2] = 6.0f;
}

static float ToRadians(float degrees) {
    return degrees * 0.01745329252f;
}

VoxelRenderer::Mat4 VoxelRenderer::mat4Identity() const {
    Mat4 m = {};
    m.m[0] = 1.0f;
    m.m[5] = 1.0f;
    m.m[10] = 1.0f;
    m.m[15] = 1.0f;
    return m;
}

VoxelRenderer::Mat4 VoxelRenderer::mat4Multiply(const Mat4& a, const Mat4& b) const {
    Mat4 r = {};
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

VoxelRenderer::Mat4 VoxelRenderer::mat4Perspective(float fovy_radians, float aspect, float znear, float zfar) const {
    Mat4 m = {};
    float f = 1.0f / std::tan(fovy_radians * 0.5f);
    m.m[0] = f / aspect;
    m.m[5] = f;
    m.m[10] = zfar / (zfar - znear);
    m.m[11] = 1.0f;
    m.m[14] = (-znear * zfar) / (zfar - znear);
    return m;
}

VoxelRenderer::Mat4 VoxelRenderer::mat4LookAt(float eye_x, float eye_y, float eye_z,
                                              float at_x, float at_y, float at_z,
                                              float up_x, float up_y, float up_z) const {
    float fx = at_x - eye_x;
    float fy = at_y - eye_y;
    float fz = at_z - eye_z;
    float flen = std::sqrt(fx * fx + fy * fy + fz * fz);
    fx /= flen;
    fy /= flen;
    fz /= flen;

    float sx = fy * up_z - fz * up_y;
    float sy = fz * up_x - fx * up_z;
    float sz = fx * up_y - fy * up_x;
    float slen = std::sqrt(sx * sx + sy * sy + sz * sz);
    sx /= slen;
    sy /= slen;
    sz /= slen;

    float ux = sy * fz - sz * fy;
    float uy = sz * fx - sx * fz;
    float uz = sx * fy - sy * fx;

    Mat4 m = mat4Identity();
    m.m[0] = sx;
    m.m[4] = sy;
    m.m[8] = sz;
    m.m[1] = ux;
    m.m[5] = uy;
    m.m[9] = uz;
    m.m[2] = fx;
    m.m[6] = fy;
    m.m[10] = fz;
    m.m[12] = -(sx * eye_x + sy * eye_y + sz * eye_z);
    m.m[13] = -(ux * eye_x + uy * eye_y + uz * eye_z);
    m.m[14] = -(fx * eye_x + fy * eye_y + fz * eye_z);
    return m;
}

VoxelRenderer::Mat4 VoxelRenderer::mat4Translate(float x, float y, float z) const {
    Mat4 m = mat4Identity();
    m.m[12] = x;
    m.m[13] = y;
    m.m[14] = z;
    return m;
}

VoxelRenderer::Mat4 VoxelRenderer::mat4RotateX(float radians) const {
    Mat4 m = mat4Identity();
    float c = std::cos(radians);
    float s = std::sin(radians);
    m.m[5] = c;
    m.m[6] = s;
    m.m[9] = -s;
    m.m[10] = c;
    return m;
}

VoxelRenderer::Mat4 VoxelRenderer::mat4RotateY(float radians) const {
    Mat4 m = mat4Identity();
    float c = std::cos(radians);
    float s = std::sin(radians);
    m.m[0] = c;
    m.m[2] = -s;
    m.m[8] = s;
    m.m[10] = c;
    return m;
}

VoxelRenderer::Mat4 VoxelRenderer::mat4RotateZ(float radians) const {
    Mat4 m = mat4Identity();
    float c = std::cos(radians);
    float s = std::sin(radians);
    m.m[0] = c;
    m.m[1] = s;
    m.m[4] = -s;
    m.m[5] = c;
    return m;
}

static VoxelRenderer::Mat4 mat4ScaleInternal(float x, float y, float z) {
    VoxelRenderer::Mat4 m = {};
    m.m[0] = x;
    m.m[5] = y;
    m.m[10] = z;
    m.m[15] = 1.0f;
    return m;
}

uint32_t VoxelRenderer::findMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_properties);
    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((type_filter & (1u << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    return 0;
}

bool VoxelRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* out_buffer, VkDeviceMemory* out_memory) {
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &buffer_info, nullptr, out_buffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device_, *out_buffer, &mem_reqs);

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = findMemoryType(mem_reqs.memoryTypeBits, properties);
    if (vkAllocateMemory(device_, &alloc_info, nullptr, out_memory) != VK_SUCCESS)
        return false;

    vkBindBufferMemory(device_, *out_buffer, *out_memory, 0);
    return true;
}

void VoxelRenderer::transitionImageLayout(VkCommandBuffer cmd, VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout) {
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.aspectMask = (new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
                                              ? VK_IMAGE_ASPECT_DEPTH_BIT
                                              : VK_IMAGE_ASPECT_COLOR_BIT;

    VkPipelineStageFlags source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dest_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = 0;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dest_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dest_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(cmd, source_stage, dest_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VoxelRenderer::copyBufferToImage(VkCommandBuffer cmd, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

bool VoxelRenderer::createTextureImage(const char* path, VkImage* out_image, VkDeviceMemory* out_memory, VkImageView* out_view) {
    int tex_w = 0, tex_h = 0, tex_comp = 0;
    unsigned char* pixels = stbi_load(path, &tex_w, &tex_h, &tex_comp, 4);
    if (!pixels)
        return false;

    VkDeviceSize image_size = (VkDeviceSize)tex_w * (VkDeviceSize)tex_h * 4;
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    if (!createBuffer(image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &staging_buffer, &staging_memory))
        return false;

    void* data = nullptr;
    vkMapMemory(device_, staging_memory, 0, image_size, 0, &data);
    std::memcpy(data, pixels, (size_t)image_size);
    vkUnmapMemory(device_, staging_memory);
    stbi_image_free(pixels);

    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent = {(uint32_t)tex_w, (uint32_t)tex_h, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device_, &image_info, nullptr, out_image) != VK_SUCCESS)
        return false;
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(device_, *out_image, &mem_reqs);
    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = findMemoryType(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device_, &alloc_info, nullptr, out_memory) != VK_SUCCESS)
        return false;
    vkBindImageMemory(device_, *out_image, *out_memory, 0);

    VkCommandBuffer cmd = pick_command_buffer_;
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);
    transitionImageLayout(cmd, *out_image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(cmd, staging_buffer, *out_image, (uint32_t)tex_w, (uint32_t)tex_h);
    transitionImageLayout(cmd, *out_image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(queue_, 1, &submit, pick_fence_);
    vkWaitForFences(device_, 1, &pick_fence_, VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &pick_fence_);

    vkDestroyBuffer(device_, staging_buffer, nullptr);
    vkFreeMemory(device_, staging_memory, nullptr);

    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = *out_image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &view_info, nullptr, out_view) != VK_SUCCESS)
        return false;

    return true;
}
bool VoxelRenderer::createShaderModule(const char* path, VkShaderModule* out_module) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        return false;
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), size);
    file.close();

    VkShaderModuleCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = buffer.size();
    info.pCode = reinterpret_cast<const uint32_t*>(buffer.data());
    return vkCreateShaderModule(device_, &info, nullptr, out_module) == VK_SUCCESS;
}

bool VoxelRenderer::createVertexBuffer(const Vertex* vertices, size_t count, VkBuffer* out_buffer, VkDeviceMemory* out_memory) {
    VkDeviceSize buffer_size = sizeof(Vertex) * count;

    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &buffer_info, nullptr, out_buffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device_, *out_buffer, &mem_reqs);

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = findMemoryType(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device_, &alloc_info, nullptr, out_memory) != VK_SUCCESS)
        return false;

    vkBindBufferMemory(device_, *out_buffer, *out_memory, 0);

    void* data = nullptr;
    vkMapMemory(device_, *out_memory, 0, buffer_size, 0, &data);
    std::memcpy(data, vertices, static_cast<size_t>(buffer_size));
    vkUnmapMemory(device_, *out_memory);

    return true;
}

static const uint32_t kMaxBlockTextures = 8;

bool VoxelRenderer::init(VkDevice device,
                         VkPhysicalDevice physical_device,
                         VkQueue queue,
                         uint32_t queue_family,
                         VkRenderPass render_pass,
                         const char* vertex_shader_path,
                         const char* fragment_shader_path,
                         const char* pick_vertex_shader_path,
                         const char* pick_fragment_shader_path,
                         const char* ground_texture_path,
                         const std::vector<std::string>& block_texture_paths) {
    device_ = device;
    physical_device_ = physical_device;
    queue_ = queue;
    queue_family_ = queue_family;
    render_pass_ = render_pass;

    if (!createShaderModule(vertex_shader_path, &vert_shader_))
        return false;
    if (!createShaderModule(fragment_shader_path, &frag_shader_))
        return false;
    if (!createShaderModule(pick_vertex_shader_path, &pick_vert_shader_))
        return false;
    if (!createShaderModule(pick_fragment_shader_path, &pick_frag_shader_))
        return false;

    VkDescriptorSetLayoutBinding sampler_bindings[2] = {};
    sampler_bindings[0].binding = 0;
    sampler_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler_bindings[0].descriptorCount = 1;
    sampler_bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    sampler_bindings[1].binding = 1;
    sampler_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler_bindings[1].descriptorCount = kMaxBlockTextures;
    sampler_bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo desc_layout_info = {};
    desc_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    desc_layout_info.bindingCount = 2;
    desc_layout_info.pBindings = sampler_bindings;
    if (vkCreateDescriptorSetLayout(device_, &desc_layout_info, nullptr, &descriptor_set_layout_) != VK_SUCCESS)
        return false;

    VkPipelineShaderStageCreateInfo shader_stages[2] = {};
    shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = vert_shader_;
    shader_stages[0].pName = "main";
    shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = frag_shader_;
    shader_stages[1].pName = "main";

    VkVertexInputBindingDescription binding = {};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributes[4] = {};
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(Vertex, pos);
    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = offsetof(Vertex, color);
    attributes[2].binding = 0;
    attributes[2].location = 2;
    attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[2].offset = offsetof(Vertex, normal);
    attributes[3].binding = 0;
    attributes[3].location = 3;
    attributes[3].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[3].offset = offsetof(Vertex, uv);

    VkPipelineVertexInputStateCreateInfo vertex_input = {};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = 4;
    vertex_input.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster = {};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.depthClampEnable = VK_FALSE;
    raster.rasterizerDiscardEnable = VK_FALSE;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    raster.cullMode = VK_CULL_MODE_BACK_BIT;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisample = {};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState color_blend_attachment = {};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo color_blend = {};
    color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &color_blend_attachment;

    VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state = {};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    VkPushConstantRange push_range = {};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(Mat4) + sizeof(float) * 4;

    VkPipelineLayoutCreateInfo pipe_layout_info = {};
    pipe_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipe_layout_info.setLayoutCount = 1;
    pipe_layout_info.pSetLayouts = &descriptor_set_layout_;
    pipe_layout_info.pushConstantRangeCount = 1;
    pipe_layout_info.pPushConstantRanges = &push_range;

    if (vkCreatePipelineLayout(device_, &pipe_layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS)
        return false;

    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &raster;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &color_blend;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = pipeline_layout_;
    pipeline_info.renderPass = render_pass_;
    pipeline_info.subpass = 0;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_) != VK_SUCCESS)
        return false;

    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family_;
    if (vkCreateCommandPool(device_, &pool_info, nullptr, &pick_command_pool_) != VK_SUCCESS)
        return false;

    VkCommandBufferAllocateInfo cmd_info = {};
    cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_info.commandPool = pick_command_pool_;
    cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_info.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device_, &cmd_info, &pick_command_buffer_) != VK_SUCCESS)
        return false;

    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(device_, &fence_info, nullptr, &pick_fence_) != VK_SUCCESS)
        return false;

    if (!createTextureImage(ground_texture_path, &ground_texture_image_, &ground_texture_memory_, &ground_texture_view_))
        return false;

    block_textures_.clear();
    std::vector<std::string> paths = block_texture_paths;
    if (paths.empty())
        paths.push_back(ground_texture_path);
    if (paths.size() > kMaxBlockTextures)
        paths.resize(kMaxBlockTextures);
    for (size_t i = 0; i < paths.size(); ++i) {
        BlockTexture tex = {};
        if (!createTextureImage(paths[i].c_str(), &tex.image, &tex.memory, &tex.view))
            return false;
        block_textures_.push_back(tex);
    }

    VkSamplerCreateInfo sampler_info = {};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.maxAnisotropy = 1.0f;
    if (vkCreateSampler(device_, &sampler_info, nullptr, &texture_sampler_) != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize pool_size = {};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 1 + kMaxBlockTextures;
    VkDescriptorPoolCreateInfo pool_info_desc = {};
    pool_info_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info_desc.poolSizeCount = 1;
    pool_info_desc.pPoolSizes = &pool_size;
    pool_info_desc.maxSets = 1;
    if (vkCreateDescriptorPool(device_, &pool_info_desc, nullptr, &descriptor_pool_) != VK_SUCCESS)
        return false;

    VkDescriptorSetAllocateInfo alloc_info_desc = {};
    alloc_info_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info_desc.descriptorPool = descriptor_pool_;
    alloc_info_desc.descriptorSetCount = 1;
    alloc_info_desc.pSetLayouts = &descriptor_set_layout_;
    if (vkAllocateDescriptorSets(device_, &alloc_info_desc, &descriptor_set_) != VK_SUCCESS)
        return false;

    VkDescriptorImageInfo image_infos[1 + kMaxBlockTextures] = {};
    image_infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_infos[0].imageView = ground_texture_view_;
    image_infos[0].sampler = texture_sampler_;
    for (uint32_t i = 0; i < kMaxBlockTextures; ++i) {
        const BlockTexture& tex = (i < block_textures_.size()) ? block_textures_[i] : block_textures_[0];
        image_infos[1 + i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_infos[1 + i].imageView = tex.view;
        image_infos[1 + i].sampler = texture_sampler_;
    }

    VkWriteDescriptorSet writes[2] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptor_set_;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &image_infos[0];
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descriptor_set_;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = kMaxBlockTextures;
    writes[1].pImageInfo = &image_infos[1];
    vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);

    VkAttachmentDescription color_attachment = {};
    color_attachment.format = VK_FORMAT_R32_UINT;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depth_attachment = {};
    depth_attachment.format = VK_FORMAT_D32_SFLOAT;
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref = {};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_ref = {};
    depth_ref.attachment = 1;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkAttachmentDescription attachments[2] = {color_attachment, depth_attachment};
    VkRenderPassCreateInfo rp_info = {};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_info.attachmentCount = 2;
    rp_info.pAttachments = attachments;
    rp_info.subpassCount = 1;
    rp_info.pSubpasses = &subpass;
    if (vkCreateRenderPass(device_, &rp_info, nullptr, &pick_render_pass_) != VK_SUCCESS)
        return false;

    VkPipelineShaderStageCreateInfo pick_stages[2] = {};
    pick_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pick_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    pick_stages[0].module = pick_vert_shader_;
    pick_stages[0].pName = "main";
    pick_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pick_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    pick_stages[1].module = pick_frag_shader_;
    pick_stages[1].pName = "main";

    VkPipelineDepthStencilStateCreateInfo depth_state = {};
    depth_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_state.depthTestEnable = VK_TRUE;
    depth_state.depthWriteEnable = VK_TRUE;
    depth_state.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_state.depthBoundsTestEnable = VK_FALSE;
    depth_state.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState pick_blend = {};
    pick_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    pick_blend.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo pick_color_blend = {};
    pick_color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    pick_color_blend.attachmentCount = 1;
    pick_color_blend.pAttachments = &pick_blend;

    VkPushConstantRange pick_push = {};
    pick_push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pick_push.offset = 0;
    pick_push.size = sizeof(Mat4) + 16;

    VkPipelineLayoutCreateInfo pick_layout = {};
    pick_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pick_layout.pushConstantRangeCount = 1;
    pick_layout.pPushConstantRanges = &pick_push;
    if (vkCreatePipelineLayout(device_, &pick_layout, nullptr, &pick_pipeline_layout_) != VK_SUCCESS)
        return false;

    VkGraphicsPipelineCreateInfo pick_pipe = {};
    pick_pipe.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pick_pipe.stageCount = 2;
    pick_pipe.pStages = pick_stages;
    pick_pipe.pVertexInputState = &vertex_input;
    pick_pipe.pInputAssemblyState = &input_assembly;
    pick_pipe.pViewportState = &viewport_state;
    pick_pipe.pRasterizationState = &raster;
    pick_pipe.pMultisampleState = &multisample;
    pick_pipe.pDepthStencilState = &depth_state;
    pick_pipe.pColorBlendState = &pick_color_blend;
    pick_pipe.pDynamicState = &dynamic_state;
    pick_pipe.layout = pick_pipeline_layout_;
    pick_pipe.renderPass = pick_render_pass_;
    pick_pipe.subpass = 0;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pick_pipe, nullptr, &pick_pipeline_) != VK_SUCCESS)
        return false;

    const float ground_uv_scale = 1.0f / block_scale_;
    const float ground_uv_offset = 0.0f;
    Vertex ground_vertices[] = {
        {{-10.0f, 0.0f, -10.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {-10.0f * ground_uv_scale + ground_uv_offset, -10.0f * ground_uv_scale + ground_uv_offset}},
        {{ 10.0f, 0.0f, -10.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, { 10.0f * ground_uv_scale + ground_uv_offset, -10.0f * ground_uv_scale + ground_uv_offset}},
        {{ 10.0f, 0.0f,  10.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, { 10.0f * ground_uv_scale + ground_uv_offset,  10.0f * ground_uv_scale + ground_uv_offset}},
        {{-10.0f, 0.0f, -10.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {-10.0f * ground_uv_scale + ground_uv_offset, -10.0f * ground_uv_scale + ground_uv_offset}},
        {{ 10.0f, 0.0f,  10.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, { 10.0f * ground_uv_scale + ground_uv_offset,  10.0f * ground_uv_scale + ground_uv_offset}},
        {{-10.0f, 0.0f,  10.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {-10.0f * ground_uv_scale + ground_uv_offset,  10.0f * ground_uv_scale + ground_uv_offset}}
    };
    ground_vertex_count_ = 6;

    Vertex cube_vertices[] = {
        {{-0.5f, -0.5f, -0.5f}, {0.85f, 0.85f, 0.85f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.7f, 0.7f, 0.7f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.7f, 0.7f, 0.7f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
        {{-0.5f, -0.5f, -0.5f}, {0.85f, 0.85f, 0.85f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.7f, 0.7f, 0.7f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f, -0.5f}, {0.7f, 0.7f, 0.7f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},

        {{-0.5f, -0.5f,  0.5f}, {0.85f, 0.85f, 0.85f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.7f, 0.7f, 0.7f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.7f, 0.7f, 0.7f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{-0.5f, -0.5f,  0.5f}, {0.85f, 0.85f, 0.85f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{-0.5f,  0.5f,  0.5f}, {0.7f, 0.7f, 0.7f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.7f, 0.7f, 0.7f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},

        {{-0.5f,  0.5f,  0.5f}, {0.7f, 0.7f, 0.7f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{-0.5f,  0.5f, -0.5f}, {0.7f, 0.7f, 0.7f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.7f, 0.7f, 0.7f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, {0.7f, 0.7f, 0.7f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.7f, 0.7f, 0.7f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.7f, 0.7f, 0.7f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},

        {{-0.5f, -0.5f,  0.5f}, {0.7f, 0.7f, 0.7f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.7f, 0.7f, 0.7f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f, -0.5f, -0.5f}, {0.7f, 0.7f, 0.7f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
        {{-0.5f, -0.5f,  0.5f}, {0.7f, 0.7f, 0.7f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.7f, 0.7f, 0.7f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.7f, 0.7f, 0.7f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},

        {{ 0.5f, -0.5f,  0.5f}, {0.7f, 0.7f, 0.7f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.7f, 0.7f, 0.7f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.7f, 0.7f, 0.7f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.7f, 0.7f, 0.7f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.7f, 0.7f, 0.7f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.7f, 0.7f, 0.7f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},

        {{-0.5f, -0.5f,  0.5f}, {0.7f, 0.7f, 0.7f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{-0.5f, -0.5f, -0.5f}, {0.7f, 0.7f, 0.7f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{-0.5f,  0.5f, -0.5f}, {0.7f, 0.7f, 0.7f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f, -0.5f,  0.5f}, {0.7f, 0.7f, 0.7f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{-0.5f,  0.5f, -0.5f}, {0.7f, 0.7f, 0.7f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, {0.7f, 0.7f, 0.7f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}}
    };
    cube_vertex_count_ = 36;

    if (!createVertexBuffer(ground_vertices, ground_vertex_count_, &ground_buffer_, &ground_memory_))
        return false;
    if (!createVertexBuffer(cube_vertices, cube_vertex_count_, &cube_buffer_, &cube_memory_))
        return false;

    return true;
}

void VoxelRenderer::shutdown() {
    if (device_ == VK_NULL_HANDLE)
        return;
    if (ground_buffer_)
        vkDestroyBuffer(device_, ground_buffer_, nullptr);
    if (ground_memory_)
        vkFreeMemory(device_, ground_memory_, nullptr);
    if (cube_buffer_)
        vkDestroyBuffer(device_, cube_buffer_, nullptr);
    if (cube_memory_)
        vkFreeMemory(device_, cube_memory_, nullptr);
    for (size_t i = 0; i < block_meshes_.size(); ++i) {
        if (block_meshes_[i].buffer)
            vkDestroyBuffer(device_, block_meshes_[i].buffer, nullptr);
        if (block_meshes_[i].memory)
            vkFreeMemory(device_, block_meshes_[i].memory, nullptr);
    }
    block_meshes_.clear();
    if (pipeline_)
        vkDestroyPipeline(device_, pipeline_, nullptr);
    if (pipeline_layout_)
        vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
    if (vert_shader_)
        vkDestroyShaderModule(device_, vert_shader_, nullptr);
    if (frag_shader_)
        vkDestroyShaderModule(device_, frag_shader_, nullptr);
    if (descriptor_pool_)
        vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
    if (descriptor_set_layout_)
        vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_, nullptr);
    if (texture_sampler_)
        vkDestroySampler(device_, texture_sampler_, nullptr);
    if (ground_texture_view_)
        vkDestroyImageView(device_, ground_texture_view_, nullptr);
    if (ground_texture_image_)
        vkDestroyImage(device_, ground_texture_image_, nullptr);
    if (ground_texture_memory_)
        vkFreeMemory(device_, ground_texture_memory_, nullptr);
    for (size_t i = 0; i < block_textures_.size(); ++i) {
        if (block_textures_[i].view)
            vkDestroyImageView(device_, block_textures_[i].view, nullptr);
        if (block_textures_[i].image)
            vkDestroyImage(device_, block_textures_[i].image, nullptr);
        if (block_textures_[i].memory)
            vkFreeMemory(device_, block_textures_[i].memory, nullptr);
    }
    block_textures_.clear();
    if (pick_pipeline_)
        vkDestroyPipeline(device_, pick_pipeline_, nullptr);
    if (pick_pipeline_layout_)
        vkDestroyPipelineLayout(device_, pick_pipeline_layout_, nullptr);
    if (pick_vert_shader_)
        vkDestroyShaderModule(device_, pick_vert_shader_, nullptr);
    if (pick_frag_shader_)
        vkDestroyShaderModule(device_, pick_frag_shader_, nullptr);
    if (pick_framebuffer_)
        vkDestroyFramebuffer(device_, pick_framebuffer_, nullptr);
    if (pick_image_view_)
        vkDestroyImageView(device_, pick_image_view_, nullptr);
    if (pick_image_)
        vkDestroyImage(device_, pick_image_, nullptr);
    if (pick_image_memory_)
        vkFreeMemory(device_, pick_image_memory_, nullptr);
    if (pick_depth_view_)
        vkDestroyImageView(device_, pick_depth_view_, nullptr);
    if (pick_depth_image_)
        vkDestroyImage(device_, pick_depth_image_, nullptr);
    if (pick_depth_memory_)
        vkFreeMemory(device_, pick_depth_memory_, nullptr);
    if (pick_render_pass_)
        vkDestroyRenderPass(device_, pick_render_pass_, nullptr);
    if (pick_command_pool_)
        vkDestroyCommandPool(device_, pick_command_pool_, nullptr);
    if (pick_fence_)
        vkDestroyFence(device_, pick_fence_, nullptr);
    device_ = VK_NULL_HANDLE;
}

void VoxelRenderer::render(VkCommandBuffer cmd, int width, int height) {
    if (!pipeline_ || width <= 0 || height <= 0)
        return;

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    if (descriptor_set_ != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1, &descriptor_set_, 0, nullptr);
    }

    float aspect = width > 0 ? (float)width / (float)height : 1.0f;
    Mat4 proj = mat4Perspective(ToRadians(60.0f), aspect, 0.1f, 100.0f);
    proj.m[5] *= -1.0f;
    float cp = std::cos(camera_pitch_);
    float sp = std::sin(camera_pitch_);
    float cy = std::cos(camera_yaw_);
    float sy = std::sin(camera_yaw_);
    float fx = cp * cy;
    float fy = sp;
    float fz = cp * sy;
    Mat4 view = mat4LookAt(camera_pos_[0], camera_pos_[1], camera_pos_[2],
                           camera_pos_[0] + fx, camera_pos_[1] + fy, camera_pos_[2] + fz,
                           0.0f, 1.0f, 0.0f);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &ground_buffer_, &offset);
    Mat4 ground_model = mat4Identity();
    struct PushConstants {
        Mat4 mvp;
        float tint[4];
    };
    PushConstants ground_pc = {};
    ground_pc.mvp = mat4Multiply(proj, mat4Multiply(view, ground_model));
    ground_pc.tint[0] = 1.0f;
    ground_pc.tint[1] = 1.0f;
    ground_pc.tint[2] = 1.0f;
    ground_pc.tint[3] = -1.0f;
    vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &ground_pc);
    vkCmdDraw(cmd, ground_vertex_count_, 1, 0, 0);

    Mat4 scale = mat4ScaleInternal(block_scale_, block_scale_, block_scale_);
    if (!blocks_.empty()) {
        struct DrawItem {
            size_t index;
            float dist2;
        };
        std::vector<DrawItem> draw_items;
        draw_items.reserve(blocks_.size());
        for (size_t i = 0; i < blocks_.size(); ++i) {
            float dx = blocks_[i].x - camera_pos_[0];
            float dy = blocks_[i].y - camera_pos_[1];
            float dz = blocks_[i].z - camera_pos_[2];
            draw_items.push_back(DrawItem{i, dx * dx + dy * dy + dz * dz});
        }
        std::sort(draw_items.begin(), draw_items.end(),
                  [](const DrawItem& a, const DrawItem& b) { return a.dist2 > b.dist2; });

        for (size_t i = 0; i < draw_items.size(); ++i) {
            const Block& block = blocks_[draw_items[i].index];
            VkBuffer vb = cube_buffer_;
            uint32_t vcount = cube_vertex_count_;
            if (block.mesh_index >= 0 && (size_t)block.mesh_index < block_meshes_.size()) {
                const MeshBuffer& mesh = block_meshes_[block.mesh_index];
                if (mesh.buffer && mesh.vertex_count > 0) {
                    vb = mesh.buffer;
                    vcount = mesh.vertex_count;
                }
            }
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
            Mat4 translate = mat4Translate(block.x, block.y, block.z);
            Mat4 rot_x = mat4RotateX(ToRadians(block.rot_x_deg));
            Mat4 rot_y = mat4RotateY(ToRadians(block.rot_y_deg));
            Mat4 rot_z = mat4RotateZ(ToRadians(block.rot_z_deg));
            Mat4 rotate = mat4Multiply(rot_z, mat4Multiply(rot_y, rot_x));
            Mat4 model = mat4Multiply(translate, mat4Multiply(rotate, scale));
            PushConstants pc = {};
            pc.mvp = mat4Multiply(proj, mat4Multiply(view, model));
            bool selected = (draw_items[i].index < selected_flags_.size() && selected_flags_[draw_items[i].index] != 0);
            pc.tint[0] = selected ? 1.0f : 1.0f;
            pc.tint[1] = selected ? 1.0f : 1.0f;
            pc.tint[2] = selected ? 0.1f : 1.0f;
            pc.tint[3] = (float)block.tex_index;
            vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);
            vkCmdDraw(cmd, vcount, 1, 0, 0);
        }
    } else {
        vkCmdBindVertexBuffers(cmd, 0, 1, &cube_buffer_, &offset);
        Mat4 translate = mat4Translate(0.0f, 0.5f, 0.0f);
        Mat4 model = mat4Multiply(translate, scale);
        PushConstants pc = {};
        pc.mvp = mat4Multiply(proj, mat4Multiply(view, model));
        pc.tint[0] = 1.0f;
        pc.tint[1] = 1.0f;
        pc.tint[2] = 1.0f;
        pc.tint[3] = 0.0f;
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdDraw(cmd, cube_vertex_count_, 1, 0, 0);
    }
}

void VoxelRenderer::setCamera(float x, float y, float z, float yaw_radians, float pitch_radians) {
    camera_pos_[0] = x;
    camera_pos_[1] = y;
    camera_pos_[2] = z;
    camera_yaw_ = yaw_radians;
    camera_pitch_ = pitch_radians;
}

void VoxelRenderer::setBlocks(const std::vector<Block>& blocks, float block_size) {
    blocks_ = blocks;
    block_scale_ = block_size;
    selected_flags_.assign(blocks_.size(), 0);
}

void VoxelRenderer::setSelection(const std::vector<unsigned char>& selected_flags) {
    selected_flags_ = selected_flags;
}

void VoxelRenderer::setBlockMeshes(const std::vector<MeshData>& meshes) {
    for (size_t i = 0; i < block_meshes_.size(); ++i) {
        if (block_meshes_[i].buffer)
            vkDestroyBuffer(device_, block_meshes_[i].buffer, nullptr);
        if (block_meshes_[i].memory)
            vkFreeMemory(device_, block_meshes_[i].memory, nullptr);
    }
    block_meshes_.clear();

    block_meshes_.resize(meshes.size());
    for (size_t i = 0; i < meshes.size(); ++i) {
        const MeshData& mesh = meshes[i];
        size_t count = mesh.positions.size() / 3;
        if (count == 0)
            continue;

        std::vector<Vertex> verts;
        verts.resize(count);
        for (size_t v = 0; v < count; ++v) {
            verts[v].pos[0] = mesh.positions[v * 3 + 0];
            verts[v].pos[1] = mesh.positions[v * 3 + 1];
            verts[v].pos[2] = mesh.positions[v * 3 + 2];
            if (mesh.normals.size() >= (v + 1) * 3) {
                verts[v].normal[0] = mesh.normals[v * 3 + 0];
                verts[v].normal[1] = mesh.normals[v * 3 + 1];
                verts[v].normal[2] = mesh.normals[v * 3 + 2];
            } else {
                verts[v].normal[0] = 0.0f;
                verts[v].normal[1] = 1.0f;
                verts[v].normal[2] = 0.0f;
            }
            if (mesh.uvs.size() >= (v + 1) * 2) {
                verts[v].uv[0] = mesh.uvs[v * 2 + 0];
                verts[v].uv[1] = mesh.uvs[v * 2 + 1];
            } else {
                verts[v].uv[0] = 0.0f;
                verts[v].uv[1] = 0.0f;
            }
            if (mesh.colors.size() >= (v + 1) * 4) {
                verts[v].color[0] = mesh.colors[v * 4 + 0];
                verts[v].color[1] = mesh.colors[v * 4 + 1];
                verts[v].color[2] = mesh.colors[v * 4 + 2];
            } else {
                verts[v].color[0] = 1.0f;
                verts[v].color[1] = 1.0f;
                verts[v].color[2] = 1.0f;
            }
        }

        MeshBuffer buffer = {};
        if (createVertexBuffer(verts.data(), verts.size(), &buffer.buffer, &buffer.memory)) {
            buffer.vertex_count = (uint32_t)verts.size();
            block_meshes_[i] = buffer;
        }
    }
}

void VoxelRenderer::resizePickResources(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0)
        return;
    if (pick_framebuffer_) {
        vkDestroyFramebuffer(device_, pick_framebuffer_, nullptr);
        pick_framebuffer_ = VK_NULL_HANDLE;
    }
    if (pick_image_view_) {
        vkDestroyImageView(device_, pick_image_view_, nullptr);
        pick_image_view_ = VK_NULL_HANDLE;
    }
    if (pick_image_) {
        vkDestroyImage(device_, pick_image_, nullptr);
        pick_image_ = VK_NULL_HANDLE;
    }
    if (pick_image_memory_) {
        vkFreeMemory(device_, pick_image_memory_, nullptr);
        pick_image_memory_ = VK_NULL_HANDLE;
    }
    if (pick_depth_view_) {
        vkDestroyImageView(device_, pick_depth_view_, nullptr);
        pick_depth_view_ = VK_NULL_HANDLE;
    }
    if (pick_depth_image_) {
        vkDestroyImage(device_, pick_depth_image_, nullptr);
        pick_depth_image_ = VK_NULL_HANDLE;
    }
    if (pick_depth_memory_) {
        vkFreeMemory(device_, pick_depth_memory_, nullptr);
        pick_depth_memory_ = VK_NULL_HANDLE;
    }

    pick_extent_.width = width;
    pick_extent_.height = height;

    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R32_UINT;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    vkCreateImage(device_, &image_info, nullptr, &pick_image_);
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(device_, pick_image_, &mem_reqs);
    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = findMemoryType(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device_, &alloc_info, nullptr, &pick_image_memory_);
    vkBindImageMemory(device_, pick_image_, pick_image_memory_, 0);

    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = pick_image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R32_UINT;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    vkCreateImageView(device_, &view_info, nullptr, &pick_image_view_);

    VkImageCreateInfo depth_info = image_info;
    depth_info.format = VK_FORMAT_D32_SFLOAT;
    depth_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    vkCreateImage(device_, &depth_info, nullptr, &pick_depth_image_);
    vkGetImageMemoryRequirements(device_, pick_depth_image_, &mem_reqs);
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = findMemoryType(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device_, &alloc_info, nullptr, &pick_depth_memory_);
    vkBindImageMemory(device_, pick_depth_image_, pick_depth_memory_, 0);

    VkImageViewCreateInfo depth_view_info = {};
    depth_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depth_view_info.image = pick_depth_image_;
    depth_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depth_view_info.format = VK_FORMAT_D32_SFLOAT;
    depth_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depth_view_info.subresourceRange.levelCount = 1;
    depth_view_info.subresourceRange.layerCount = 1;
    vkCreateImageView(device_, &depth_view_info, nullptr, &pick_depth_view_);

    VkImageView attachments[2] = {pick_image_view_, pick_depth_view_};
    VkFramebufferCreateInfo fb_info = {};
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.renderPass = pick_render_pass_;
    fb_info.attachmentCount = 2;
    fb_info.pAttachments = attachments;
    fb_info.width = width;
    fb_info.height = height;
    fb_info.layers = 1;
    vkCreateFramebuffer(device_, &fb_info, nullptr, &pick_framebuffer_);
}

bool VoxelRenderer::pickRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, std::vector<unsigned char>* out_flags) {
    if (!pick_framebuffer_ || width == 0 || height == 0)
        return false;

    if (x >= pick_extent_.width || y >= pick_extent_.height)
        return false;
    uint32_t max_w = pick_extent_.width - x;
    uint32_t max_h = pick_extent_.height - y;
    uint32_t rect_w = (width > max_w) ? max_w : width;
    uint32_t rect_h = (height > max_h) ? max_h : height;

    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    VkDeviceSize buffer_size = rect_w * rect_h * sizeof(uint32_t);
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &buffer_info, nullptr, &staging_buffer) != VK_SUCCESS)
        return false;
    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device_, staging_buffer, &mem_reqs);
    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = findMemoryType(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device_, &alloc_info, nullptr, &staging_memory) != VK_SUCCESS)
        return false;
    vkBindBufferMemory(device_, staging_buffer, staging_memory, 0);

    vkResetFences(device_, 1, &pick_fence_);
    vkResetCommandBuffer(pick_command_buffer_, 0);
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(pick_command_buffer_, &begin_info);

    VkClearValue clear_values[2] = {};
    clear_values[0].color.uint32[0] = 0;
    clear_values[1].depthStencil.depth = 1.0f;

    VkRenderPassBeginInfo rp_begin = {};
    rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass = pick_render_pass_;
    rp_begin.framebuffer = pick_framebuffer_;
    rp_begin.renderArea.extent = pick_extent_;
    rp_begin.clearValueCount = 2;
    rp_begin.pClearValues = clear_values;
    vkCmdBeginRenderPass(pick_command_buffer_, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)pick_extent_.width;
    viewport.height = (float)pick_extent_.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(pick_command_buffer_, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset.x = (int32_t)x;
    scissor.offset.y = (int32_t)y;
    scissor.extent.width = rect_w;
    scissor.extent.height = rect_h;
    vkCmdSetScissor(pick_command_buffer_, 0, 1, &scissor);

    vkCmdBindPipeline(pick_command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pick_pipeline_);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(pick_command_buffer_, 0, 1, &cube_buffer_, &offset);

    struct PickPush {
        Mat4 mvp;
        uint32_t id;
        uint32_t pad[3];
    };

    float aspect = pick_extent_.height > 0 ? (float)pick_extent_.width / (float)pick_extent_.height : 1.0f;
    Mat4 proj = mat4Perspective(ToRadians(60.0f), aspect, 0.1f, 100.0f);
    proj.m[5] *= -1.0f;
    float cp = std::cos(camera_pitch_);
    float sp = std::sin(camera_pitch_);
    float cy = std::cos(camera_yaw_);
    float sy = std::sin(camera_yaw_);
    float fx = cp * cy;
    float fy = sp;
    float fz = cp * sy;
    Mat4 view = mat4LookAt(camera_pos_[0], camera_pos_[1], camera_pos_[2],
                           camera_pos_[0] + fx, camera_pos_[1] + fy, camera_pos_[2] + fz,
                           0.0f, 1.0f, 0.0f);

    Mat4 scale = mat4ScaleInternal(block_scale_, block_scale_, block_scale_);
    for (size_t i = 0; i < blocks_.size(); ++i) {
        Mat4 translate = mat4Translate(blocks_[i].x, blocks_[i].y, blocks_[i].z);
        Mat4 rot_x = mat4RotateX(ToRadians(blocks_[i].rot_x_deg));
        Mat4 rot_y = mat4RotateY(ToRadians(blocks_[i].rot_y_deg));
        Mat4 rot_z = mat4RotateZ(ToRadians(blocks_[i].rot_z_deg));
        Mat4 rotate = mat4Multiply(rot_z, mat4Multiply(rot_y, rot_x));
        Mat4 model = mat4Multiply(translate, mat4Multiply(rotate, scale));
        PickPush pc = {};
        pc.mvp = mat4Multiply(proj, mat4Multiply(view, model));
        pc.id = (uint32_t)(i + 1);
        vkCmdPushConstants(pick_command_buffer_, pick_pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PickPush), &pc);
        vkCmdDraw(pick_command_buffer_, cube_vertex_count_, 1, 0, 0);
    }

    vkCmdEndRenderPass(pick_command_buffer_);

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.image = pick_image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(pick_command_buffer_, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy copy = {};
    copy.bufferOffset = 0;
    copy.bufferRowLength = 0;
    copy.bufferImageHeight = 0;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = {(int32_t)x, (int32_t)y, 0};
    copy.imageExtent = {rect_w, rect_h, 1};
    vkCmdCopyImageToBuffer(pick_command_buffer_, pick_image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging_buffer, 1, &copy);

    VkImageMemoryBarrier barrier_back = barrier;
    barrier_back.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier_back.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier_back.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier_back.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(pick_command_buffer_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier_back);

    vkEndCommandBuffer(pick_command_buffer_);

    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &pick_command_buffer_;
    vkQueueSubmit(queue_, 1, &submit, pick_fence_);
    vkWaitForFences(device_, 1, &pick_fence_, VK_TRUE, UINT64_MAX);

    void* mapped = nullptr;
    vkMapMemory(device_, staging_memory, 0, buffer_size, 0, &mapped);
    uint32_t* ids = static_cast<uint32_t*>(mapped);
    out_flags->assign(blocks_.size(), 0);
    size_t pixel_count = (size_t)rect_w * (size_t)rect_h;
    for (size_t i = 0; i < pixel_count; ++i) {
        uint32_t id = ids[i];
        if (id > 0 && id - 1 < out_flags->size())
            (*out_flags)[id - 1] = 1;
    }
    vkUnmapMemory(device_, staging_memory);
    vkDestroyBuffer(device_, staging_buffer, nullptr);
    vkFreeMemory(device_, staging_memory, nullptr);
    return true;
}

} // namespace voxel
