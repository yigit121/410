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

    // ── Week 6: Animation blending ────────────────────────────────────────────
    // Start a smooth crossfade from the current clip to clipIdx over durationSec seconds.
    void  blendTo(int clipIdx, float durationSec = 0.5f);
    bool  isBlending()  const { return blending_; }
    float blendAlpha()  const { return blendAlpha_; }
    int   blendTarget() const { return blendTarget_; }

    // Clip name helper — empty string if idx out of range
    std::string clipName(int idx) const;

private:
    const Model* model_;
    int   clipIdx_  = 0;
    float time_     = 0.0f;
    bool  playing_  = true;
    float speed_    = 1.0f;

    std::vector<glm::mat4> local_;
    std::vector<glm::mat4> global_;
    std::vector<glm::mat4> skinning_;

    // Blending state
    bool  blending_        = false;
    int   blendTarget_     = 0;
    float blendSrcTime_    = 0.0f;   // source clip time frozen at blend start
    float blendTargetTime_ = 0.0f;   // target clip time advancing from 0
    float blendTime_       = 0.0f;   // elapsed blend time
    float blendDur_        = 0.5f;   // total blend duration in seconds
    float blendAlpha_      = 0.0f;   // current blend weight [0=src, 1=dst]

    void computeLocal();
    void computeGlobal();
    void computeSkinning();

    static glm::vec3 sampleVec3(const std::vector<float>& times,
                                 const std::vector<glm::vec3>& vals, float t);
    static glm::quat sampleQuat(const std::vector<float>& times,
                                 const std::vector<glm::quat>& vals, float t);
};
