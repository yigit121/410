# Final Demo Extensions — Implementation Plan

## Context

Part 1 delivered a working real-time glTF 2.0 skeletal animation viewer: GPU skinning,
TRS-level blending, orbit camera, bone overlay, and a Dear ImGui panel (see `PLAN.md` and
`docs/report.md`). For the **final demo** we extend the renderer with two graphics features
that both connect directly to ray-tracing concepts taught in the course:

1. **Shadow mapping** — a rasterization-based stand-in for shadow rays. A shadow-map depth
   test *is* a visibility query toward the light; PCF filtering approximates soft shadows
   from an area light. This is the rasterizer's answer to "is this point in shadow?"
2. **Physically Based Rendering** — the glTF metallic-roughness Cook-Torrance BRDF plus
   **Image-Based Lighting (IBL)**. IBL evaluates the same environment-lighting integral that
   a path tracer solves by sampling the hemisphere — we precompute it into cubemaps instead.

**Scope (locked): full.** Shadow mapping + PCF, metallic-roughness BRDF with textures and
factors, full IBL (irradiance + prefiltered specular + BRDF LUT), and a visible skybox
background showing the lighting environment.

**Hard constraint: OpenGL 4.1 Core / GLSL 410** (macOS forward-compatible context, see
`src/App.cpp`). **No compute shaders, no SSBOs.** All IBL precomputation is done the classic
way — fragment shaders rendering into framebuffer-attached cubemap faces / 2D textures. This
is the LearnOpenGL approach and works fully on 4.1.

---

## 1. Current State (what we build on)

| Area | Today | What changes |
|---|---|---|
| Lighting | Single hardcoded directional light, Blinn-Phong in `shaders/skinned.frag` | Replaced by Cook-Torrance + IBL ambient |
| Materials | Only `baseColorTexture` → `Mesh.albedoTexture` (`GltfLoader.cpp:250`) | Full metallic-roughness material struct |
| Texture upload | `uploadTexture()` ignores color space (`GltfLoader.cpp:38`) | sRGB vs linear handling per texture role |
| Ground | `GL_LINES` grid only (`Renderer.cpp:103`) | Add filled receiver plane (shadows + PBR) |
| Render loop | Single forward pass (`App::render`) | Multi-pass: shadow depth → main → skybox → grid/UI |
| Shaders | skinned, bone_debug, grid | + depth, pbr, skybox, + IBL precompute shaders |

Key reusable infrastructure that stays:
- Bone UBO at binding point 0 (`Renderer::initUBO`) — the depth pass reuses it for skinning.
- `Shader` helper, `Camera`, `Animator`, the VAO/attrib layout in `Renderer::uploadModel`.
- `Vertex { pos; nrm; uv; bones; weights; }` — extended only if we add tangents.

---

## 2. Target Architecture (Data Flow)

```
                          ┌──────────────── startup (once) ────────────────┐
  env.hdr ──stbi_loadf──> │ equirect → env cubemap                         │
                          │ env cubemap → irradiance cubemap   (diffuse)   │  IBL
                          │ env cubemap → prefiltered cubemap  (specular)  │  textures
                          │ BRDF integration → 2D LUT                      │
                          └────────────────────────────────────────────────┘
                                            │
  per frame:                                ▼
  1. SHADOW PASS   bind depthFBO, lightVP, draw skinned mesh + ground → shadowMap (depth)
  2. MAIN PASS     bind default FBO
                     skinned.vert (skinning, + tangents) → pbr.frag:
                       direct  = CookTorrance(L, V, N, albedo, metal, rough) * shadow(shadowMap)
                       ambient = IBL(irradiance, prefiltered, brdfLUT, F0, rough)
                       color   = direct + ambient + emissive
                     draw ground plane with same pbr.frag (metal=0, rough≈0.8)
  3. SKYBOX        draw env cubemap at far plane (depth = 1.0)
  4. OVERLAY       grid lines + bone debug + ImGui
```

New runtime state lives in two new helper classes plus additions to `Renderer`:
- `ShadowMap` — owns depth FBO + depth texture, builds the light-space matrix from the model AABB.
- `Environment` (IBL) — owns env/irradiance/prefiltered cubemaps + BRDF LUT, runs precompute.

---

## 3. Phased Build Plan

Each phase is **independently demoable** — if time runs short, every completed phase is a
shippable increment. Recommended order: 0 → A → B1 → B2.

### Phase 0 — Shared Groundwork

**0.1 Material model.** In `src/Model.h`, add:
```cpp
struct Material {
    glm::vec4 baseColorFactor{1,1,1,1};
    float     metallicFactor  = 1.0f;
    float     roughnessFactor  = 1.0f;
    glm::vec3 emissiveFactor{0,0,0};
    int baseColorTex        = -1;  // sRGB
    int metallicRoughnessTex= -1;  // linear  (G=roughness, B=metallic)
    int normalTex           = -1;  // linear
    int emissiveTex         = -1;  // sRGB
    int occlusionTex        = -1;  // linear  (R=AO)
};
```
Give `Mesh` a `Material material;` (replacing the bare `albedoTexture`).

**0.2 Loader.** In `src/GltfLoader.cpp`, read the full
`material.pbrMetallicRoughness` block + `normalTexture`/`emissiveTexture`/`occlusionTexture`
and the scalar/vector factors. Add a `bool srgb` parameter to `uploadTexture` and upload
baseColor/emissive as `GL_SRGB8_ALPHA8`, everything else as `GL_RGBA8`.

**0.3 sRGB output.** Enable `glEnable(GL_FRAMEBUFFER_SRGB)` (or do manual gamma in the
shader) so linear-space lighting math is displayed correctly. Decide one and be consistent.

**0.4 Ground plane.** Add a filled quad (pos+normal+UV) to `Renderer` next to the grid —
large, y=0, facing up. It receives shadows and is PBR-shaded. Keep the existing grid as a
thin overlay drawn on top.

**0.5 Model AABB.** Compute the world-space bounding box after load (min/max over vertices,
transformed by `rootTransform`). Needed to fit the shadow frustum and to place the camera/
ground sensibly. Store on `Model` or compute in `Animator`.

**Checkpoint:** model still renders (Blinn-Phong is fine here); ground plane visible; console
prints per-material metallic/roughness/texture indices; AABB printed.

---

### Phase A — Shadow Mapping

> **Demo hook:** "A shadow map is a precomputed depth buffer from the light's point of view.
> Testing a fragment against it is exactly a shadow ray's visibility query — does anything
> block the path to the light? PCF samples neighbors to approximate a soft, area-light shadow."

**A.1 Depth target.** New `ShadowMap` class (or methods on `Renderer`): create an FBO with a
`GL_DEPTH_COMPONENT24` texture (start at 2048×2048), `GL_CLAMP_TO_BORDER` with white border so
outside-the-map samples are lit. No color attachment (`glDrawBuffer(GL_NONE)`).

**A.2 Light matrix.** Directional light → **orthographic** projection sized from the model
AABB; `glm::lookAt(center - lightDir*dist, center, up)`. Recompute per frame (cheap). Expose
`uLightDir` as shared state on `App` so the BRDF and the shadow frustum use the same vector.

**A.3 Depth shader.** `shaders/depth.vert` + empty `shaders/depth.frag`. The vertex shader
**must skin** — copy the weighted-skin block from `shaders/skinned.vert:22-29`, reuse the bone
UBO (binding 0), but output `uLightVP * uModel * skinnedPos` and drop normal/UV outputs.

**A.4 Two-pass render.** In `App::render`:
1. bind depthFBO, `glViewport` to shadow res, `glClear(GL_DEPTH_BUFFER_BIT)`, draw skinned
   mesh + ground with `depth.vert`. (Optionally cull front faces here to reduce acne.)
2. restore viewport/default FBO, draw the scene; bind the shadow map to a texture unit.

**A.5 Receive shadows.** In the main shader: pass `uLightVP`, compute light-space position,
do the depth compare with **slope-scaled bias** (`max(0.0005, 0.005*(1-dot(N,L)))`) and a
**3×3 PCF** kernel. Multiply the direct lighting term by the resulting visibility factor.

**A.6 Self-shadowing** falls out for free — the character is in its own depth pass.

**ImGui:** shadow on/off, bias slider, PCF kernel size, light direction (azimuth/elevation).

**Checkpoint:** character casts a shadow on the ground and self-shadows; no acne or
peter-panning after bias tuning; shadow tracks the light direction.

New files: `src/ShadowMap.{h,cpp}`, `shaders/depth.vert`, `shaders/depth.frag`.
Touches: `Renderer` (ground, draw helpers), `App::render`, `skinned.frag`/new `pbr.frag`.

---

### Phase B1 — Cook-Torrance Direct Lighting

`shaders/pbr.frag` replaces Blinn-Phong for the direct (analytic light) term:
- **NDF:** GGX / Trowbridge-Reitz.
- **Geometry:** Smith with Schlick-GGX, direct-lighting `k = (rough+1)²/8`.
- **Fresnel:** Schlick, `F0 = mix(vec3(0.04), baseColor, metallic)`.
- **Energy split:** `kd = (1 - F) * (1 - metallic)`, `diffuse = kd * albedo / PI`.
- Sample baseColor×factor (sRGB→linear), metallicRoughness (`G`=rough, `B`=metal per glTF),
  multiply by the shadow visibility from Phase A.

**Normal mapping (in full scope):** read the glTF `TANGENT` attribute (vec4, w = handedness)
into the `Vertex` struct, build a TBN matrix in `skinned.vert` (skin the tangent too), and
perturb N by the sampled normal map in `pbr.frag`. If `TANGENT` is missing for a model, fall
back to the geometric normal.

**ImGui:** metallic/roughness override sliders (debug), toggle normal mapping, light intensity/color.

**Checkpoint:** dielectric vs metal looks correct as roughness varies; normal-mapped detail
visible; still shadowed by Phase A.

New files: `shaders/pbr.frag` (and `pbr` may reuse `skinned.vert` extended with tangents).

---

### Phase B2 — Image-Based Lighting + Skybox

All precompute runs once at startup in a new `Environment` class; everything is a fragment
shader rendering into a framebuffer-attached cubemap face or 2D texture (GL 4.1 safe).

**B2.1 Load HDR.** `stbi_loadf("assets/env/<name>.hdr", ...)` (stb already vendored) → float
equirectangular texture (`GL_RGB16F`).
*Asset needed: add one `.hdr` environment to `assets/env/` (e.g. a free Poly Haven map).*

**B2.2 Equirect → cubemap.** Render 6 faces (512²) with a cube + `equirect_to_cube` shader
sampling the equirect map by direction. `GL_RGB16F`, mipmapped.

**B2.3 Irradiance map** (diffuse IBL): convolve env cubemap over the hemisphere into a small
32² cubemap (`irradiance_convolve.frag`).

**B2.4 Prefiltered specular map:** GGX importance sampling into a mip chain (e.g. 128² base,
5 mips), each mip = increasing roughness. Sampled at runtime with `textureLod`.

**B2.5 BRDF LUT:** fullscreen pass integrating the split-sum scale/bias into a 512² `GL_RG16F`
2D texture (`brdf_lut.frag`).

**B2.6 Ambient term** in `pbr.frag`:
```
F      = FresnelSchlickRoughness(NdotV, F0, roughness)
kd     = (1 - F) * (1 - metallic)
diffuse  = irradiance(N) * albedo
spec     = prefiltered(R, roughness*MAX_LOD) * (F * brdf.x + brdf.y)
ambient  = (kd * diffuse + spec) * occlusion
```
Replaces the constant `ambient = 0.15` from the old shader.

**B2.7 Skybox.** Draw the env cubemap as background at `gl_Position.z = w` (depth 1.0,
`glDepthFunc(GL_LEQUAL)`), so the lighting environment is visible behind the model — a big
demo win and makes reflections "make sense" visually.

**B2.8 Seamless filtering:** `glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS)`.

**ImGui:** IBL on/off, environment selector (if >1 HDR), exposure slider, skybox on/off.

**Checkpoint:** metals reflect the environment, dielectrics pick up colored ambient, skybox
visible, lighting matches the visible environment; combined with Phase A shadows.

New files: `src/Environment.{h,cpp}`, shaders `equirect_to_cube.vert/frag`,
`irradiance_convolve.frag`, `prefilter.frag`, `brdf_lut.vert/frag`, `skybox.vert/frag`.

---

## 4. Workload Distribution (2 members)

| Area | Owner | Phase |
|---|---|---|
| Material struct + glTF material parsing + sRGB upload | B | 0 |
| Ground plane + AABB + render-loop refactor | A | 0 |
| ShadowMap class (FBO, light matrix) | A | A |
| Depth shader (skinned) + two-pass wiring | A | A |
| PCF + bias + receive-shadow in main shader | A/B | A |
| Cook-Torrance `pbr.frag` (NDF/G/F) | B | B1 |
| Tangents + normal mapping | B | B1 |
| Environment/IBL precompute class | A | B2 |
| IBL precompute shaders (equirect, irradiance, prefilter, LUT) | A | B2 |
| Ambient IBL term in `pbr.frag` + skybox | B | B2 |
| ImGui controls (shadow/PBR/IBL) | A/B | all |
| Report + demo video update | Both | end |

Rough balance ~50/50: A owns the framebuffer/precompute plumbing, B owns materials and BRDF
shading; both touch `pbr.frag`.

---

## 5. Files

**New source:**
- `src/ShadowMap.{h,cpp}`
- `src/Environment.{h,cpp}`

**New shaders:**
- `shaders/depth.vert`, `shaders/depth.frag`
- `shaders/pbr.frag` (paired with extended `skinned.vert`)
- `shaders/skybox.vert`, `shaders/skybox.frag`
- `shaders/equirect_to_cube.vert`, `shaders/equirect_to_cube.frag`
- `shaders/irradiance_convolve.frag`
- `shaders/prefilter.frag`
- `shaders/brdf_lut.vert`, `shaders/brdf_lut.frag`

**Modified:**
- `src/Model.h` (Material struct, tangents, AABB), `src/GltfLoader.cpp` (material parse, sRGB)
- `src/Renderer.{h,cpp}` (ground plane, material binding, draw helpers)
- `src/App.{h,cpp}` (shared light dir, multi-pass `render()`, env load, ImGui)
- `src/ui/DebugUI.{h,cpp}` (new toggles/sliders)
- `shaders/skinned.vert` (tangents, light-space out)
- `CMakeLists.txt` (new sources; shaders/assets already auto-copied)

**New assets:** at least one `.hdr` environment under `assets/env/`.

---

## 6. Verification Plan

**Per-phase checkpoints:**
- **0:** materials print expected metallic/roughness/texture indices; ground + AABB correct.
- **A:** shadow on ground + self-shadow; no acne/peter-panning; tracks light direction;
  shadow-map debug view (render depth to a small ImGui quad) looks sane.
- **B1:** roughness sweep 0→1 reads correctly; metal vs dielectric distinct; normal map detail.
- **B2:** mirror-rough metal reflects skybox; diffuse picks up ambient color; turning IBL off
  visibly removes ambient; skybox aligns with reflections.

**Test models:** existing `CesiumMan`, `RiggedFigure`. Add a metallic/roughness-textured glTF
(e.g. Khronos `DamagedHelmet` or `FlightHelmet`) to actually exercise the PBR textures —
the two current models are diffuse-ish and won't show off metal/roughness.

**Demo script:** load model → orbit → toggle shadow (show it disappear) → rotate light, watch
shadow swing → toggle IBL (ambient/reflections appear) → roughness slider sweep → show skybox
→ switch to the textured PBR model → play animation with shadows + PBR live.

**Performance target:** keep ≥60 FPS at 1280×720. Shadow pass is one extra geometry pass; IBL
precompute is one-time at load. Measure and record in the report.

---

## 7. Risk Watchlist

- **Shadow acne / peter-panning** — slope-scaled bias + (optional) front-face culling in the
  depth pass + PCF. Budget tuning time; expose bias in ImGui.
- **Light frustum coverage** — ortho bounds too tight clips the shadow; too loose wastes
  resolution. Fit to the AABB and pad ~10%.
- **sRGB double-correction** — pick *either* `GL_FRAMEBUFFER_SRGB` *or* manual gamma, and
  upload color textures as sRGB exactly once. Getting this wrong washes out or darkens everything.
- **glTF metallicRoughness channel order** — roughness in **G**, metallic in **B**. A common bug.
- **No compute on 4.1** — all IBL precompute must be render-to-texture; no SSBO/compute paths.
- **Cubemap face orientation / seams** — get the 6 view matrices right and enable
  `GL_TEXTURE_CUBE_MAP_SEAMLESS`; flipped faces are the usual "reflections look mirrored" cause.
- **Skinning the tangent** — if normal mapping looks wrong under animation, the tangent isn't
  being transformed by the skin matrix like the normal is.
- **Depth pass needs the bone UBO bound** — the skinned depth shader silently collapses to the
  origin if the UBO binding/`glUniformBlockBinding` isn't set up like the main shader
  (`App::loadModel:81`).

---

## 8. Ray-Tracing Connection (for the report / talk)

Both features are rasterization's approximations of effects a ray tracer gets "for free":

- **Shadow map ≈ shadow ray.** The depth test answers the same question a shadow ray does —
  is the light occluded? — but precomputed from the light's view instead of traced per pixel.
  PCF ≈ sampling an area light for soft shadows. Limitations (resolution, bias, single light)
  motivate why ray tracing is attractive.
- **IBL ≈ the environment-lighting integral.** A path tracer estimates incoming radiance by
  sampling the hemisphere against the environment; IBL precomputes that integral into the
  irradiance map (diffuse) and the split-sum prefiltered map + BRDF LUT (specular). Same
  rendering equation, amortized into cubemaps so it runs in real time.
