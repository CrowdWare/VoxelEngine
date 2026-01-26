# VoxelEngine

Kern-Library fuer Voxel-Rendering in Vulkan. Wird aktuell vom RaidBuilder genutzt und spaeter auch vom Game.

## Features (aktueller Stand)
- Vulkan Renderer fuer einfache Voxel-Bloecke
- Texturen fuer Ground/Cubes (stb_image)
- Picking per Offscreen-Render (Pick-Buffer)
- Kamera-Setup (Position, Yaw, Pitch)

## Build
Voraussetzungen:
- Vulkan SDK

```sh
cmake -S . -B build
cmake --build build
```

## API Einstieg
- Header: `include/voxel_renderer.h`
- Kernklasse: `voxel::VoxelRenderer`
  - `init(...)`, `render(...)`, `setBlocks(...)`, `pickRect(...)`

## Status
Rendering-Backend ist aktiv, Features werden iterativ ausgebaut (Performance, LOD, Materialien).
