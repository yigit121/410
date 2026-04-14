#include "App.h"
#include "GltfLoader.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <iomanip>
#include <sstream>

// ── Construction / window setup ───────────────────────────────────────────────

App::App(int width, int height, const char* title)
    : width_(width), height_(height) {

    if (!glfwInit())
        throw std::runtime_error("Failed to init GLFW");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    window_ = glfwCreateWindow(width_, height_, title, nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwMakeContextCurrent(window_);
    glfwSetWindowUserPointer(window_, this);
    glfwSwapInterval(1); // vsync

    // Register callbacks
    glfwSetCursorPosCallback      (window_, cbCursorPos);
    glfwSetScrollCallback         (window_, cbScroll);
    glfwSetKeyCallback            (window_, cbKey);
    glfwSetCharCallback           (window_, cbChar);   // for '+'/'-' regardless of layout
    glfwSetMouseButtonCallback    (window_, cbMouseButton);
    glfwSetFramebufferSizeCallback(window_, cbFramebuffer);

    if (!gladLoadGL(glfwGetProcAddress))
        throw std::runtime_error("Failed to init GLAD");

    std::cout << "OpenGL " << glGetString(GL_VERSION) << "\n"
              << "GPU: "   << glGetString(GL_RENDERER) << "\n";

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

App::~App() {
    glfwDestroyWindow(window_);
    glfwTerminate();
}

// ── Model loading ─────────────────────────────────────────────────────────────

void App::loadModel(const std::string& path) {
    model_ = GltfLoader::load(path);

    renderer_ = std::make_unique<Renderer>();
    renderer_->uploadModel(model_);

    animator_ = std::make_unique<Animator>(&model_);

    // Week 2 validation: run at load time so output appears in terminal
    animator_->validateBindPose();
    animator_->printClipDiagnostic();

    // Compile shaders
    skinnedShader_   = std::make_unique<Shader>("shaders/skinned.vert",    "shaders/skinned.frag");
    boneDebugShader_ = std::make_unique<Shader>("shaders/bone_debug.vert", "shaders/bone_debug.frag");
    gridShader_      = std::make_unique<Shader>("shaders/grid.vert",       "shaders/grid.frag");

    // Bind UBO to shader's named block
    unsigned int blockIdx = glGetUniformBlockIndex(skinnedShader_->id, "BoneMatrices");
    if (blockIdx != GL_INVALID_INDEX)
        glUniformBlockBinding(skinnedShader_->id, blockIdx, 0);
}

// ── Main loop ─────────────────────────────────────────────────────────────────

void App::run() {
    modelPaths_ = {
        "assets/CesiumMan/CesiumMan.gltf",
        "assets/RiggedFigure/RiggedFigure.gltf"
    };
    loadModel(modelPaths_[modelIndex_]);

    double prevTime = glfwGetTime();
    int    frames   = 0;
    double fpsTimer = 0.0;
    int    cachedFps = 0;

    while (!glfwWindowShouldClose(window_)) {
        double now = glfwGetTime();
        float  dt  = (float)(now - prevTime);
        prevTime   = now;

        // FPS counter — recalculated once per second
        frames++;
        fpsTimer += dt;
        if (fpsTimer >= 1.0) {
            cachedFps = frames;
            frames = 0; fpsTimer = 0.0;
        }

        // Title bar — updated every frame so t: reflects scrubbing immediately
        {
            std::ostringstream title;
            title << "Skeletal Viewer | FPS: " << cachedFps;
            if (animator_) {
                title << " | Speed: " << std::fixed << std::setprecision(2)
                      << animator_->speed() << "x";
                title << " | t: " << std::setprecision(3)
                      << animator_->currentTime() << "s";
                if (animator_->clipDuration() > 0.0f)
                    title << "/" << std::setprecision(2) << animator_->clipDuration() << "s";
                title << " | " << (animator_->isPlaying() ? "Playing" : "Paused");
            }
            glfwSetWindowTitle(window_, title.str().c_str());
        }

        processInput(dt);

        if (animator_) animator_->update(dt);
        if (renderer_ && animator_)
            renderer_->uploadSkinningMatrices(animator_->skinningMatrices());

        render();

        glfwSwapBuffers(window_);
        glfwPollEvents();
    }
}

// ── Rendering ─────────────────────────────────────────────────────────────────

void App::render() {
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspect = (float)width_ / (float)height_;
    glm::mat4 view  = camera_.view();
    glm::mat4 proj  = camera_.projection(aspect);
    glm::mat4 model = model_.rootTransform;

    if (renderer_ && skinnedShader_) {
        skinnedShader_->use();
        skinnedShader_->setMat4("uModel",      model);
        skinnedShader_->setMat4("uView",       view);
        skinnedShader_->setMat4("uProjection", proj);
        skinnedShader_->setVec3("uLightDir",   glm::normalize(glm::vec3(1, 2, 1)));
        skinnedShader_->setVec3("uCamPos",     camera_.position());
        renderer_->drawSkinned(*skinnedShader_);
    }

    if (renderer_ && gridShader_) {
        gridShader_->use();
        gridShader_->setMat4("uView",       view);
        gridShader_->setMat4("uProjection", proj);
        renderer_->drawGrid(*gridShader_);
    }

    if (showBones_ && renderer_ && boneDebugShader_ && animator_) {
        boneDebugShader_->use();
        boneDebugShader_->setMat4("uModel",      model);   // same rootTransform as mesh
        boneDebugShader_->setMat4("uView",       view);
        boneDebugShader_->setMat4("uProjection", proj);
        renderer_->drawBones(*boneDebugShader_,
                             animator_->globalTransforms(),
                             model_.skeleton);
    }
}

// ── Keyboard ──────────────────────────────────────────────────────────────────

void App::processInput(float /*dt*/) {
    if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window_, true);
}

// ── GLFW Callbacks ────────────────────────────────────────────────────────────

void App::cbCursorPos(GLFWwindow* w, double x, double y) {
    auto* app = (App*)glfwGetWindowUserPointer(w);
    if (app->firstMouse_) { app->lastX_ = x; app->lastY_ = y; app->firstMouse_ = false; }
    double dx = x - app->lastX_;
    double dy = app->lastY_ - y; // inverted for natural orbit
    app->lastX_ = x; app->lastY_ = y;
    app->camera_.onMouseMove(dx, dy, app->lmbDown_, app->rmbDown_);
}

void App::cbScroll(GLFWwindow* w, double, double dy) {
    auto* app = (App*)glfwGetWindowUserPointer(w);
    app->camera_.onScroll(dy);
}

void App::cbKey(GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
    auto* app = (App*)glfwGetWindowUserPointer(w);
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    if (!app->animator_) return;

    switch (key) {
        case GLFW_KEY_SPACE:
            app->animator_->setPlaying(!app->animator_->isPlaying());
            break;
        case GLFW_KEY_R:
            app->animator_->resetTime();
            break;
        case GLFW_KEY_RIGHT_BRACKET: {
            int next = (app->animator_->clipIndex() + 1) % std::max(1, app->animator_->clipCount());
            app->animator_->setClip(next);
            break;
        }
        case GLFW_KEY_LEFT_BRACKET: {
            int prev = app->animator_->clipIndex() - 1;
            if (prev < 0) prev = app->animator_->clipCount() - 1;
            app->animator_->setClip(std::max(0, prev));
            break;
        }
        case GLFW_KEY_UP:          // arrow up   → faster
        case GLFW_KEY_KP_ADD:      // numpad '+'
            app->animator_->setSpeed(
                std::clamp(app->animator_->speed() * 1.25f, 0.05f, 8.0f));
            break;
        case GLFW_KEY_DOWN:        // arrow down → slower
        case GLFW_KEY_KP_SUBTRACT: // numpad '-'
            app->animator_->setSpeed(
                std::clamp(app->animator_->speed() * 0.8f, 0.05f, 8.0f));
            break;
        // Week 3: frame-by-frame scrubbing — J = back, L = forward (video convention)
        case GLFW_KEY_L:
        case GLFW_KEY_RIGHT:
            app->animator_->stepTime(+1.0f / 30.0f);
            break;
        case GLFW_KEY_J:
        case GLFW_KEY_LEFT:
            app->animator_->stepTime(-1.0f / 30.0f);
            break;
        case GLFW_KEY_B:
            app->showBones_ = !app->showBones_;
            break;
        case GLFW_KEY_M: {
            // Cycle through available models
            app->modelIndex_ = (app->modelIndex_ + 1) % (int)app->modelPaths_.size();
            app->loadModel(app->modelPaths_[app->modelIndex_]);
            break;
        }
        case GLFW_KEY_0:
            // Week 2 diagnostic: freeze to bind pose (t=0) + print validation
            app->animator_->freezeBindPose();
            app->animator_->validateBindPose();
            app->animator_->printClipDiagnostic();
            break;
    }
}

// Character callback — fires with the actual typed Unicode codepoint,
// correctly handles any keyboard layout for '+' and '-'
void App::cbChar(GLFWwindow* w, unsigned int cp) {
    auto* app = (App*)glfwGetWindowUserPointer(w);
    if (!app->animator_) return;
    if (cp == '+') {
        app->animator_->setSpeed(
            std::clamp(app->animator_->speed() * 1.25f, 0.05f, 8.0f));
    } else if (cp == '-') {
        app->animator_->setSpeed(
            std::clamp(app->animator_->speed() * 0.8f, 0.05f, 8.0f));
    }
}

void App::cbMouseButton(GLFWwindow* w, int btn, int action, int) {
    auto* app = (App*)glfwGetWindowUserPointer(w);
    if (btn == GLFW_MOUSE_BUTTON_LEFT)  app->lmbDown_ = (action == GLFW_PRESS);
    if (btn == GLFW_MOUSE_BUTTON_RIGHT) app->rmbDown_ = (action == GLFW_PRESS);
}

void App::cbFramebuffer(GLFWwindow* w, int width, int height) {
    auto* app = (App*)glfwGetWindowUserPointer(w);
    app->width_ = width; app->height_ = height;
    glViewport(0, 0, width, height);
}
