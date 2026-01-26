#include "voxel_engine.h"

namespace voxel {

Engine::Engine() = default;

bool Engine::init(const EngineConfig& config) {
    (void)config;
    return true;
}

void Engine::shutdown() {
}

} // namespace voxel
