#include "AnimStateMachine.h"
#include "Animator.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}
// Find the first clip whose name contains `key` (case-insensitive); -1 if none.
int findClip(const Animator& a, const char* key) {
    std::string k = lower(key);
    for (int i = 0; i < a.clipCount(); ++i)
        if (lower(a.clipName(i)).find(k) != std::string::npos) return i;
    return -1;
}
} // namespace

void AnimStateMachine::configureForModel(Animator& a) {
    int n = a.clipCount();
    multiClip_ = n >= 2;

    if (multiClip_) {
        int w = findClip(a, "walk");
        int r = findClip(a, "run");
        // Fall back to the first two clips when names don't match.
        walkClip_ = (w >= 0) ? w : 0;
        runClip_  = (r >= 0) ? r : std::min(1, n - 1);
    } else {
        walkClip_ = runClip_ = 0;
    }

    state_ = State::Walk;
    request(State::Walk, a);
}

void AnimStateMachine::request(State s, Animator& a) {
    state_ = s;
    if (multiClip_) {
        int target = (s == State::Run) ? runClip_ : walkClip_;
        a.setSpeed(1.0f);
        if (a.clipIndex() != target)
            a.blendTo(target, 0.3f);   // smooth crossfade between real clips
    } else {
        a.setSpeed(s == State::Run ? 2.2f : 1.0f);
    }
    a.setPlaying(true);
}

const char* AnimStateMachine::stateName() const {
    switch (state_) {
        case State::Walk: return "Walk";
        case State::Run:  return "Run";
    }
    return "?";
}
