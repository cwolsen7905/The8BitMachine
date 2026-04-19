#pragma once

#include <string>

// ---------------------------------------------------------------------------
// IPeripheral — implemented by any external device the user can attach media
// to (disk drive, tape deck, printer, …).
//
// The Application peripheral registry iterates IPeripheral* to populate the
// Peripherals menu; media management (mount/eject) goes through this interface
// so the menu code stays machine-agnostic.
// ---------------------------------------------------------------------------
class IPeripheral {
public:
    virtual ~IPeripheral() = default;

    // Short human-readable name shown in the Peripherals menu.
    virtual const char* peripheralName() const = 0;

    // Path of the currently mounted image, or empty string if nothing mounted.
    virtual const std::string& mountedImage() const = 0;

    // Mount an image file.  Returns false and sets an error message on failure.
    virtual bool mount(const std::string& path) = 0;
    virtual void eject() = 0;

    // Last error from mount(), cleared on the next mount() call.
    virtual const std::string& mountError() const = 0;
};
