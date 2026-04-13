#pragma once
#include "Model.h"
#include <vector>
#include <glm/glm.hpp>

class Animator {
public:
    explicit Animator(const Model* model);

    void update(float dt);
    void setClip(int idx);
    void setPlaying(bool v)    { playing_ = v; }
    void setSpeed(float s)     { speed_ = s; }
    void resetTime()           { time_ = 0.0f; }
    void freezeBindPose()      { time_ = 0.0f; playing_ = false; update(0.0f); }

    // Week 3: manual time scrubbing — clamps to [0, duration]
    void stepTime(float delta);
    float clipDuration() const;

    bool  isPlaying()          const { return playing_; }
    float speed()              const { return speed_; }
    float currentTime()        const { return time_; }
    int   clipIndex()          const { return clipIdx_; }
    int   clipCount()          const;

    // Week 2 diagnostics
    void validateBindPose()    const; // prints max deviation of skin[i] from identity at t=0
    void printClipDiagnostic() const; // prints first 3 keyframes of root bone

    // Returns current per-bone skinning matrices (size = skeleton.size())
    const std::vector<glm::mat4>& skinningMatrices() const { return skinning_; }
    // Returns current global transforms (for debug bone drawing)
    const std::vector<glm::mat4>& globalTransforms() const { return global_; }

private:
    const Model* model_;
    int   clipIdx_  = 0;
    float time_     = 0.0f;
    bool  playing_  = true;
    float speed_    = 1.0f;

    std::vector<glm::mat4> local_;
    std::vector<glm::mat4> global_;
    std::vector<glm::mat4> skinning_;

    void computeLocal();
    void computeGlobal();
    void computeSkinning();

    static glm::vec3 sampleVec3(const std::vector<float>& times,
                                 const std::vector<glm::vec3>& vals, float t);
    static glm::quat sampleQuat(const std::vector<float>& times,
                                 const std::vector<glm::quat>& vals, float t);
};
