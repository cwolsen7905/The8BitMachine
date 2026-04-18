#pragma once

// ---------------------------------------------------------------------------
// IHasPanel — optional interface for devices that expose an ImGui debug panel.
//
// Implement this alongside IBusDevice to opt a device into the Debug menu.
// Devices that do not implement IHasPanel require no ImGui knowledge at all.
//
// drawPanel() is called each frame while the panel is open.  title comes from
// the bus entry label so two CIAs get distinct window titles.
// ---------------------------------------------------------------------------
class IHasPanel {
public:
    virtual ~IHasPanel() = default;
    virtual void drawPanel(const char* title, bool* open) = 0;
};
