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
#include <cmath>

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

    // Register GLFW callbacks (ImGui will chain these when ui_.init() is called)
    glfwSetCursorPosCallback      (window_, cbCursorPos);
    glfwSetScrollCallback         (window_, cbScroll);
    glfwSetKeyCallback            (window_, cbKey);
    glfwSetCharCallback           (window_, cbChar);
    glfwSetMouseButtonCallback    (window_, cbMouseButton);
    glfwSetFramebufferSizeCallback(window_, cbFramebuffer);

    if (!gladLoadGL(glfwGetProcAddress))
        throw std::runtime_error("Failed to init GLAD");

    std::cout << "OpenGL " << glGetString(GL_VERSION) << "\n"
              << "GPU: "   << glGetString(GL_RENDERER) << "\n";

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    // Shadow map depth target (2048x2048)
    shadowMap_.init(2048);

    // Init Dear ImGui (must happen after glad + before any GL draw calls)
    ui_.init(window_);
}

App::~App() {
    ui_.shutdown();
    glfwDestroyWindow(window_);
    glfwTerminate();
}

// ── Model loading ─────────────────────────────────────────────────────────────

void App::loadModel(const std::string& path) {
    model_ = GltfLoader::load(path);

    renderer_ = std::make_unique<Renderer>();
    renderer_->uploadModel(model_);

    animator_ = std::make_unique<Animator>(&model_);

    animator_->validateBindPose();
    animator_->printClipDiagnostic();

    frameCameraToModel();

    // Configure the locomotion state machine for the freshly-loaded model.
    stateMachine_.configureForModel(*animator_);

    skinnedShader_   = std::make_unique<Shader>("shaders/skinned.vert",    "shaders/pbr.frag");
    boneDebugShader_ = std::make_unique<Shader>("shaders/bone_debug.vert", "shaders/bone_debug.frag");
    gridShader_      = std::make_unique<Shader>("shaders/grid.vert",       "shaders/grid.frag");
    depthShader_     = std::make_unique<Shader>("shaders/depth.vert",      "shaders/depth.frag");
    groundShader_    = std::make_unique<Shader>("shaders/ground.vert",     "shaders/ground.frag");
    if (!skyboxShader_)
        skyboxShader_ = std::make_unique<Shader>("shaders/skybox.vert",    "shaders/skybox.frag");

    // Both the main and the depth pass read the bone matrices from UBO binding 0.
    for (Shader* s : {skinnedShader_.get(), depthShader_.get()}) {
        unsigned int blockIdx = glGetUniformBlockIndex(s->id, "BoneMatrices");
        if (blockIdx != GL_INVALID_INDEX)
            glUniformBlockBinding(s->id, blockIdx, 0);
    }
}

// ── Auto-frame the orbit camera to the loaded model's bounds ───────────────────

void App::frameCameraToModel() {
    // World-space AABB: transform the 8 local corners by rootTransform.
    glm::vec3 wmin( 1e9f), wmax(-1e9f);
    for (int i = 0; i < 8; ++i) {
        glm::vec3 c(
            (i & 1) ? model_.aabbMax.x : model_.aabbMin.x,
            (i & 2) ? model_.aabbMax.y : model_.aabbMin.y,
            (i & 4) ? model_.aabbMax.z : model_.aabbMin.z);
        glm::vec3 w = glm::vec3(model_.rootTransform * glm::vec4(c, 1.0f));
        wmin = glm::min(wmin, w);
        wmax = glm::max(wmax, w);
    }
    glm::vec3 center = 0.5f * (wmin + wmax);
    float sphereRadius = glm::max(0.5f * glm::length(wmax - wmin), 0.1f);

    // Distance so the bounding sphere fits the vertical FOV, with margin.
    float halfFov = glm::radians(camera_.fovY * 0.5f);
    camera_.target = center;
    camera_.radius = (sphereRadius / std::tan(halfFov)) * 1.5f;
    camera_.yaw    = 0.0f;
    camera_.pitch  = 22.0f;   // look down enough to reveal the ground + shadow
}

// ── Main loop ─────────────────────────────────────────────────────────────────

void App::run() {
    // IBL precompute (loads shaders from shaders/, runs once at startup)
    environment_.init();

    modelPaths_ = {
        "assets/CesiumMan/CesiumMan.gltf",
        "assets/RiggedFigure/RiggedFigure.gltf",
        "assets/Fox/Fox.gltf"
    };
    loadModel(modelPaths_[modelIndex_]);

    double prevTime = glfwGetTime();
    int    frames   = 0;
    double fpsTimer = 0.0;

    while (!glfwWindowShouldClose(window_)) {
        double now = glfwGetTime();
        float  dt  = (float)(now - prevTime);
        prevTime   = now;

        // FPS counter — recalculated once per second
        frames++;
        fpsTimer += dt;
        if (fpsTimer >= 1.0) {
            cachedFps_ = frames;
            frames = 0; fpsTimer = 0.0;
        }

        // Title bar
        {
            std::ostringstream title;
            title << "Skeletal Viewer | FPS: " << cachedFps_;
            if (animator_) {
                title << " | Speed: " << std::fixed << std::setprecision(2)
                      << animator_->speed() << "x";
                title << " | t: " << std::setprecision(3)
                      << animator_->currentTime() << "s";
                if (animator_->clipDuration() > 0.0f)
                    title << "/" << std::setprecision(2) << animator_->clipDuration() << "s";
                title << " | " << (animator_->isPlaying() ? "Playing" : "Paused");
                if (animator_->isBlending())
                    title << " | Blending " << (int)(animator_->blendAlpha() * 100) << "%";
            }
            glfwSetWindowTitle(window_, title.str().c_str());
        }

        processInput(dt);

        if (animator_) animator_->update(dt);
        if (renderer_ && animator_)
            renderer_->uploadSkinningMatrices(animator_->skinningMatrices());

        // Check if the ImGui panel requested a model switch
        ui_.beginFrame();
        {
            int prevModelIndex = modelIndex_;
            bool panelChanged = ui_.draw(
                *animator_, showBones_,
                modelIndex_, modelPaths_,
                cachedFps_,
                renderer_ ? renderer_->totalTriangles() : 0,
                settings_,
                stateMachine_
            );
            if (panelChanged && modelIndex_ != prevModelIndex)
                loadModel(modelPaths_[modelIndex_]);
        }

        render(); // render scene first, then ImGui on top
        ui_.endFrame();

        glfwSwapBuffers(window_);
        glfwPollEvents();
    }
}

// ── Rendering ─────────────────────────────────────────────────────────────────

void App::render() {
    // Use the real framebuffer size (Retina = 2x window points) for aspect + viewport.
    int fbw = width_, fbh = height_;
    glfwGetFramebufferSize(window_, &fbw, &fbh);
    if (fbh == 0) fbh = 1;

    float aspect = (float)fbw / (float)fbh;
    glm::mat4 view  = camera_.view();
    glm::mat4 proj  = camera_.projection(aspect);
    glm::mat4 model = model_.rootTransform;
    glm::vec3 lightDir = settings_.lightDir();

    // ── World-space bounds of the model (8 transformed AABB corners) ───────────
    glm::vec3 wmin( 1e9f), wmax(-1e9f);
    for (int i = 0; i < 8; ++i) {
        glm::vec3 c(
            (i & 1) ? model_.aabbMax.x : model_.aabbMin.x,
            (i & 2) ? model_.aabbMax.y : model_.aabbMin.y,
            (i & 4) ? model_.aabbMax.z : model_.aabbMin.z);
        glm::vec3 w = glm::vec3(model * glm::vec4(c, 1.0f));
        wmin = glm::min(wmin, w);
        wmax = glm::max(wmax, w);
    }
    glm::vec3 center = 0.5f * (wmin + wmax);
    float radius = glm::max(0.5f * glm::length(wmax - wmin) * 1.2f, 0.5f);
    float groundY = wmin.y;

    glm::mat4 lightVP = ShadowMap::lightSpaceMatrix(center, radius, lightDir);

    // ── Pass 1: depth from the light's point of view ───────────────────────────
    if (renderer_ && depthShader_ && settings_.shadowEnabled) {
        shadowMap_.beginDepthPass();
        glClear(GL_DEPTH_BUFFER_BIT);
        depthShader_->use();
        depthShader_->setMat4("uModel",   model);
        depthShader_->setMat4("uLightVP", lightVP);
        renderer_->drawDepth(*depthShader_);
        ShadowMap::endDepthPass(fbw, fbh);
    }

    // ── Pass 2: main scene (linear lighting -> sRGB framebuffer) ───────────────
    glEnable(GL_FRAMEBUFFER_SRGB);
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Shadow map is bound to texture unit 1 for every shadow-receiving shader.
    const int SHADOW_UNIT = 1;
    glActiveTexture(GL_TEXTURE0 + SHADOW_UNIT);
    glBindTexture(GL_TEXTURE_2D, shadowMap_.depthTexture());
    glActiveTexture(GL_TEXTURE0);

    if (renderer_ && skinnedShader_) {
        skinnedShader_->use();
        skinnedShader_->setMat4("uModel",         model);
        skinnedShader_->setMat4("uView",          view);
        skinnedShader_->setMat4("uProjection",    proj);
        skinnedShader_->setVec3 ("uLightDir",         lightDir);
        skinnedShader_->setVec3 ("uCamPos",           camera_.position());
        skinnedShader_->setVec3 ("uLightColor",       glm::vec3(settings_.lightIntensity));
        skinnedShader_->setMat4 ("uLightVP",          lightVP);
        skinnedShader_->setInt  ("uShadowMap",        SHADOW_UNIT);
        skinnedShader_->setInt  ("uShadowEnabled",    settings_.shadowEnabled ? 1 : 0);
        skinnedShader_->setFloat("uShadowBias",       settings_.shadowBias);
        skinnedShader_->setFloat("uMetallicOverride",  settings_.metallicOverride);
        skinnedShader_->setFloat("uRoughnessOverride", settings_.roughnessOverride);

        // IBL textures
        const int IBL_IRRADIANCE = 5, IBL_PREFILTERED = 6, IBL_BRDF = 7;
        glActiveTexture(GL_TEXTURE0 + IBL_IRRADIANCE);
        glBindTexture(GL_TEXTURE_CUBE_MAP, environment_.irradianceMap());
        glActiveTexture(GL_TEXTURE0 + IBL_PREFILTERED);
        glBindTexture(GL_TEXTURE_CUBE_MAP, environment_.prefilteredMap());
        glActiveTexture(GL_TEXTURE0 + IBL_BRDF);
        glBindTexture(GL_TEXTURE_2D, environment_.brdfLut());
        glActiveTexture(GL_TEXTURE0);

        skinnedShader_->setInt("uIrradianceMap",  IBL_IRRADIANCE);
        skinnedShader_->setInt("uPrefilteredMap", IBL_PREFILTERED);
        skinnedShader_->setInt("uBrdfLut",        IBL_BRDF);
        skinnedShader_->setInt("uIBLEnabled",     settings_.iblEnabled ? 1 : 0);

        renderer_->drawSkinned(*skinnedShader_);
    }

    if (renderer_ && groundShader_ && settings_.showGround) {
        groundShader_->use();
        groundShader_->setMat4("uView",          view);
        groundShader_->setMat4("uProjection",    proj);
        groundShader_->setVec3("uLightDir",      lightDir);
        groundShader_->setVec3("uCamPos",        camera_.position());
        groundShader_->setMat4("uLightVP",       lightVP);
        groundShader_->setInt ("uShadowMap",     SHADOW_UNIT);
        groundShader_->setInt ("uShadowEnabled", settings_.shadowEnabled ? 1 : 0);
        groundShader_->setFloat("uShadowBias",   settings_.shadowBias);
        renderer_->drawGroundPlane(*groundShader_, groundY, radius * 2.0f);
    }

    // ── Skybox ─────────────────────────────────────────────────────────────────
    if (skyboxShader_ && settings_.iblEnabled && settings_.skyboxEnabled) {
        glDepthFunc(GL_LEQUAL);  // pass at depth = 1.0 (far plane)
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, environment_.envCubemap());
        skyboxShader_->use();
        skyboxShader_->setMat4("uView",       view);
        skyboxShader_->setMat4("uProjection", proj);
        skyboxShader_->setInt ("uEnvMap",     0);
        glDisable(GL_CULL_FACE);
        environment_.drawCube();
        glEnable(GL_CULL_FACE);
        glDepthFunc(GL_LESS);
    }

    if (renderer_ && gridShader_) {
        gridShader_->use();
        gridShader_->setMat4("uView",       view);
        gridShader_->setMat4("uProjection", proj);
        renderer_->drawGrid(*gridShader_);
    }

    if (showBones_ && renderer_ && boneDebugShader_ && animator_) {
        boneDebugShader_->use();
        boneDebugShader_->setMat4("uModel",      model);
        boneDebugShader_->setMat4("uView",       view);
        boneDebugShader_->setMat4("uProjection", proj);
        boneDebugShader_->setVec3("uColor",      glm::vec3(0.0f, 1.0f, 0.2f));
        renderer_->drawBones(*boneDebugShader_,
                             animator_->globalTransforms(),
                             model_.skeleton);
    }

    // IK target marker (model-local space, same as bone positions).
    if (renderer_ && boneDebugShader_ && animator_ &&
        animator_->isIKEnabled() && animator_->ikValid()) {
        float markerSize = 0.04f * glm::length(model_.aabbMax - model_.aabbMin);
        boneDebugShader_->use();
        boneDebugShader_->setMat4("uModel",      model);
        boneDebugShader_->setMat4("uView",       view);
        boneDebugShader_->setMat4("uProjection", proj);
        boneDebugShader_->setVec3("uColor",      glm::vec3(1.0f, 0.85f, 0.1f));
        renderer_->drawMarker(*boneDebugShader_, animator_->ikTarget(), markerSize);
    }

    // ImGui must draw in plain (non-sRGB) space, or its colors wash out.
    glDisable(GL_FRAMEBUFFER_SRGB);
}

// ── Keyboard ──────────────────────────────────────────────────────────────────

void App::processInput(float /*dt*/) {
    if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window_, true);
}

// ── GLFW Callbacks ────────────────────────────────────────────────────────────

void App::cbCursorPos(GLFWwindow* w, double x, double y) {
    auto* app = (App*)glfwGetWindowUserPointer(w);
    if (app->ui_.wantsMouse()) return; // ImGui has focus
    if (app->firstMouse_) { app->lastX_ = x; app->lastY_ = y; app->firstMouse_ = false; }
    double dx = x - app->lastX_;
    double dy = app->lastY_ - y;
    app->lastX_ = x; app->lastY_ = y;
    app->camera_.onMouseMove(dx, dy, app->lmbDown_, app->rmbDown_);
}

void App::cbScroll(GLFWwindow* w, double, double dy) {
    auto* app = (App*)glfwGetWindowUserPointer(w);
    if (app->ui_.wantsMouse()) return;
    app->camera_.onScroll(dy);
}

void App::cbKey(GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
    auto* app = (App*)glfwGetWindowUserPointer(w);
    if (app->ui_.wantsKeyboard()) return; // ImGui text field has focus
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    if (!app->animator_) return;

    switch (key) {
        case GLFW_KEY_SPACE:
            app->animator_->setPlaying(!app->animator_->isPlaying());
            break;
        case GLFW_KEY_R:
            app->animator_->resetTime();
            break;
        // Instant clip switch
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
        // Smooth blend to next clip (N key)
        case GLFW_KEY_N: {
            int next = (app->animator_->clipIndex() + 1) % std::max(1, app->animator_->clipCount());
            app->animator_->blendTo(next, 0.4f);
            break;
        }
        case GLFW_KEY_UP:
        case GLFW_KEY_KP_ADD:
            app->animator_->setSpeed(
                std::clamp(app->animator_->speed() * 1.25f, 0.05f, 8.0f));
            break;
        case GLFW_KEY_DOWN:
        case GLFW_KEY_KP_SUBTRACT:
            app->animator_->setSpeed(
                std::clamp(app->animator_->speed() * 0.8f, 0.05f, 8.0f));
            break;
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
            app->modelIndex_ = (app->modelIndex_ + 1) % (int)app->modelPaths_.size();
            app->loadModel(app->modelPaths_[app->modelIndex_]);
            break;
        }
        case GLFW_KEY_0:
            app->animator_->freezeBindPose();
            app->animator_->validateBindPose();
            app->animator_->printClipDiagnostic();
            break;
        // State machine triggers
        case GLFW_KEY_1:
            app->stateMachine_.request(AnimStateMachine::State::Walk, *app->animator_);
            break;
        case GLFW_KEY_2:
            app->stateMachine_.request(AnimStateMachine::State::Run, *app->animator_);
            break;
        // Toggle inverse kinematics
        case GLFW_KEY_I: {
            bool en = !app->animator_->isIKEnabled();
            if (en && app->animator_->ikValid())
                app->animator_->setIKTarget(app->animator_->effectorWorldPos());
            app->animator_->setIKEnabled(en);
            break;
        }
    }
}

void App::cbChar(GLFWwindow* w, unsigned int cp) {
    auto* app = (App*)glfwGetWindowUserPointer(w);
    if (app->ui_.wantsKeyboard()) return;
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
