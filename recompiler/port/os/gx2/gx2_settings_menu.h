// gx2_settings_menu.h - the port's ESC settings menu.
//
// Opened with Esc on the keyboard or the touchpad button of a PlayStation
// controller (the GamePad HOME button moved to H). Two pages show and rebind
// the keyboard/mouse and controller inputs (port_settings.h). While it is
// open the game receives a neutral pad; it keeps running. Game thread: input and actions
// (GX2SwapScanBuffers' event loop, VPADRead). Render thread: gx2vk::present
// draws the current view.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

union SDL_Event;

struct Gx2MenuLine {
    std::string label, value;
    bool selected = false;
    bool adjustable = false;    // left/right change the value
    bool note = false;          // value shown as a pending-change note
};
struct Gx2MenuView {
    std::string title, hint;
    std::string help;           // what the selected row does, in plain words
    std::vector<Gx2MenuLine> lines;
};

// Game-side hooks the menu calls (gx2_context.cpp).
struct Gx2MenuActions {
    void (*set_fullscreen)(bool) = nullptr;
    bool (*fullscreen)() = nullptr;
    void (*set_fps_overlay)(bool) = nullptr;
    void (*quit)() = nullptr;
};
void gx2_settings_menu_init(const Gx2MenuActions& actions);
// Returns true when the menu consumed the event.
bool gx2_settings_menu_event(const SDL_Event& event);
bool gx2_settings_menu_open();
// True while the menu is open and until every key/button held when it
// closed is released (the confirming press must not reach the game).
bool gx2_settings_menu_blocks_input(bool any_input_held);
// Render thread: copies the view when it changed since `revision`.
// False when the menu is closed.
bool gx2_settings_menu_view(Gx2MenuView& view, uint64_t& revision);
