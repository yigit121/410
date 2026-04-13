# Real-Time Skeletal Animation Viewer — Implementation Plan

## Context
This is a 2-person Computer Graphics course project to build a self-contained OpenGL desktop application that loads a glTF 2.0 skinned character, samples animation clips at runtime, performs GPU skinning, and provides interactive controls. The proposal locks the MVP and library choices (GLFW, GLM, tinygltf, stb_image, optional ImGui). This plan turns the proposal into a concrete, week-by-week build guide with file layout, build setup, an exact task split between Member A and Member B, integration checkpoints, and testing strategy. The goal: by end of Week 6 you have a working viewer + report + demo video, with no surprises.

---

## 1. Toolchain & Repo Setup (Day 1)

**Editor/IDE:** Either works — VSCode (with C/C++ + CMake Tools extensions) is recommended for cross-platform parity between teammates. Xcode is fine if both of you use macOS.

**Build system:** CMake (single `CMakeLists.txt`). Avoids Xcode/VSCode lock-in; both members can build identically.

**Language:** C++17.

**Dependencies (vendored under `third_party/`):**
- GLFW — install via Homebrew (`brew install glfw`) or as a git submodule.
- GLM — header-only, submodule or Homebrew.
- tinygltf — single header, drop into `third_party/tinygltf/`.
- stb_image — single header, drop into `third_party/stb/`.
- glad — OpenGL function loader (use the web generator: OpenGL 4.1 core, since macOS caps at 4.1).
- Dear ImGui (optional, Week 6) — drop sources into `third_party/imgui/`.

**Repo layout:**
```
SkeletalViewer/
├── CMakeLists.txt
├── README.md
├── assets/                 # glTF models (CesiumMan, RiggedFigure from Khronos samples)
├── shaders/
│   ├── skinned.vert
│   ├── skinned.frag
│   └── bone_debug.vert/frag
├── src/
│   ├── main.cpp
│   ├── App.{h,cpp}                 # window, main loop, input wiring
│   ├── Camera.{h,cpp}              # orbit camera
│   ├── Shader.{h,cpp}              # GLSL load/compile/link helpers
│   ├── GltfLoader.{h,cpp}          # tinygltf -> internal Model struct
│   ├── Model.h                     # POD: Mesh, Skeleton, Skin, Animation
│   ├── Animator.{h,cpp}            # sampling, interpolation, skinning matrices
│   ├── Renderer.{h,cpp}            # VAO/VBO/UBO setup, draw calls
│   └── ui/DebugUI.{h,cpp}          # ImGui (optional)
├── third_party/
└── docs/
    ├── report.md
    └── demo_script.md
```

**Branching:** `main` is stable. Each member works on `feature/<name>` branches and merges via PR (or just direct push for a 2-person team — but PRs give a review checkpoint).

---

## 2. Architecture (Data Flow)

```
glTF file
  └─> GltfLoader ──> Model { Mesh(vertices+indices+boneIDs+weights),
                              Skeleton(bones[], parentIdx[], invBind[]),
                              Animations[] (channels: T/R/S keyframes per bone) }
                            │
                            ▼
                       Animator(Model, currentClip, t)
                            │  per frame:
                            │   1. sample keyframes -> local TRS
                            │   2. compose local mat4
                            │   3. forward pass -> global mat4
                            │   4. global * invBind -> skinningMatrices[]
                            ▼
                       Renderer
                         - upload skinningMatrices[] to UBO (std140)
                         - bind VAO, shader, textures
                         - glDrawElements
                            ▼
                       skinned.vert: v' = Σ wᵢ * (M_skin[boneᵢ] * v)
```

Key types in `Model.h`:
```cpp
struct Vertex { vec3 pos; vec3 nrm; vec2 uv; ivec4 bones; vec4 weights; };
struct Bone   { int parent; mat4 localBind; mat4 invBind; };
struct Channel{ int boneIdx; std::vector<float> times;
                std::vector<vec3> T; std::vector<quat> R; std::vector<vec3> S; };
struct Animation { std::string name; float duration; std::vector<Channel> channels; };
```

---

## 3. Detailed Week-by-Week Plan

### Week 1 — Setup & OpenGL Baseline

**Member A (Renderer foundation):**
- CMake project, GLFW window (1280×720), glad init, OpenGL 4.1 core context.
- Clear color + depth test enabled.
- `Shader` class: load vert/frag from disk, compile, link, log errors, `setUniform*`.
- `Camera` class: orbit camera (yaw/pitch/radius), view + projection matrices, mouse input.
- Render a hardcoded triangle, then a textured cube — sanity checks the pipeline.

**Member B (Asset loading scaffolding):**
- Drop tinygltf + stb_image into `third_party/`.
- `GltfLoader::load(path)` returning a `Model`.
- Parse: positions, normals, UVs, indices, JOINTS_0, WEIGHTS_0.
- Parse: skin (`inverseBindMatrices`, joint list), build `Skeleton` with parent indices.
- Parse: textures (load albedo via stb_image, upload to GL texture).
- Print bone count, vertex count, animation list to stdout for verification.
- Test on `CesiumMan.gltf` and `RiggedFigure.gltf` from KhronosGroup/glTF-Sample-Models.

**Integration checkpoint (end of Wk 1):** Window opens, camera orbits, model file parses cleanly and prints expected stats. **No rendering of the model yet.**

---

### Week 2 — Static Mesh Rendering (Bind Pose)

**Member A:**
- `Renderer::uploadModel(Model)`: build VAO, VBO (interleaved Vertex), IBO.
- Vertex attribs: pos(0), nrm(1), uv(2), boneIDs(3, ivec4), weights(4, vec4).
- Write `skinned.vert` / `skinned.frag` — but for now, **no skinning**: just pos→clip, simple Lambert lighting using a hardcoded directional light + albedo texture.
- Render the loaded model at bind pose. Verify it looks correct (no animation yet).

**Member B:**
- Parse animation clips: for each animation, walk channels, extract sampler input (times) + output (TRS values), bucket by target bone.
- Store in `std::vector<Animation>` on the Model.
- Print: clip names, durations, channel counts. Validate against expected values for the test models.
- Begin writing unit-test-style validations: load model, dump first 3 keyframes of root bone, sanity-check.

**Checkpoint:** Bind pose renders correctly (recognizable character standing in T-pose / rest pose). Animations are parsed but unused.

---

### Week 3 — Animation Sampling & Hierarchy

**Member A:**
- `Animator` class: holds a `const Model*`, current clip index, `float currentTime`, `bool playing`, `float speed`.
- `Animator::update(dt)`: advance `currentTime`, loop modulo clip duration.
- `sampleChannel(channel, t)` → returns interpolated `vec3` (T/S) or `quat` (R) using `glm::mix` and `glm::slerp`. Binary-search the `times` array for the surrounding keyframes.
- `computeLocalTransforms()` → for each bone, compose TRS into a `mat4`. Bones with no channels in this clip use their `localBind`.
- `computeGlobalTransforms()` → single forward pass: `global[i] = global[parent[i]] * local[i]`. Root has no parent.
- `computeSkinningMatrices()` → `skin[i] = global[i] * invBind[i]`.

**Member B:**
- Debug rendering: add a "skeleton lines" pass — for each bone with parent, draw a line segment from `global[parent].translation` to `global[i].translation`.
- This is **invaluable for debugging** — ship it early even though the proposal lists it as optional.
- Validation: at `t=0`, `skin[i]` should be ≈ identity for every bone (because `global == bind`, so `global * invBind == I`). If not, the bind pose import is wrong.
- Write a small "validation mode" that pauses animation and lets you scrub time with arrow keys.

**Checkpoint:** Skeleton lines animate correctly in 3D space, even though the mesh is still static (bind pose). This isolates animation bugs from skinning bugs.

---

### Week 4 — GPU Skinning

**Member A:**
- Add a UBO: `layout(std140, binding=0) uniform BoneMatrices { mat4 bones[128]; };`
- `Renderer::uploadSkinningMatrices(const std::vector<mat4>&)` → `glBufferSubData` into the UBO.
- Wire `Animator` → `Renderer` in the main loop: each frame, animator.update(dt); renderer.uploadSkinningMatrices(animator.matrices()); renderer.draw().

**Member B:**
- Update `skinned.vert` to do skinning:
  ```glsl
  mat4 skin = weights.x * bones[boneIDs.x]
            + weights.y * bones[boneIDs.y]
            + weights.z * bones[boneIDs.z]
            + weights.w * bones[boneIDs.w];
  vec4 skinnedPos = skin * vec4(aPos, 1.0);
  vec3 skinnedNrm = mat3(transpose(inverse(skin))) * aNrm;
  ```
  Then apply MVP and lighting as before.
- **Incremental validation strategy** (critical — this is the proposal's highest risk):
  1. First, force `skin = mat4(1.0)` in shader → should look identical to bind pose. Confirms attrib upload.
  2. Then `skin = bones[boneIDs.x]` (single bone, ignore weights) → mesh follows bone 0 rigidly. Confirms UBO upload + indexing.
  3. Then full weighted sum. Compare against the skeleton-line overlay from Week 3.

**Checkpoint:** Mesh deforms correctly with the animation. Run on both test models.

---

### Week 5 — Interaction, Polish, MVP Lock

**Member A:**
- Camera controls: LMB-drag = orbit, RMB-drag or shift+drag = pan, scroll = zoom.
- Keyboard: Space = play/pause, `[` / `]` = prev/next clip, `+` / `-` = speed up/down, `R` = reset time.
- Frame timing: use `glfwGetTime()` for `dt`.
- FPS counter (printed to window title at minimum).

**Member B:**
- Edge cases: clip with 0 channels (static), clip shorter than 1 keyframe, normals after non-uniform scale (already handled by inverse-transpose).
- Handle models where `skin.skeleton` is unset, where node hierarchy ≠ joint list order — make sure the bone array is built consistently.
- Write the **first draft** of the report (sections 1–3).

**Checkpoint (END OF WK 5 — MVP LOCK):** Run through the MVP checklist from the proposal. Every item must work on both test models. **Tag the commit `mvp-locked`.** Do not start optional features until this tag exists.

---

### Week 6 — Optional Extensions, Report, Demo

**Pick optional features in priority order; only do what time allows:**

1. **Skeleton overlay toggle** (likely already done in Wk 3 — just expose the toggle).
2. **Animation blending** (Member A): hold two `Animator` time states; blend skinning matrices: `M_blend[i] = (1-α) * M_a[i] + α * M_b[i]` (matrix lerp is incorrect for rotations, but acceptable for short crossfades; for correctness, decompose to TRS and slerp the rotations). Add a slider for α.
3. **ImGui debug panel** (Member B): drop ImGui in, add play/pause button, speed slider, clip dropdown, blend α slider, FPS/triangle/draw-call counters. Replaces keyboard-only UX.
4. **Multi-model loader** (Member B): file picker or hardcoded list cycled by a key.

**Both members:**
- Finalize the report (`docs/report.md`): pipeline diagram, design decisions, risks encountered, performance numbers (FPS at 1080p on your hardware), screenshots.
- Record a 2–3 min demo video: load model, orbit, switch clips, scrub speed, toggle skeleton, (optional) blend.
- README with build instructions for macOS (and Linux if applicable).

---

## 4. Workload Distribution Summary

| Area | Owner | Notes |
|---|---|---|
| Build system, window, GL context | A | Wk 1 |
| glTF parsing | B | Wk 1–2 |
| Camera + input | A | Wk 1, polished Wk 5 |
| Bind-pose render + shaders | A | Wk 2 |
| Animation parsing | B | Wk 2 |
| Animator (sampling, hierarchy, skinning matrices) | A | Wk 3 |
| Skeleton debug overlay | B | Wk 3 |
| UBO upload | A | Wk 4 |
| Skinning vertex shader | B | Wk 4 |
| Playback controls | A | Wk 5 |
| Edge cases & validation | B | Wk 5 |
| Report draft | B | Wk 5 |
| Optional: blending | A | Wk 6 |
| Optional: ImGui panel | B | Wk 6 |
| Final report + video | Both | Wk 6 |

**Rough effort balance:** ~50/50. A owns the GL/animation runtime side; B owns the asset/data + debugging side. Both touch shaders.

---

## 5. Critical Files (to be created)

- `CMakeLists.txt`
- `src/main.cpp`, `src/App.{h,cpp}`
- `src/Camera.{h,cpp}`
- `src/Shader.{h,cpp}`
- `src/GltfLoader.{h,cpp}`, `src/Model.h`
- `src/Animator.{h,cpp}`
- `src/Renderer.{h,cpp}`
- `shaders/skinned.vert`, `shaders/skinned.frag`
- `shaders/bone_debug.vert`, `shaders/bone_debug.frag`
- `docs/report.md`

No existing code to reuse — this is a from-scratch project.

---

## 6. Verification / Test Plan

**Per-week checkpoints:**
- Wk 1: window opens; model parses; bone/vertex counts match expected.
- Wk 2: bind pose renders correctly; recognizable character.
- Wk 3: skeleton debug lines animate in sync with the clip.
- Wk 4: at `t=0`, skinned mesh = bind pose (sanity); at `t>0`, mesh deforms following the skeleton overlay.
- Wk 5: full MVP checklist on both Khronos test models at 60+ FPS.
- Wk 6: optional features verified; report + video done.

**Test models (both must work):**
- `CesiumMan.gltf` (walking animation, ~19 bones — easy)
- `RiggedFigure.gltf` (simpler skeleton — sanity check)
- (Optional) `Fox.gltf` (multiple animation clips — tests clip switching)

**Manual test script for the demo:**
1. Launch app — character appears in T-pose, then begins animating.
2. Orbit camera with mouse — verify smoothness.
3. Press Space — animation pauses.
4. Press `]` — switches to next clip.
5. Press `+` — animation speeds up.
6. Toggle skeleton overlay — bones visible, follow mesh.
7. Frame rate stays ≥ 60 FPS throughout.

**Performance target:** ≥60 FPS at 1280×720 on a typical laptop (Apple M-series or similar). Measure with a 1-second moving average and put it in the report.

---

## 7. Risk Watchlist (operational)

- **Bone-index mismatch between glTF joints array and node hierarchy.** Build skeleton in joints-array order, remap parents accordingly. This is the #1 source of "everything looks like a tornado" bugs.
- **Inverse-bind matrices not applied / applied twice.** The `t=0 → identity` test catches this.
- **UBO std140 padding.** mat4 is 64 bytes, no padding needed, but verify `sizeof(BoneMatrices)` matches GL's expectation.
- **macOS OpenGL cap = 4.1.** Don't use 4.3+ features (no compute, no SSBOs). UBO is fine.
- **Quaternion sign flip across keyframes.** glm::slerp handles it, but if you write your own, check the dot product and negate one quat if negative.
