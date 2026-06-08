#pragma once

class Animator;

// Keyboard-triggered locomotion state machine: Walk <-> Run.
//
// For multi-clip models (e.g. Fox: Survey/Walk/Run) Walk and Run crossfade
// between real clips. For single-clip models the same states are emulated by
// scaling playback speed.
class AnimStateMachine {
public:
    enum class State { Walk, Run };

    void  configureForModel(Animator& a); // map states to clips for the loaded model
    void  request(State s, Animator& a);   // handle a key press / button

    State       state()      const { return state_; }
    bool        multiClip()  const { return multiClip_; }
    const char* stateName()  const;

private:
    State state_ = State::Walk;

    bool  multiClip_ = false;
    int   walkClip_  = 0;
    int   runClip_   = 0;
};
