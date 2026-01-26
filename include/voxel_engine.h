#ifndef VOXEL_ENGINE_H
#define VOXEL_ENGINE_H

namespace voxel {

struct EngineConfig {
    int width;
    int height;
};

class Engine {
public:
    Engine();
    bool init(const EngineConfig& config);
    void shutdown();
};

} // namespace voxel

#endif
