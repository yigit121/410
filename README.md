# Skeletal Viewer

Real-time glTF 2.0 skeletal animation viewer built with OpenGL 4.1 (macOS-compatible).

Loads skinned character models, samples animation clips, performs GPU skinning via a vertex shader, supports smooth crossfade blending between clips, and provides a Dear ImGui debug panel.

## Features

- glTF 2.0 loading (tinygltf) — meshes, skeletons, animations, textures
- GPU skinning in vertex shader (UBO, up to 128 bones)
- TRS-level animation blending with SLERP (smooth crossfade)
- Orbit / pan / zoom camera
- Bone skeleton debug overlay
- Dear ImGui debug panel — playback controls, clip selector, scrub bar, blend progress
- Ground grid with distance fade
- Tested on Khronos reference models: CesiumMan, RiggedFigure

## Prerequisites

macOS with Xcode Command Line Tools, then:

```bash
brew install cmake glfw glm
```

All other dependencies (glad, tinygltf, stb, Dear ImGui) are already vendored under `third_party/`.

## Building

```bash
git clone https://github.com/yigit121/410.git
cd 410
mkdir build && cd build
cmake ..
cmake --build . -j$(sysctl -n hw.logicalcpu)
./bin/SkeletalViewer
```

> **Xcode:** `cmake -G Xcode ..` then open the generated `.xcodeproj` and set the run scheme to **SkeletalViewer**.

## Controls

| Input | Action |
|-------|--------|
| LMB drag | Orbit camera |
| RMB drag | Pan camera |
| Scroll | Zoom |
| Space | Play / Pause |
| R | Reset animation time |
| `[` / `]` | Previous / next clip (instant switch) |
| N | Blend to next clip (smooth 0.4 s crossfade) |
| Up / Down arrow | Speed ×1.25 / ×0.8 |
| `+` / `-` (typed) | Speed ×1.25 / ×0.8 |
| J / Left arrow | Step back 1/30 s |
| L / Right arrow | Step forward 1/30 s |
| B | Toggle bone overlay |
| M | Cycle model (CesiumMan ↔ RiggedFigure) |
| 0 | Freeze to bind pose + print diagnostics |
| Esc | Quit |

## Project Structure

```
SkeletalViewer/
├── assets/             # glTF models (CesiumMan, RiggedFigure)
├── docs/
│   └── report.md       # Full project report
├── shaders/
│   ├── skinned.vert/frag      # GPU skinning + Blinn-Phong lighting
│   ├── bone_debug.vert/frag   # Skeleton line overlay
│   └── grid.vert/frag         # Ground grid
├── src/
│   ├── App.{h,cpp}            # Window, main loop, input handling
│   ├── Animator.{h,cpp}       # Keyframe sampling, blending, skinning matrices
│   ├── Camera.{h,cpp}         # Orbit camera
│   ├── GltfLoader.{h,cpp}     # tinygltf → internal Model struct
│   ├── Model.h                # POD data types (Vertex, Bone, Channel, Animation, Mesh, Model)
│   ├── Renderer.{h,cpp}       # VAO/VBO/UBO management, draw calls
│   ├── Shader.{h,cpp}         # GLSL load/compile/link helper
│   └── ui/
│       └── DebugUI.{h,cpp}    # Dear ImGui debug panel
└── third_party/
    ├── glad/           # OpenGL 4.1 Core loader (vendored)
    ├── imgui/          # Dear ImGui v1.91.8 (vendored)
    ├── stb/            # stb_image (vendored)
    └── tinygltf/       # tinygltf + nlohmann/json (vendored)
```

## Third-Party Libraries

| Library | Version | License |
|---------|---------|---------|
| [GLFW](https://www.glfw.org) | 3.3+ | zlib |
| [GLM](https://github.com/g-truc/glm) | 0.9.9+ | MIT |
| [glad](https://glad.dav1d.de) | GL 4.1 Core | MIT |
| [tinygltf](https://github.com/syoyo/tinygltf) | 2.x | MIT |
| [stb_image](https://github.com/nothings/stb) | — | Public Domain |
| [Dear ImGui](https://github.com/ocornut/imgui) | 1.91.8 | MIT |
