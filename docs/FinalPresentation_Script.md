# Final Presentation — Speaker Script

**Real-Time Skeletal Animation Viewer — Final Demo**  
Mehmet Yiğit Tutaş · Ozan Özak

Estimated runtime: **~9 min 25 s** (speaking only; add ~2-3 min for the live demo).

> Speaker split below is a suggestion — Yiğit takes the rendering extensions, Ozan takes shadows + animation. Adjust freely.

---

## 1 · Title
*~25s · Yiğit*

Hi everyone — we're Yiğit and Ozan, and this is the final demo of our Real-Time Skeletal Animation Viewer, written in C++17 on OpenGL 4.1 Core, loading glTF 2.0 characters.

In the mid-demo we showed the core pipeline working. Today we're focusing on the four extensions we listed back then as future work — and all four are now implemented.


## 2 · What's New
*~45s · Yiğit*

Quick recap on the left: the base viewer loads glTF geometry, skeleton, and animation; it does forward kinematics down the bone hierarchy, then linear blend skinning on the GPU with up to four bone influences per vertex. That foundation hasn't changed.

On the right are the four new extensions, and they all build directly on that FK-and-skinning core: shadow mapping, physically based rendering with image-based lighting, inverse kinematics, and a keyboard-driven animation state machine. We'll take them in that order.


## 3 · Shadow Mapping — Two-Pass
*~40s · Ozan*

Think about what a shadow really is: a spot is in shadow when something stands between it and the light. So the whole question is simply — can this spot see the light, or not? Ray tracing answers it the obvious way: from the spot, you send a ray straight to the light and check if anything is in the way. Path tracing does this with lots of rays bouncing around the scene — it looks amazing, but it's far too slow to run in real time.

Shadow mapping gets the same answer with a clever shortcut. First, we take a picture from the light's position — but instead of colours, it records how far away the nearest thing is in each direction. Then, while we draw the scene, every spot just asks: am I farther away than the nearest thing the light saw? If I am, something is blocking me, so I'm in shadow. It's the same 'can I see the light' test — just done ahead of time, instead of one ray at a time.


## 4 · Shadow Mapping — Soft Edges
*~20s · Ozan*

One catch: the light's picture is a bit low-resolution, so the shadow edges come out blocky and jagged. To smooth them, instead of each spot checking a single point, it checks a few neighbouring points and averages them — so right at the edge you get a soft, gradual fade instead of a hard line. It's a cheap way to fake the soft shadows you'd naturally get from a real-world light.


## 5 · PBR
*~50s · Yiğit*

PBR just means we colour surfaces the way light actually behaves in the real world, instead of hand-tweaking things until they look okay. The big win is that a material then looks right under any lighting. We describe every surface with three simple, intuitive values: its base colour, how metallic it is — basically, is it metal or not — and how rough or polished it is. Smooth gives sharp mirror reflections, rough gives a soft matte look. In the demo you can drag the metallic and roughness sliders and watch the same model go from matte plastic to a shiny chrome mirror.

For each pixel we add up two things: the soft, even base colour, and the shiny highlights and reflections on top. Roughness controls how sharp those highlights are, and there's a real-world effect built in where every surface gets more mirror-like when you look at it edge-on — like a tabletop that's dull from above but reflective when you look across it. The takeaway is simple: it follows real physics, so it just looks correct.


## 6 · IBL
*~50s · Yiğit*

In the real world, a surface isn't lit by a single light — it also receives light bouncing off everything around it: the sky, the ground, nearby objects. Image-based lighting reproduces that. We light the character using its whole environment, not just one lamp. That's why the metal reflects the sky, and the shaded side still picks up some colour instead of looking flat.

The problem is that adding up light from every direction, for every pixel, would be way too heavy to do live. So we do that expensive part once, up front, and save the results into a few small images we can quickly look up while rendering. We build our own sky in code — no photo needed — and from it we make a soft, blurry version for the gentle fill light, plus a set of sharper-to-blurrier versions for reflections, so smooth surfaces look like a mirror and rough ones look hazy.

Big picture: it's the same idea as our shadows — a fast stand-in for the rich, all-around lighting that path tracing would compute properly, but far too slowly for real time.

**Demo cue:** Demo highlight: set Metallic to 1 and Roughness near 0 — the character becomes a chrome mirror of the sky.


## 7 · Inverse Kinematics — Concept
*~50s · Ozan*

Now inverse kinematics. Our whole animation pipeline so far is forward kinematics: you have joint angles, and you compute where the end of the limb lands. Inverse kinematics flips that around — you specify a target position, and the solver computes the joint angles needed to reach it.

We implemented a two-bone analytic solver over a chain of three joints: root, mid, and end — think hip, knee, foot, or shoulder, elbow, hand. Because the bone lengths are fixed, the law of cosines gives us the exact interior angles for the root and the mid joint, and then an aim rotation points the whole chain at the target. It's closed-form, so there's no iteration — it solves every frame. If the target is out of reach, the limb just straightens.


## 8 · Inverse Kinematics — Integration
*~45s · Ozan*

Integrating it was clean. After we sample the clips and run the forward kinematics pass, we insert the IK solve, then re-run forward kinematics so the corrected joints propagate to the rest of the limb — and only then do skinning. The solver edits the local rotations of just the root and mid joints.

In the demo you pick any bone as the end effector from a dropdown; the two parents above it become the chain. A yellow marker shows the target, and dragging it bends the knee or elbow to follow. The key point: IK runs on top of the playing animation, so the body keeps walking while the chosen limb tracks the target.

**Demo cue:** Live: pick a foot or hand bone, press I, then drag the target X/Y/Z.


## 9 · Animation State Machine
*~55s · Yiğit*

The last extension is a keyboard-driven animation state machine. Pressing 1 or 2 requests Walk or Run, and each request maps to a real animation clip and starts a 0.3-second crossfade. Importantly, that crossfade reuses the TRS-level SLERP blending we already built and validated in the mid-demo — so transitions are smooth, no popping or shearing.

To make this meaningful we added the Khronos Fox model: 24 bones and three genuinely different clips — survey, walk, and run — so we can show real clip-to-clip blending, not just a speed change. For the single-clip human models, the same keys fall back to speed-scaling.

One honest design note: we originally planned Walk-Run-Jump. But none of these rigs ship with a jump clip, and faking it by translating the whole model upward looked artificial — so we kept locomotion to Walk and Run, which we can demonstrate properly.

**Demo cue:** Live: press M to switch to the Fox, then toggle 2 and 1 to crossfade Run and Walk.


## 10 · State Machine — Architecture
*~40s · Yiğit*

Architecturally the state machine is deliberately thin. It's a small new class that just holds the current state and maps each state to a clip index by matching the clip name. When you press a key, it calls the existing Animator blendTo function — it doesn't touch the core pipeline at all.

So the data flow is: key press, to the state machine, which picks the clip, which calls blendTo, which runs the SLERP crossfade. All the heavy lifting is the blend system we already had; the state machine is pure orchestration on top of it.


## 11 · Implementation Challenges
*~60s · Ozan*

A few real problems came up, mostly from adding the Fox. First, the Fox mesh was invisible — it ships as non-indexed geometry, so we synthesise a sequential index buffer. Second, it has no normals, so we generate smooth per-vertex normals from the faces.

The trickiest one: the Fox collapsed into a ball while walking. Its clips animate rotation only on most joints, and our code was defaulting the missing translation to zero — yanking every joint to the origin. The fix is to fall back to the bone's bind pose for any missing track. Our human models exported full translation-rotation-scale per joint, which is why they never hit this.

And two from the rendering side: all the IBL baking had to work around no compute shaders on GL 4.1, and the IK solver computes a world-space correction that we convert into each bone's parent frame before writing the local rotation.


## 12 · Live Demo Guide
*~50s · Both*

This slide is our demo checklist. We'll start with shadows — toggle the ground and swing the light. Then PBR — push metallic and roughness to get the chrome look. Then toggle IBL ambient and the skybox to show the environment lighting. Then switch to the Fox and crossfade run and walk. And finally enable IK and drag a limb around.

The new key bindings are on the right: 1 and 2 for the locomotion states, I to toggle IK, M to switch models. The whole thing still runs at a steady 60 FPS with shadows, IBL, and skinning all on, across three models.

**Demo cue:** Switch to the actual app window here and run through the five steps.


## 13 · Conclusion
*~35s · Both*

To wrap up: every one of the four extensions we promised at the mid-demo is delivered — shadow mapping, PBR with image-based lighting, interactive inverse kinematics, and the Walk-Run state machine — plus we added a new multi-clip reference model.

The big takeaway is that these extensions slotted in additively on top of the FK-and-skinning core, and several of them mirror offline techniques — a shadow map is a rasterised shadow ray, and split-sum IBL approximates path-traced ambient. Future work would be multi-bone IK chains and parametric blend trees. Thank you — happy to take questions.

