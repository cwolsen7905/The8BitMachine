#pragma once

// ---------------------------------------------------------------------------
// IKeyMapper — translates SDL key events into hardware matrix state.
//
// Each preset installs a concrete mapper; Machine::keyEvent() / clearKeys()
// delegate to it.  Implementations that don't need a class can use the
// LambdaKeyMapper wrapper in Machine.cpp.
// ---------------------------------------------------------------------------

class IKeyMapper {
public:
    virtual ~IKeyMapper() = default;
    // sdlSym is SDL_Keycode cast to int so the core stays SDL-free.
    virtual void keyEvent(int sdlSym, bool pressed) = 0;
    virtual void clearKeys() = 0;
};
