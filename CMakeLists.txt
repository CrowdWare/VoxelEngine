cmake_minimum_required(VERSION 3.16)
project(VoxelEngine LANGUAGES CXX)

add_library(VoxelEngine STATIC
    src/voxel_engine.cpp
    src/voxel_renderer.cpp
    src/stb_image_impl.cpp
)

target_include_directories(VoxelEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(VoxelEngine PUBLIC cxx_std_11)

find_package(Vulkan REQUIRED)
target_link_libraries(VoxelEngine PUBLIC Vulkan::Vulkan)
