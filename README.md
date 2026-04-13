# Skeletal Animation Viewer

Real-time glTF 2.0 skeletal animation viewer using OpenGL 4.1 (macOS-compatible).

## Prerequisites

```bash
brew install cmake glfw glm
```

You also need **glad** (OpenGL 4.1 Core loader). Generate it at https://glad.dav1d.de:
- Language: C/C++
- Specification: OpenGL
- Profile: Core
- Version: 4.1
- Extensions: none needed

Download and extract into `third_party/glad/` so you have:
```
third_party/glad/include/glad/glad.h
third_party/glad/include/KHR/khrplatform.h
third_party/glad/src/glad.c
```

Drop `tiny_gltf.h` into `third_party/tinygltf/` and `stb_image.h` + `stb_image_write.h` into `third_party/stb/`.

Download test models from the [Khronos glTF-Sample-Models repo](https://github.com/KhronosGroup/glTF-Sample-Models):
- `2.0/CesiumMan/glTF/CesiumMan.gltf` (and its .bin + textures)

Place them in `assets/`.

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(sysctl -n hw.logicalcpu)
```

## Run

```bash
./build/bin/SkeletalViewer
```

## Controls

| Input | Action |
|---|---|
| LMB drag | Orbit camera |
| RMB drag | Pan camera |
| Scroll | Zoom |
| Space | Play / Pause |
| `[` / `]` | Previous / next animation clip |
| `+` / `-` | Speed up / slow down |
| `R` | Reset animation time |
| `B` | Toggle bone debug overlay |
| Escape | Quit |
