#pragma once
#include <string>
#include <vector>

struct GLFWwindow;
class Animator;

// ImGui debug panel — encapsulates all Dear ImGui lifecycle calls so App stays clean.
class DebugUI {
public:
    void init(GLFWwindow* window);
    void shutdown();

    // Call at the start of each frame (before any ImGui::* calls)
    void beginFrame();
    // Call after all ImGui::* calls — renders the draw list
    void endFrame();

    // Draw the main control panel.
    // Returns true if any value was changed that the caller should act on.
    bool draw(Animator& animator,
              bool&       showBones,
              int&        modelIndex,
              const std::vector<std::string>& modelPaths,
              int         fps,
              int         triCount);

    // Returns true when ImGui wants to capture keyboard/mouse input
    // (caller should suppress its own input processing)
    bool wantsKeyboard() const;
    bool wantsMouse()    const;
};
