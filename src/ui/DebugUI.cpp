#include "DebugUI.h"
#include "../Animator.h"
#include "../AnimStateMachine.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <string>
#include <vector>

void DebugUI::init(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // don't write imgui.ini

    ImGui::StyleColorsDark();

    // Soften the default dark theme slightly
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 6.0f;
    style.FrameRounding     = 4.0f;
    style.GrabRounding      = 4.0f;
    style.WindowPadding     = ImVec2(10, 10);
    style.FramePadding      = ImVec2(6, 3);
    style.ItemSpacing       = ImVec2(8, 5);
    style.Alpha             = 0.92f;

    // install_callbacks = true so ImGui chains our existing GLFW callbacks
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 410 core");
}

void DebugUI::shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void DebugUI::beginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void DebugUI::endFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool DebugUI::wantsKeyboard() const { return ImGui::GetIO().WantCaptureKeyboard; }
bool DebugUI::wantsMouse()    const { return ImGui::GetIO().WantCaptureMouse; }

// ── Main panel ────────────────────────────────────────────────────────────────

bool DebugUI::draw(Animator& animator,
                   bool&      showBones,
                   int&       modelIndex,
                   const std::vector<std::string>& modelPaths,
                   int        fps,
                   int        triCount,
                   RenderSettings& settings,
                   AnimStateMachine& stateMachine) {
    bool changed = false;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(290, 0), ImGuiCond_Always); // auto height
    ImGui::Begin("Skeletal Viewer", nullptr,
                 ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse);

    // ── Stats ────────────────────────────────────────────────────────────────
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "FPS: %d", fps);
    ImGui::SameLine();
    ImGui::TextDisabled("|  Triangles: %d", triCount);

    ImGui::Separator();

    // ── Model selector ───────────────────────────────────────────────────────
    ImGui::Text("Model");
    for (int i = 0; i < (int)modelPaths.size(); i++) {
        // Extract short name (last path component without extension)
        std::string full = modelPaths[i];
        size_t sl = full.rfind('/');
        std::string label = (sl == std::string::npos) ? full : full.substr(sl + 1);
        size_t dot = label.rfind('.');
        if (dot != std::string::npos) label = label.substr(0, dot);

        bool selected = (i == modelIndex);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));
        if (ImGui::Button(label.c_str(), ImVec2(130, 0))) {
            if (i != modelIndex) { modelIndex = i; changed = true; }
        }
        if (selected) ImGui::PopStyleColor();
        if (i + 1 < (int)modelPaths.size()) ImGui::SameLine();
    }

    ImGui::Separator();

    // ── Playback controls ────────────────────────────────────────────────────
    ImGui::Text("Playback");

    // Play / Pause
    const char* playLabel = animator.isPlaying() ? "  Pause  " : "  Play  ";
    if (ImGui::Button(playLabel)) {
        animator.setPlaying(!animator.isPlaying());
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        animator.resetTime();
    }

    // Time scrub bar
    float t   = animator.currentTime();
    float dur = animator.clipDuration();
    if (dur > 0.0f) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##time", &t, 0.0f, dur, "t = %.3f s")) {
            float delta = t - animator.currentTime();
            animator.stepTime(delta);
        }
    }

    // Speed slider
    float spd = animator.speed();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("Speed##spd", &spd, 0.05f, 8.0f, "%.2fx")) {
        animator.setSpeed(spd);
    }

    ImGui::Separator();

    // ── Clip selector ────────────────────────────────────────────────────────
    int clipCount = animator.clipCount();
    if (clipCount > 0) {
        ImGui::Text("Clips  (%d total)", clipCount);

        int  curClip  = animator.clipIndex();
        bool blending = animator.isBlending();

        for (int i = 0; i < clipCount; i++) {
            std::string cname = animator.clipName(i);
            if (cname.empty()) cname = "clip_" + std::to_string(i);

            bool active = (i == curClip) ||
                          (blending && i == animator.blendTarget());

            ImVec4 col = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
            if (i == curClip && !blending) col = ImVec4(0.2f, 0.6f, 0.2f, 1.0f);
            if (blending && i == curClip)  col = ImVec4(0.6f, 0.6f, 0.1f, 1.0f);
            if (blending && i == animator.blendTarget()) col = ImVec4(0.1f, 0.5f, 0.8f, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Button, col);
            std::string btnLabel = std::to_string(i) + " " + cname;
            if (ImGui::Button(btnLabel.c_str(), ImVec2(-1, 0))) {
                if (!blending && i != curClip) {
                    animator.blendTo(i, 0.4f); // 400 ms crossfade
                }
            }
            ImGui::PopStyleColor();
        }

        // Blend progress bar
        if (blending) {
            float alpha = animator.blendAlpha();
            char overlay[32];
            snprintf(overlay, sizeof(overlay), "Blending %.0f%%", alpha * 100.0f);
            ImGui::SetNextItemWidth(-1);
            ImGui::ProgressBar(alpha, ImVec2(-1, 8), overlay);
        }
    }

    ImGui::Separator();

    // ── State machine (Walk / Run / Jump) ─────────────────────────────────────
    ImGui::Text("State Machine");
    {
        using S = AnimStateMachine::State;
        S cur = stateMachine.state();
        struct { S s; const char* label; } btns[] = {
            { S::Walk, "Walk [1]" }, { S::Run, "Run [2]" }
        };
        for (int i = 0; i < 2; i++) {
            bool active = (cur == btns[i].s);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
            if (ImGui::Button(btns[i].label, ImVec2(126, 0)))
                stateMachine.request(btns[i].s, animator);
            if (active) ImGui::PopStyleColor();
            if (i < 1) ImGui::SameLine();
        }
        ImGui::TextDisabled("State: %s", stateMachine.stateName());
        if (!stateMachine.multiClip())
            ImGui::TextDisabled("(speed-scaled - single-clip model)");
    }

    ImGui::Separator();

    // ── Inverse Kinematics ────────────────────────────────────────────────────
    ImGui::Text("Inverse Kinematics");
    {
        bool ikOn = animator.isIKEnabled();
        if (ImGui::Checkbox("Enable IK  [I]", &ikOn)) {
            if (ikOn && animator.ikValid())
                animator.setIKTarget(animator.effectorWorldPos());
            animator.setIKEnabled(ikOn);
        }

        // End-effector dropdown (bone names)
        int   end     = animator.ikEndEffector();
        int   nBones  = animator.boneCount();
        std::string preview = (end >= 0) ? animator.boneName(end) : "<pick a bone>";
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##ikbone", preview.c_str())) {
            for (int i = 0; i < nBones; i++) {
                bool sel = (i == end);
                if (ImGui::Selectable(animator.boneName(i).c_str(), sel)) {
                    animator.setIKEndEffector(i);
                    if (animator.isIKEnabled() && animator.ikValid())
                        animator.setIKTarget(animator.effectorWorldPos());
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (animator.ikValid()) {
            glm::vec3 tgt = animator.ikTarget();
            // Slider range from model bounds (generous, model-local space).
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat3("##iktarget", &tgt.x, 0.01f))
                animator.setIKTarget(tgt);
            if (ImGui::Button("Reset to limb", ImVec2(-1, 0)))
                animator.setIKTarget(animator.effectorWorldPos());
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "chain OK (root->mid->end)");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                               "pick a deeper bone (needs 2 parents)");
        }
    }

    ImGui::Separator();

    // ── Lighting & shadows ───────────────────────────────────────────────────
    ImGui::Text("Lighting & Shadows");
    ImGui::Checkbox("Shadows", &settings.shadowEnabled);
    ImGui::SameLine();
    ImGui::Checkbox("Ground", &settings.showGround);
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("Intensity##li", &settings.lightIntensity, 0.5f, 10.0f, "%.1f");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("Bias##shadowbias", &settings.shadowBias, 0.0002f, 0.01f, "%.4f");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("Azimuth##lightaz", &settings.lightAzimuth, 0.0f, 360.0f, "%.0f deg");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("Elevation##lightel", &settings.lightElevation, 10.0f, 89.0f, "%.0f deg");

    ImGui::Separator();

    // ── PBR Overrides ────────────────────────────────────────────────────────
    ImGui::Text("PBR / IBL");
    ImGui::Checkbox("IBL Ambient", &settings.iblEnabled);
    ImGui::SameLine();
    ImGui::Checkbox("Skybox", &settings.skyboxEnabled);

    {
        static bool overridePBR = false;
        static float sliderMetal = 0.0f, sliderRough = 0.5f;

        if (ImGui::Checkbox("Override material", &overridePBR)) {
            if (!overridePBR) {
                settings.metallicOverride  = -1.0f;
                settings.roughnessOverride = -1.0f;
            }
        }
        if (overridePBR) {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("Metallic##m",  &sliderMetal, 0.0f, 1.0f, "%.2f"))
                settings.metallicOverride = sliderMetal;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("Roughness##r", &sliderRough, 0.05f, 1.0f, "%.2f"))
                settings.roughnessOverride = sliderRough;
            // Keep settings in sync even when the sliders haven't moved
            settings.metallicOverride  = sliderMetal;
            settings.roughnessOverride = sliderRough;
        }
    }

    ImGui::Separator();

    // ── Debug toggles ────────────────────────────────────────────────────────
    ImGui::Text("Debug");
    ImGui::Checkbox("Bone overlay  [B]", &showBones);

    ImGui::Separator();

    // ── Keyboard reference ───────────────────────────────────────────────────
    if (ImGui::TreeNode("Keyboard shortcuts")) {
        ImGui::TextDisabled("Space      Play / Pause");
        ImGui::TextDisabled("R          Reset time");
        ImGui::TextDisabled("[ / ]      Prev / Next clip (instant)");
        ImGui::TextDisabled("N          Blend to next clip (smooth)");
        ImGui::TextDisabled("1 / 2      Walk / Run");
        ImGui::TextDisabled("I          Toggle inverse kinematics");
        ImGui::TextDisabled("Up / Down  Speed x1.25 / x0.8");
        ImGui::TextDisabled("+ / -      Speed (typed)");
        ImGui::TextDisabled("J / L      Step -1/30s / +1/30s");
        ImGui::TextDisabled("B          Toggle bone overlay");
        ImGui::TextDisabled("M          Switch model");
        ImGui::TextDisabled("0          Bind pose + diagnostics");
        ImGui::TextDisabled("LMB drag   Orbit camera");
        ImGui::TextDisabled("RMB drag   Pan camera");
        ImGui::TextDisabled("Scroll     Zoom");
        ImGui::TextDisabled("Esc        Quit");
        ImGui::TreePop();
    }

    ImGui::End();
    return changed;
}
