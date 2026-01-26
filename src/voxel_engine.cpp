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
