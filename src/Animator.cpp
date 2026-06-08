#include "Animator.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>

Animator::Animator(const Model* model) : model_(model) {
    size_t n = model_->skeleton.size();
    local_   .assign(n, glm::mat4(1.0f));
    global_  .assign(n, glm::mat4(1.0f));
    skinning_.assign(n, glm::mat4(1.0f));
}

int Animator::clipCount() const {
    return (int)model_->animations.size();
}

std::string Animator::clipName(int idx) const {
    if (idx < 0 || idx >= clipCount()) return "";
    return model_->animations[idx].name;
}

void Animator::setClip(int idx) {
    if (idx < 0 || idx >= clipCount()) return;
    clipIdx_ = idx;
    time_    = 0.0f;
    blending_ = false;
}

// ── Week 6: Start a crossfade to another clip ─────────────────────────────────

void Animator::blendTo(int clipIdx, float dur) {
    if (clipIdx < 0 || clipIdx >= clipCount() || clipIdx == clipIdx_) return;
    blendTarget_     = clipIdx;
    blendSrcTime_    = time_;      // freeze source at current playhead position
    blendTargetTime_ = 0.0f;       // start target from its beginning
    blendTime_       = 0.0f;
    blendDur_        = std::max(0.01f, dur);
    blendAlpha_      = 0.0f;
    blending_        = true;
}

// ── Per-frame update ──────────────────────────────────────────────────────────

void Animator::update(float dt) {
    if (!model_->animations.empty()) {
        // Advance the active (source) clip
        if (playing_) {
            const float dur = model_->animations[clipIdx_].duration;
            if (dur > 1e-5f) {
                time_ += dt * speed_;
                time_  = std::fmod(time_, dur);
            }
        }

        // Advance blend if in progress
        if (blending_) {
            blendTime_ += dt;
            blendAlpha_ = std::min(blendTime_ / blendDur_, 1.0f);

            // Advance target clip time
            float targetDur = model_->animations[blendTarget_].duration;
            if (targetDur > 1e-5f) {
                blendTargetTime_ += dt * speed_;
                blendTargetTime_ = std::fmod(blendTargetTime_, targetDur);
            }

            // Blend finished — commit to target clip
            if (blendAlpha_ >= 1.0f) {
                clipIdx_  = blendTarget_;
                time_     = blendTargetTime_;
                blending_ = false;
                blendAlpha_ = 0.0f;
            }
        }
    }

    computeLocal();
    computeGlobal();
    if (ikEnabled_ && ikValid()) {
        solveTwoBoneIK();   // adjusts local_[root], local_[mid]
        computeGlobal();    // re-propagate FK so descendants follow
    }
    computeSkinning();
}

float Animator::clipDuration() const {
    if (model_->animations.empty()) return 0.0f;
    return model_->animations[clipIdx_].duration;
}

// Pause and step by delta seconds — wraps around at clip boundaries
void Animator::stepTime(float delta) {
    playing_ = false;
    float dur = clipDuration();
    if (dur > 0.0f) {
        time_ = std::fmod(time_ + delta + dur, dur); // + dur prevents negative fmod
    }
    computeLocal();
    computeGlobal();
    computeSkinning();
}

// ── Keyframe sampling ─────────────────────────────────────────────────────────

glm::vec3 Animator::sampleVec3(const std::vector<float>& times,
                                const std::vector<glm::vec3>& vals, float t) {
    if (vals.empty()) return glm::vec3(0);
    if (vals.size() == 1 || t <= times.front()) return vals.front();
    if (t >= times.back()) return vals.back();

    auto it = std::lower_bound(times.begin(), times.end(), t);
    int  hi = (int)(it - times.begin());
    int  lo = hi - 1;

    float alpha = (t - times[lo]) / (times[hi] - times[lo]);
    return glm::mix(vals[lo], vals[hi], alpha);
}

glm::quat Animator::sampleQuat(const std::vector<float>& times,
                                const std::vector<glm::quat>& vals, float t) {
    if (vals.empty()) return glm::quat(1,0,0,0);
    if (vals.size() == 1 || t <= times.front()) return vals.front();
    if (t >= times.back()) return vals.back();

    auto it = std::lower_bound(times.begin(), times.end(), t);
    int  hi = (int)(it - times.begin());
    int  lo = hi - 1;

    float alpha = (t - times[lo]) / (times[hi] - times[lo]);
    return glm::slerp(vals[lo], vals[hi], alpha);
}

// ── Per-bone local transforms ─────────────────────────────────────────────────

void Animator::computeLocal() {
    int n = (int)model_->skeleton.size();

    if (!blending_) {
        // Single-clip path (original logic)
        for (int i = 0; i < n; i++)
            local_[i] = model_->skeleton[i].localBind;

        if (model_->animations.empty()) return;

        const Animation& clip = model_->animations[clipIdx_];
        for (const Channel& ch : clip.channels) {
            int i = ch.boneIdx;
            if (i < 0 || i >= n) continue;

            // Missing tracks fall back to the bone's BIND value (not a neutral
            // default), so e.g. rotation-only channels keep their bind offset.
            const Bone& bone = model_->skeleton[i];
            glm::vec3 T = ch.T.empty() ? bone.bindT : sampleVec3(ch.times, ch.T, time_);
            glm::quat R = ch.R.empty() ? bone.bindR : sampleQuat(ch.times, ch.R, time_);
            glm::vec3 S = ch.S.empty() ? bone.bindS : sampleVec3(ch.times, ch.S, time_);

            local_[i] = glm::translate(glm::mat4(1.0f), T)
                      * glm::mat4_cast(R)
                      * glm::scale(glm::mat4(1.0f), S);
        }
        return;
    }

    // ── Blending path: sample both clips at TRS level, SLERP/LERP, build mat4 ──

    // Temporary per-bone TRS for source and target
    std::vector<glm::vec3> tA(n), sA(n), tB(n), sB(n);
    std::vector<glm::quat> rA(n), rB(n);

    // Initialise both from bind pose
    for (int i = 0; i < n; i++) {
        tA[i] = tB[i] = model_->skeleton[i].bindT;
        rA[i] = rB[i] = model_->skeleton[i].bindR;
        sA[i] = sB[i] = model_->skeleton[i].bindS;
    }

    // Override with source clip samples (time frozen at blend start)
    if (!model_->animations.empty() && clipIdx_ < (int)model_->animations.size()) {
        const Animation& clipA = model_->animations[clipIdx_];
        for (const Channel& ch : clipA.channels) {
            int i = ch.boneIdx;
            if (i < 0 || i >= n) continue;
            if (!ch.T.empty()) tA[i] = sampleVec3(ch.times, ch.T, blendSrcTime_);
            if (!ch.R.empty()) rA[i] = sampleQuat(ch.times, ch.R, blendSrcTime_);
            if (!ch.S.empty()) sA[i] = sampleVec3(ch.times, ch.S, blendSrcTime_);
        }
    }

    // Override with target clip samples (time advancing from 0)
    if (blendTarget_ < (int)model_->animations.size()) {
        const Animation& clipB = model_->animations[blendTarget_];
        for (const Channel& ch : clipB.channels) {
            int i = ch.boneIdx;
            if (i < 0 || i >= n) continue;
            if (!ch.T.empty()) tB[i] = sampleVec3(ch.times, ch.T, blendTargetTime_);
            if (!ch.R.empty()) rB[i] = sampleQuat(ch.times, ch.R, blendTargetTime_);
            if (!ch.S.empty()) sB[i] = sampleVec3(ch.times, ch.S, blendTargetTime_);
        }
    }

    // Blend and build local mat4 for each bone
    float a = blendAlpha_;
    for (int i = 0; i < n; i++) {
        glm::vec3 T = glm::mix(tA[i], tB[i], a);
        glm::quat R = glm::slerp(rA[i], rB[i], a);
        glm::vec3 S = glm::mix(sA[i], sB[i], a);
        local_[i] = glm::translate(glm::mat4(1.0f), T)
                  * glm::mat4_cast(R)
                  * glm::scale(glm::mat4(1.0f), S);
    }
}

// ── Global transforms (single forward pass) ──────────────────────────────────

void Animator::computeGlobal() {
    for (int i = 0; i < (int)model_->skeleton.size(); i++) {
        int p = model_->skeleton[i].parent;
        if (p < 0)
            global_[i] = local_[i];
        else
            global_[i] = global_[p] * local_[i];
    }
}

// ── Skinning matrices ─────────────────────────────────────────────────────────

void Animator::computeSkinning() {
    for (int i = 0; i < (int)model_->skeleton.size(); i++)
        skinning_[i] = global_[i] * model_->skeleton[i].invBind;
}

// ── Week 2 Diagnostics ────────────────────────────────────────────────────────

void Animator::validateBindPose() const {
    int n = (int)model_->skeleton.size();
    std::vector<glm::mat4> loc(n), glob(n), skin(n);

    for (int i = 0; i < n; i++) loc[i] = model_->skeleton[i].localBind;
    for (int i = 0; i < n; i++) {
        int p = model_->skeleton[i].parent;
        glob[i] = (p < 0) ? loc[i] : glob[p] * loc[i];
        skin[i] = glob[i] * model_->skeleton[i].invBind;
    }

    const glm::mat4 identity(1.0f);
    float maxErr = 0.0f;
    int   worstBone = -1;

    for (int i = 0; i < n; i++) {
        float err = 0.0f;
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                err = std::max(err, std::abs(skin[i][c][r] - identity[c][r]));
        if (err > maxErr) { maxErr = err; worstBone = i; }
    }

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "[BindPose] Max deviation from identity: " << maxErr;
    if (maxErr < 1e-4f) {
        std::cout << "  (bind pose import is correct)\n";
    } else {
        std::cout << "  (worst bone: " << worstBone
                  << " \"" << model_->skeleton[worstBone].name << "\")\n";
        std::cout << "[BindPose] skin[" << worstBone << "] =\n";
        for (int r = 0; r < 4; r++) {
            std::cout << "  [";
            for (int c = 0; c < 4; c++)
                std::cout << std::setw(12) << skin[worstBone][c][r];
            std::cout << " ]\n";
        }
    }
}

void Animator::printClipDiagnostic() const {
    if (model_->animations.empty()) {
        std::cout << "[ClipDiag] No animation clips.\n";
        return;
    }
    const Animation& clip = model_->animations[clipIdx_];
    std::cout << "[ClipDiag] Clip \"" << clip.name
              << "\"  duration=" << clip.duration << "s"
              << "  channels=" << clip.channels.size() << "\n";

    const Channel* rootCh = nullptr;
    for (const Channel& ch : clip.channels) {
        if (ch.boneIdx >= 0 &&
            model_->skeleton[ch.boneIdx].parent < 0) {
            rootCh = &ch;
            break;
        }
    }
    if (!rootCh) {
        if (!clip.channels.empty()) rootCh = &clip.channels[0];
        else { std::cout << "[ClipDiag] No channels.\n"; return; }
    }

    int bone = rootCh->boneIdx;
    std::cout << "[ClipDiag] Root bone: " << bone
              << " \"" << model_->skeleton[bone].name << "\"\n";

    int nKeys = (int)rootCh->times.size();
    int show  = std::min(nKeys, 3);
    std::cout << "[ClipDiag] First " << show << " / " << nKeys << " keyframes:\n";
    std::cout << std::setprecision(4);
    for (int k = 0; k < show; k++) {
        std::cout << "  t=" << rootCh->times[k];
        if (k < (int)rootCh->T.size())
            std::cout << "  T=(" << rootCh->T[k].x << ","
                      << rootCh->T[k].y << "," << rootCh->T[k].z << ")";
        if (k < (int)rootCh->R.size())
            std::cout << "  R=(" << rootCh->R[k].w << ","
                      << rootCh->R[k].x << "," << rootCh->R[k].y
                      << "," << rootCh->R[k].z << ")";
        std::cout << "\n";
    }
}

// ── Inverse Kinematics (two-bone analytic solver) ─────────────────────────────

std::string Animator::boneName(int idx) const {
    if (idx < 0 || idx >= boneCount()) return "";
    const std::string& n = model_->skeleton[idx].name;
    return n.empty() ? ("bone_" + std::to_string(idx)) : n;
}

void Animator::setIKEndEffector(int boneIdx) {
    if (boneIdx < 0 || boneIdx >= boneCount()) { ikEnd_ = -1; return; }
    ikEnd_ = boneIdx;
}

// Valid only when the effector has two ancestors (root -> mid -> end).
bool Animator::ikValid() const {
    if (ikEnd_ < 0 || ikEnd_ >= boneCount()) return false;
    int mid = model_->skeleton[ikEnd_].parent;
    if (mid < 0) return false;
    int root = model_->skeleton[mid].parent;
    return root >= 0;
}

glm::vec3 Animator::effectorWorldPos() const {
    if (ikEnd_ < 0 || ikEnd_ >= (int)global_.size()) return glm::vec3(0.0f);
    return glm::vec3(global_[ikEnd_][3]);
}

// Reorthonormalises a global rotation into a local rotation for one bone and
// stores it back into local_, preserving that bone's local translation & scale.
static void applyWorldDeltaToLocal(std::vector<glm::mat4>& local,
                                   const std::vector<glm::mat4>& global,
                                   const std::vector<Bone>& skel,
                                   int bone, const glm::quat& worldDelta) {
    int p = skel[bone].parent;
    glm::quat parentWorldRot = (p < 0) ? glm::quat(1,0,0,0)
                                       : glm::quat_cast(glm::mat3(global[p]));
    // current local rotation
    glm::quat localRot = glm::quat_cast(glm::mat3(local[bone]));
    // Express the world-space delta in the parent's frame, then pre-multiply.
    glm::quat newLocalRot = glm::normalize(
        glm::inverse(parentWorldRot) * worldDelta * parentWorldRot * localRot);

    // Rebuild local matrix from existing T & S plus the new rotation.
    glm::vec3 T = glm::vec3(local[bone][3]);
    glm::vec3 S(glm::length(glm::vec3(local[bone][0])),
                glm::length(glm::vec3(local[bone][1])),
                glm::length(glm::vec3(local[bone][2])));
    local[bone] = glm::translate(glm::mat4(1.0f), T)
                * glm::mat4_cast(newLocalRot)
                * glm::scale(glm::mat4(1.0f), S);
}

void Animator::solveTwoBoneIK() {
    int end  = ikEnd_;
    int mid  = model_->skeleton[end].parent;
    int root = model_->skeleton[mid].parent;

    glm::vec3 a = glm::vec3(global_[root][3]);   // root joint (hip / shoulder)
    glm::vec3 b = glm::vec3(global_[mid][3]);    // mid joint  (knee / elbow)
    glm::vec3 c = glm::vec3(global_[end][3]);    // end joint  (foot / hand)
    glm::vec3 t = ikTarget_;

    const float eps = 1e-5f;
    float lab = glm::length(b - a);
    float lcb = glm::length(c - b);
    if (lab < eps || lcb < eps) return;

    glm::vec3 toTarget = t - a;
    float lat = glm::length(toTarget);
    lat = glm::clamp(lat, std::abs(lab - lcb) + eps, lab + lcb - eps);

    // Current interior angles at root (a) and mid (b).
    auto angle = [](float adj1, float adj2, float opp) {
        float v = (adj1*adj1 + adj2*adj2 - opp*opp) / (2.0f*adj1*adj2);
        return std::acos(glm::clamp(v, -1.0f, 1.0f));
    };
    float ac_ab_0 = angle(lab, glm::length(c - a), lcb); // at root, current
    float ba_bc_0 = angle(lab, lcb, glm::length(c - a)); // at mid,  current
    float ac_ab_1 = angle(lab, lat, lcb);                // at root, desired
    float ba_bc_1 = angle(lab, lcb, lat);                // at mid,  desired

    // Bend axis: normal of the (a,b,c) triangle; fall back to pole hint when straight.
    glm::vec3 axis = glm::cross(c - a, b - a);
    if (glm::length(axis) < eps)
        axis = glm::cross(c - a, ikPoleHint_);
    if (glm::length(axis) < eps)
        axis = glm::vec3(0.0f, 0.0f, 1.0f);
    axis = glm::normalize(axis);

    // 1) Hinge the mid joint to set the elbow/knee bend.
    glm::quat midDelta = glm::angleAxis(ba_bc_1 - ba_bc_0, axis);
    applyWorldDeltaToLocal(local_, global_, model_->skeleton, mid, midDelta);

    // 2) Rotate the root so the chain swings into the bend, then aim at the target.
    glm::quat rootBend = glm::angleAxis(ac_ab_1 - ac_ab_0, axis);
    glm::vec3 dirCur = glm::normalize(c - a);
    glm::vec3 dirTgt = glm::normalize(toTarget);
    glm::quat rootAim = glm::rotation(dirCur, dirTgt);   // glm::gtx::quaternion
    applyWorldDeltaToLocal(local_, global_, model_->skeleton, root,
                           glm::normalize(rootAim * rootBend));
}
