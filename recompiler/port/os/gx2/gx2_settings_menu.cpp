#include "gx2_settings_menu.h"
#include "../../runtime/port_settings.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>
#include <mutex>

namespace {
namespace rs = rbrew_settings;

enum Page { kMain, kKeyboard, kController };
enum MainItem { kResume, kResolution, kPlaza60, kFullscreen, kFpsOverlay, kGyro, kGyroSensitivity, kGyroInvert,
                kMouseSensitivity, kVolume, kKeyboardPage, kControllerPage, kQuit, kMainCount };

std::mutex s_lock;
Gx2MenuActions s_actions;
bool s_open = false;
bool s_block_until_released = false;
Page s_page = kMain;
int s_selected[3] = {};
bool s_capture = false;      // waiting for the key/button of the selected action
bool s_quit_armed = false;
uint64_t s_revision = 1;
Gx2MenuView s_view;

const char* const kResolutionLabels[rs::kResolutionCount] = {"720p (console)", "1080p", "1440p", "Display"};
// GamePad input, with what it does in Splatoon where it is fixed.
const char* const kActionLabels[rs::kActionCount] = {
    "A  (confirm)", "B  (jump)", "X", "Y", "L", "R  (sub weapon)", "ZL  (squid form)", "ZR  (shoot)",
    "+  (Plus)", "-  (Minus)", "HOME", "D-pad up", "D-pad down", "D-pad left", "D-pad right",
    "Left stick click", "Right stick click", "Move forward", "Move back", "Move left", "Move right"};

const char* on_off(bool on) { return on ? "On" : "Off"; }
std::string percent(int value) { char text[16]; std::snprintf(text, sizeof(text), "%d %%", value); return text; }

int action_count(Page page) { return page == kKeyboard ? rs::kActionCount : rs::kFirstMoveAction; }
int item_count(Page page) { return page == kMain ? kMainCount : action_count(page) + 2; }   // + reset, back

std::string key_name(int binding) {
    if (binding == rs::kUnbound) return "-";
    if (binding >= rs::kMouseBase) {
        switch (binding - rs::kMouseBase) {
        case SDL_BUTTON_LEFT: return "Mouse left";
        case SDL_BUTTON_MIDDLE: return "Mouse middle";
        case SDL_BUTTON_RIGHT: return "Mouse right";
        case SDL_BUTTON_X1: return "Mouse 4";
        default: return "Mouse 5";
        }
    }
    const char* name = SDL_GetScancodeName(SDL_Scancode(binding));
    return name && *name ? name : "Key " + std::to_string(binding);
}
std::string pad_name(int binding) {
    switch (binding) {
    case rs::kUnbound: return "-";
    case rs::kPadTriggerLeft: return "L2 (LT)";
    case rs::kPadTriggerRight: return "R2 (RT)";
    case SDL_CONTROLLER_BUTTON_A: return "Cross (A)";
    case SDL_CONTROLLER_BUTTON_B: return "Circle (B)";
    case SDL_CONTROLLER_BUTTON_X: return "Square (X)";
    case SDL_CONTROLLER_BUTTON_Y: return "Triangle (Y)";
    case SDL_CONTROLLER_BUTTON_BACK: return "Share (Back)";
    case SDL_CONTROLLER_BUTTON_GUIDE: return "PS (Guide)";
    case SDL_CONTROLLER_BUTTON_START: return "Options (Start)";
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return "L3";
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return "R3";
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return "L1 (LB)";
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "R1 (RB)";
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return "D-pad up";
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return "D-pad down";
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return "D-pad left";
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return "D-pad right";
    case SDL_CONTROLLER_BUTTON_MISC1: return "Mute / Share";
    default: return "Paddle " + std::to_string(binding - SDL_CONTROLLER_BUTTON_PADDLE1 + 1);
    }
}

Gx2MenuView build_view() {
    const auto v = rs::get();
    Gx2MenuView view;
    auto line = [&](const std::string& label, std::string value, bool adjustable, bool selected) {
        Gx2MenuLine l;
        l.label = label; l.value = std::move(value); l.adjustable = adjustable; l.selected = selected;
        view.lines.push_back(std::move(l));
    };
    if (s_page == kMain) {
        view.title = "Settings";
        view.hint = "Up/Down: select    Left/Right: change    Enter/Cross: OK    Esc/Circle: close";
        auto item = [&](MainItem id, const char* label, std::string value, bool adjustable) {
            line(label, std::move(value), adjustable, s_selected[kMain] == id);
        };
        item(kResume, "Resume", "", false);
        std::string resolution = kResolutionLabels[v.resolution];
        if (v.resolution != rs::active_resolution()) resolution += "  (after restart)";
        item(kResolution, "Render resolution", resolution, true);
        item(kPlaza60, "Plaza 60 FPS", on_off(v.plaza_60fps), true);
        item(kFullscreen, "Fullscreen", on_off(s_actions.fullscreen ? s_actions.fullscreen() : v.fullscreen), true);
        item(kFpsOverlay, "FPS overlay", on_off(v.fps_overlay), true);
        item(kGyro, "Controller gyro", on_off(v.gyro), true);
        item(kGyroSensitivity, "Gyro sensitivity", percent(v.gyro_sensitivity), true);
        item(kGyroInvert, "Invert gyro up/down", on_off(v.gyro_invert_pitch), true);
        item(kMouseSensitivity, "Mouse sensitivity", percent(v.mouse_sensitivity), true);
        item(kVolume, "Volume", percent(v.volume), true);
        item(kKeyboardPage, "Keyboard & mouse controls", ">", false);
        item(kControllerPage, "Controller controls", ">", false);
        item(kQuit, "Quit game", s_quit_armed ? "Press again to confirm" : "", false);
        return view;
    }
    const bool keyboard = s_page == kKeyboard;
    const int selected = s_selected[s_page], count = action_count(s_page);
    view.title = keyboard ? "Keyboard & mouse controls" : "Controller controls";
    if (s_capture)
        view.hint = keyboard ? "Press a key or mouse button for the selected input    Esc: cancel"
                             : "Press a controller button for the selected input    Touchpad/Esc: cancel";
    else
        view.hint = keyboard ? "Enter: change    Delete: clear    Esc: back    (mouse moves the camera)"
                             : "Cross: change    Square: clear    Circle: back    (sticks: move / camera)";
    for (int action = 0; action < count; ++action) {
        const int binding = keyboard ? v.keys[action] : v.pad[action];
        const bool row = selected == action;
        line(kActionLabels[action], row && s_capture ? "press ..." : keyboard ? key_name(binding) : pad_name(binding), false, row);
    }
    line("Reset to defaults", "", false, selected == count);
    line("Back", "", false, selected == count + 1);
    return view;
}
// Game thread: the view is rebuilt where the state changes (window state
// is only queried here, never from the render thread).
void touch() { ++s_revision; s_view = build_view(); }

void set_open(bool open) {
    if (s_open == open) return;
    s_open = open;
    s_quit_armed = false;
    s_capture = false;
    s_page = kMain;
    if (!open) s_block_until_released = true;
    else s_selected[kMain] = kResume;
    touch();
}
void set_page(Page page) {
    s_page = page;
    s_capture = false;
    s_selected[page] = page == kMain ? s_selected[kMain] : 0;
    touch();
}

// Binds the selected action. A key already used by another action moves
// over: that action takes this one's previous binding (a swap).
void bind(int binding) {
    auto v = rs::get();
    int* table = s_page == kKeyboard ? v.keys : v.pad;
    const int action = s_selected[s_page], previous = table[action];
    for (int other = 0; other < action_count(s_page); ++other)
        if (other != action && binding != rs::kUnbound && table[other] == binding) table[other] = previous;
    table[action] = binding;
    s_capture = false;
    rs::set(v);
    touch();
}

// Main page. direction: -1/+1 for left/right, 0 for confirm.
void change(int direction) {
    auto v = rs::get();
    const auto step = [&](int& value, int amount, int low, int high) {
        value = std::clamp(value + (direction ? direction : 1) * amount, low, high);
    };
    switch (s_selected[kMain]) {
    case kResume: if (!direction) set_open(false); return;
    case kResolution: {
        const int n = rs::kResolutionCount;
        v.resolution = (v.resolution + (direction < 0 ? n - 1 : 1)) % n;
        break;
    }
    case kPlaza60: v.plaza_60fps = !v.plaza_60fps; break;
    case kFullscreen: {
        const bool now = s_actions.fullscreen ? s_actions.fullscreen() : v.fullscreen;
        v.fullscreen = !now;
        if (s_actions.set_fullscreen) s_actions.set_fullscreen(v.fullscreen);
        break;
    }
    case kFpsOverlay:
        v.fps_overlay = !v.fps_overlay;
        if (s_actions.set_fps_overlay) s_actions.set_fps_overlay(v.fps_overlay);
        break;
    case kGyro: v.gyro = !v.gyro; break;
    case kGyroSensitivity: step(v.gyro_sensitivity, 25, 25, 300); break;
    case kGyroInvert: v.gyro_invert_pitch = !v.gyro_invert_pitch; break;
    case kMouseSensitivity: step(v.mouse_sensitivity, 25, 25, 400); break;
    case kVolume: step(v.volume, 10, 0, 100); break;
    case kKeyboardPage: if (!direction) set_page(kKeyboard); return;
    case kControllerPage: if (!direction) set_page(kController); return;
    case kQuit:
        if (direction) return;
        if (s_quit_armed && s_actions.quit) { s_actions.quit(); return; }
        s_quit_armed = true; touch();
        return;
    }
    rs::set(v);
    touch();
}
// Controls pages: confirm on a row.
void confirm_controls() {
    const int selected = s_selected[s_page], count = action_count(s_page);
    if (selected < count) { s_capture = true; touch(); return; }
    if (selected == count) {
        auto v = rs::get();
        int keys[rs::kActionCount], pad[rs::kActionCount];
        rs::default_bindings(keys, pad);
        std::copy(std::begin(keys), std::end(keys), v.keys);
        std::copy(std::begin(pad), std::end(pad), v.pad);
        rs::set(v);
        touch();
        return;
    }
    set_page(kMain);
}

void move(int delta) {
    const int n = item_count(s_page);
    s_selected[s_page] = (s_selected[s_page] + delta + n) % n;
    s_quit_armed = false;
    touch();
}

enum class Key { None, Escape, Touchpad, Up, Down, Left, Right, Confirm, Back, Clear };
Key key_of(const SDL_Event& e) {
    if (e.type == SDL_KEYDOWN) {
        const bool repeat = e.key.repeat != 0;   // held arrows keep moving
        switch (e.key.keysym.scancode) {
        case SDL_SCANCODE_ESCAPE: return repeat ? Key::None : Key::Escape;
        case SDL_SCANCODE_UP: return Key::Up;
        case SDL_SCANCODE_DOWN: return Key::Down;
        case SDL_SCANCODE_LEFT: return Key::Left;
        case SDL_SCANCODE_RIGHT: return Key::Right;
        case SDL_SCANCODE_W: return repeat ? Key::None : Key::Up;
        case SDL_SCANCODE_S: return repeat ? Key::None : Key::Down;
        case SDL_SCANCODE_A: return repeat ? Key::None : Key::Left;
        case SDL_SCANCODE_D: return repeat ? Key::None : Key::Right;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE: return repeat ? Key::None : Key::Confirm;
        case SDL_SCANCODE_BACKSPACE: return repeat ? Key::None : Key::Back;
        case SDL_SCANCODE_DELETE: return repeat ? Key::None : Key::Clear;
        default: return Key::None;
        }
    }
    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (e.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_TOUCHPAD: return Key::Touchpad;
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return Key::Up;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return Key::Down;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return Key::Left;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return Key::Right;
        case SDL_CONTROLLER_BUTTON_A: return Key::Confirm;   // south: Cross
        case SDL_CONTROLLER_BUTTON_B: return Key::Back;      // east: Circle
        case SDL_CONTROLLER_BUTTON_X: return Key::Clear;     // west: Square
        default: return Key::None;
        }
    }
    return Key::None;
}

// Capture mode: true when the event was the new binding (or cancelled it).
bool capture_event(const SDL_Event& e) {
    const Key key = key_of(e);
    if (key == Key::Escape || key == Key::Touchpad) { s_capture = false; touch(); return true; }
    if (s_page == kKeyboard) {
        if (e.type == SDL_KEYDOWN && !e.key.repeat) { bind(int(e.key.keysym.scancode)); return true; }
        if (e.type == SDL_MOUSEBUTTONDOWN) { bind(rs::kMouseBase + e.button.button); return true; }
    } else {
        if (e.type == SDL_CONTROLLERBUTTONDOWN) { bind(int(e.cbutton.button)); return true; }
        if (e.type == SDL_CONTROLLERAXISMOTION && e.caxis.value > 16000) {
            if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) { bind(rs::kPadTriggerLeft); return true; }
            if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) { bind(rs::kPadTriggerRight); return true; }
        }
    }
    return e.type == SDL_KEYDOWN || e.type == SDL_CONTROLLERBUTTONDOWN;
}
}  // namespace

void gx2_settings_menu_init(const Gx2MenuActions& actions) {
    std::lock_guard lock(s_lock);
    s_actions = actions;
}

bool gx2_settings_menu_event(const SDL_Event& event) {
    std::lock_guard lock(s_lock);
    if (s_open && s_capture) return capture_event(event);
    const Key key = key_of(event);
    if (key == Key::Touchpad) { set_open(!s_open); return true; }
    if (key == Key::Escape) {
        if (s_open && s_page != kMain) set_page(kMain);
        else set_open(!s_open);
        return true;
    }
    if (!s_open) return false;
    const bool main = s_page == kMain;
    switch (key) {
    case Key::Up: move(-1); break;
    case Key::Down: move(1); break;
    case Key::Left: if (main) change(-1); break;
    case Key::Right: if (main) change(1); break;
    case Key::Confirm: if (main) change(0); else confirm_controls(); break;
    case Key::Back: if (main) set_open(false); else set_page(kMain); break;
    case Key::Clear:
        if (!main && s_selected[s_page] < action_count(s_page)) bind(rs::kUnbound);
        break;
    default: break;
    }
    // Every key press goes to the menu while it is open (Tab, F-keys too).
    return event.type == SDL_KEYDOWN || event.type == SDL_CONTROLLERBUTTONDOWN;
}

bool gx2_settings_menu_open() {
    std::lock_guard lock(s_lock);
    return s_open;
}

bool gx2_settings_menu_blocks_input(bool any_input_held) {
    std::lock_guard lock(s_lock);
    if (s_open) return true;
    if (s_block_until_released && !any_input_held) s_block_until_released = false;
    return s_block_until_released;
}

bool gx2_settings_menu_view(Gx2MenuView& view, uint64_t& revision) {
    std::lock_guard lock(s_lock);
    if (!s_open) return false;
    if (revision != s_revision) {
        view = s_view;
        revision = s_revision;
    }
    return true;
}
