# Real-Time Skeletal Animation Viewer — Project Report

**Course:** Computer Graphics  
**Team:** Member A (renderer/animation runtime) · Member B (asset loading/debugging)  
**Date:** April 2026  
**Platform:** macOS (Apple Silicon), OpenGL 4.1 Core

---

## 1. Introduction

This project implements a self-contained desktop application that loads glTF 2.0 skinned character models, samples their animation clips at runtime, performs GPU skinning, and provides interactive camera and playback controls. The viewer supports real-time smooth crossfade blending between animation clips and includes a live Dear ImGui debug panel.

**Deliverables:**
- Fully working viewer (both Khronos reference models)
- Animation blending system (TRS-level SLERP/LERP crossfade)
- ImGui debug panel
- This report

---

## 2. System Architecture

```
glTF file
  └─> GltfLoader ──> Model {
                       Mesh  (vertices: pos, nrm, uv, boneIDs[4], weights[4])
                       Skeleton (bones[], parent[], localBind[], invBind[], bindTRS[])
                       Animations[] (channels: T/R/S keyframes per bone)
                     }
                           │
                           ▼
                      Animator(Model*, currentClip, t)
                           │  per frame:
                           │   1. sample keyframes → local TRS
                           │   2. blend (optional): slerp/lerp two clips' TRS
                           │   3. compose TRS → local mat4
                           │   4. forward pass → global mat4
                           │   5. global × invBind → skinningMatrices[]
                           ▼
                      Renderer
                        ├─ upload skinningMatrices[] → UBO (std140, binding 0)
                        ├─ bind VAO + shader + albedo texture
                        └─ glDrawElements
                           ▼
                      skinned.vert
                        v' = Σ wᵢ × (M_skin[boneᵢ] × v)
                        n' = mat3(transpose(inverse(skin))) × n
```

---

## 3. Pipeline Details

### 3.1 glTF Loading (`GltfLoader.cpp`)

The loader uses **tinygltf** to parse glTF 2.0 JSON and binary buffers. Key steps:

1. **Skeleton** — built in *joint-array order* (the order in `skin.joints[]`), not node-hierarchy order. A node→joint index map is built first to correctly remap parent indices. Each bone stores:
   - `localBind` (mat4) — node's rest-pose local transform
   - `invBind` (mat4) — from `skin.inverseBindMatrices`
   - `bindT/R/S` (vec3/quat/vec3) — bind-pose TRS components, stored separately for blending

2. **Root transform** — glTF models often have non-joint ancestor nodes (e.g. `Z_UP`, `Armature`) that carry orientation/scale. A DFS walk from scene roots accumulates transforms of all non-joint nodes above the skeleton into `model.rootTransform`, applied as `uModel` in the vertex shader.

3. **Mesh** — positions, normals, UVs, `JOINTS_0` (ubyte or ushort), `WEIGHTS_0`, indices (ubyte/ushort/uint). All combined into an interleaved `Vertex` array.

4. **Animations** — each glTF animation channel maps to a (bone, path) pair. Sampler input = time array, output = T/R/S values. Quaternions are reordered from glTF xyzw to GLM wxyz.

### 3.2 Animation Sampling (`Animator.cpp`)

Each frame, `Animator::update(dt)`:

1. Advances `time_` modulo clip duration (guarded for zero-duration static clips).
2. Calls `computeLocal()` — for each bone with a channel, binary-search the time array and interpolate:
   - Translation/Scale: `glm::mix` (linear)
   - Rotation: `glm::slerp` (spherical linear, handles quaternion sign flips)
   - Bones with no channel use their bind-pose TRS.
3. Calls `computeGlobal()` — single forward pass: `global[i] = global[parent[i]] × local[i]`
4. Calls `computeSkinning()` — `skin[i] = global[i] × invBind[i]`

### 3.3 GPU Skinning (`skinned.vert`)

```glsl
mat4 skin = weights.x * bones[boneIDs.x]
          + weights.y * bones[boneIDs.y]
          + weights.z * bones[boneIDs.z]
          + weights.w * bones[boneIDs.w];

vec4 skinnedPos = skin * vec4(aPos, 1.0);
vec3 skinnedNrm = mat3(transpose(inverse(skin))) * aNrm;
```

Skinning matrices are uploaded via a **UBO** (`layout(std140) uniform BoneMatrices { mat4 bones[128]; }`), bound to binding point 0. Using a UBO instead of individual uniforms avoids the per-uniform overhead and stays well within the 64 KB std140 limit (128 × 64 bytes = 8 KB).

> **macOS note:** OpenGL 4.1 does not support `layout(binding=X)` on UBOs. The binding is set via `glUniformBlockBinding` at load time.

### 3.4 Rendering (`Renderer.cpp`)

- **VAO layout:** attrib 0 = pos (vec3), 1 = nrm (vec3), 2 = uv (vec2), 3 = boneIDs (ivec4), 4 = weights (vec4). Interleaved, single VBO.
- **Lighting:** Blinn-Phong with one directional light. Ambient 0.15, diffuse 0.8, specular 0.2 (shininess 32). Albedo from texture or grey fallback.
- **Skeleton overlay:** line segments from each bone's global position to its parent's, drawn with depth test disabled so they are always visible through the mesh. Rendered with a separate shader (`bone_debug.vert/frag`) emitting solid green.
- **Ground grid:** 20×20 lines at Y=0, faded by distance from camera using `smoothstep` in the fragment shader.

---

## 4. Week 6 Extensions

### 4.1 Animation Blending

`Animator::blendTo(int targetClip, float durationSec)` starts a smooth crossfade:

- The **source** clip's time is frozen at the current playhead position.
- The **target** clip's time starts from 0 and advances each frame.
- `blendAlpha_` increases linearly from 0→1 over `durationSec` seconds.
- In `computeLocal()`, both clips are sampled per-bone as raw TRS (not mat4) and then blended:
  - Translation: `glm::mix(tA, tB, α)` — linear
  - Rotation: `glm::slerp(rA, rB, α)` — correctly interpolates on the unit quaternion sphere
  - Scale: `glm::mix(sA, sB, α)` — linear
  - The blended TRS is then composed into a `mat4` for each bone.

This is **correct skeletal blending** — operating at the TRS level ensures rotations interpolate without stretching or shearing. Matrix-level lerp (incorrect shortcut) is deliberately avoided.

When the blend completes (`α ≥ 1`), the animator commits to the target clip and clears the blend state.

**Trigger:** keyboard `N` key or clicking any clip button in the ImGui panel.

### 4.2 Dear ImGui Debug Panel

Built with **Dear ImGui v1.91.8**, using the GLFW + OpenGL3 backends. The panel provides:

| Control | Description |
|---------|-------------|
| FPS / triangle count | Live stats header |
| Model buttons | Instant switch between loaded models |
| Play/Pause + Reset | Playback control |
| Time scrub slider | Drag to any point in the clip |
| Speed slider | 0.05× – 8.0× |
| Clip list (buttons) | Click to smooth-blend to that clip |
| Blend progress bar | Shows % completion of active crossfade |
| Bone overlay checkbox | Toggle skeleton lines |
| Keyboard reference | Collapsible cheat sheet |

ImGui callbacks are chained with the existing GLFW callbacks (`install_callbacks=true`). When ImGui captures keyboard or mouse input, the app's own input handlers are suppressed to avoid double processing.

---

## 5. Design Decisions

### Bone array order: joint-array vs. node-hierarchy
glTF allows the skeleton node hierarchy order to differ from the `skin.joints[]` array order. We build the skeleton strictly in joints-array order and remap parent indices accordingly. This is the most robust approach and matches what the vertex shader expects (bone indices in `JOINTS_0` refer to positions in the joints array).

### Root transform accumulation
Early versions rendered CesiumMan tiny and tilted. The cause: non-joint ancestor nodes (`Z_UP`, `Armature`) carrying a global rotation/scale. The fix: DFS from scene roots, accumulating transforms until the first joint node is encountered. This accumulated matrix becomes `model.rootTransform` and is passed as `uModel` to all shaders.

### UBO over SSBO
macOS caps OpenGL at 4.1, which does not support SSBOs. UBOs (`GL_UNIFORM_BUFFER`) support up to 64 KB — sufficient for 128 × 64-byte mat4s (8 KB). The 128-bone limit covers all Khronos reference models; a real production viewer would use an SSBO on GL 4.3+.

### Speed controls via character callback
macOS GLFW maps keyboard scan codes differently across layouts. Using `glfwSetCharCallback` (which fires with the actual Unicode codepoint) for `+` and `-` ensures speed controls work regardless of keyboard layout, whereas `glfwSetKeyCallback` with `GLFW_KEY_EQUAL` misfired on the test machine.

### TRS storage on Bone struct
Blending requires sampling both clips at TRS level and interpolating before building `mat4`. Rather than decomposing `localBind` at blend time (numerically fragile), the loader reads TRS directly from the glTF node and stores it on the `Bone` struct (`bindT`, `bindR`, `bindS`). For nodes using a matrix form, GLM's `decompose` is used with the conjugate correction.

---

## 6. Challenges & Solutions

| Challenge | Cause | Solution |
|-----------|-------|----------|
| Character tiny / upside-down | Non-joint ancestor nodes not applied | DFS root transform accumulation |
| `+` key paused instead of speeding up | macOS GLFW keycode mapping | `glfwSetCharCallback` for `+`/`-` |
| Skeleton lines invisible | Depth test culled lines inside mesh | `glDisable(GL_DEPTH_TEST)` around `drawBones` |
| `glad.h` not found | glad2 generates `glad/gl.h`, not `glad/glad.h` | Updated all includes |
| `layout(binding=0)` compile error | GL 4.1 doesn't support binding qualifier on UBOs | Removed from shader; use `glUniformBlockBinding` |
| Animation blending mat4 lerp stretching | Linear mat4 interpolation is not rotation-correct | Moved blending to TRS level with SLERP for rotations |

---

## 7. Performance Results

Tested on Apple M2 Pro, macOS 14, 1280×720, vsync on.

| Model | Bones | Triangles | FPS (avg) |
|-------|-------|-----------|-----------|
| CesiumMan | 19 | ~7 600 | 60 (vsync-capped) |
| RiggedFigure | 8 | ~3 200 | 60 (vsync-capped) |

Both models maintain vsync-capped 60 FPS at 1280×720 with bone overlay and ImGui panel active. CPU skinning matrix computation (19 bones) is negligible; the bottleneck is the GPU clear + draw call, which is below 1 ms.

The UBO update (`glBufferSubData`, 19 × 64 = 1 216 bytes) is also negligible. GPU skinning in the vertex shader adds ~6 MAD operations per vertex vs. no-skinning baseline.

---

## 8. Controls Reference

| Input | Action |
|-------|--------|
| LMB drag | Orbit camera |
| RMB drag | Pan camera |
| Scroll wheel | Zoom |
| Space | Play / Pause |
| R | Reset time to 0 |
| `[` / `]` | Previous / Next clip (instant) |
| N | Blend to next clip (0.4 s crossfade) |
| Up / Down arrow | Speed ×1.25 / ×0.8 |
| `+` / `-` (typed) | Speed ×1.25 / ×0.8 |
| J / Left arrow | Step back 1/30 s |
| L / Right arrow | Step forward 1/30 s |
| B | Toggle bone skeleton overlay |
| M | Cycle loaded model |
| 0 | Freeze bind pose + print diagnostics |
| Esc | Quit |

---

## 9. Build Instructions

```bash
# Prerequisites (macOS, Homebrew)
brew install cmake glfw glm

# Clone and build
git clone https://github.com/yigit121/410.git
cd 410
mkdir build && cd build
cmake ..
cmake --build . -j$(sysctl -n hw.logicalcpu)
./bin/SkeletalViewer
```

---

## 10. Conclusion

The viewer successfully demonstrates the full skeletal animation pipeline from glTF asset loading through CPU animation sampling to GPU skinning in the vertex shader. The Week 6 extension adds correct TRS-level animation blending and a fully functional Dear ImGui debug panel.

Key implementation lessons:
- The joint-array order vs. node-hierarchy order distinction is the single most common source of "tornado mesh" bugs in glTF viewers.
- Root transform accumulation from non-joint ancestor nodes is necessary for real-world models.
- Blending must happen at TRS level — matrix-level lerp produces shearing artefacts.
- macOS GL 4.1 has several restrictions (no SSBO, no UBO `layout(binding=)`, no `gl_FragDepth` in 4.1 core without extension) that require deliberate workarounds.
