#pragma once
#include "Camera.h"
#include "Shader.h"
#include "Model.h"
#include "Animator.h"
#include "Renderer.h"
#include "ui/DebugUI.h"
#include <string>
#include <memory>
#include <vector>

struct GLFWwindow;

class App {
public:
    explicit App(int width, int height, const char* title);
    ~App();

    void run();

private:
    GLFWwindow* window_ = nullptr;
    int width_, height_;

    Camera   camera_;
    std::unique_ptr<Shader>   skinnedShader_;
    std::unique_ptr<Shader>   boneDebugShader_;
    std::unique_ptr<Shader>   gridShader_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<Animator> animator_;
    Model model_;

    // ImGui debug panel
    DebugUI ui_;

    // Mouse state
    double lastX_ = 0, lastY_ = 0;
    bool   firstMouse_ = true;
    bool   lmbDown_ = false, rmbDown_ = false;

    // Settings
    bool showBones_ = false;

    // Model list for M-key / UI switching
    std::vector<std::string> modelPaths_;
    int                      modelIndex_ = 0;

    // FPS cache (updated once per second)
    int cachedFps_ = 0;

    void loadModel(const std::string& path);
    void processInput(float dt);
    void render();

    // GLFW callbacks (static forwarders)
    static void cbCursorPos  (GLFWwindow*, double x, double y);
    static void cbScroll     (GLFWwindow*, double, double dy);
    static void cbKey        (GLFWwindow*, int key, int, int action, int);
    static void cbChar       (GLFWwindow*, unsigned int codepoint);
    static void cbMouseButton(GLFWwindow*, int btn, int action, int);
    static void cbFramebuffer(GLFWwindow*, int w, int h);
};
