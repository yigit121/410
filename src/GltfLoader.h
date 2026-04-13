#pragma once
#include "Model.h"
#include <string>

namespace GltfLoader {
    // Loads a glTF 2.0 file (.gltf or .glb) into our internal Model struct.
    // Throws std::runtime_error on failure.
    Model load(const std::string& path);
}
