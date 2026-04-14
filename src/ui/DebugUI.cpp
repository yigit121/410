#include "DebugUI.h"
#include "../Animator.h"

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
                   int        triCount) {
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
