// port_settings.h - player options of the PC port (ESC menu), kept in
// settings.ini next to Splatoon.exe.
//
// Environment switches still win: a variable that is set (RBREW_PLAZA_60FPS,
// RBREW_GYRO, RBREW_RENDER_RESOLUTION, ...) decides the starting value and
// the file is not consulted for it, so tests and benchmark runs stay
// reproducible. Values read on hot paths are atomics.
#pragma once
#include <atomic>
#include <cstdint>

namespace rbrew_settings {

enum Resolution : int { k720p, k1080p, k1440p, kNative, kResolutionCount };

// GamePad inputs that keyboard/mouse and controller buttons can be bound to.
enum Action : int {
    kActA, kActB, kActX, kActY, kActL, kActR, kActZL, kActZR, kActPlus, kActMinus, kActHome,
    kActUp, kActDown, kActLeft, kActRight, kActStickL, kActStickR,
    kActMoveUp, kActMoveDown, kActMoveLeft, kActMoveRight,   // left stick (keyboard only)
    kActionCount
};
constexpr int kFirstMoveAction = kActMoveUp;
constexpr int kUnbound = -1;
// Keyboard bindings are SDL scancodes; kMouseBase + SDL_BUTTON_* is a mouse button.
constexpr int kMouseBase = 1000;
// Controller bindings are SDL_GameControllerButton values, or the triggers.
constexpr int kPadTriggerLeft = 100, kPadTriggerRight = 101;
// The touchpad button opens the menu and cannot be bound.
constexpr int kPadTouchpad = 20;
void default_bindings(int (&keys)[kActionCount], int (&pad)[kActionCount]);

struct Values {
    int resolution = k720p;        // takes effect at the next start
    bool plaza_60fps = true;       // RBREW_PLAZA_60FPS
    bool fullscreen = true;
    bool fps_overlay = true;       // RBREW_FPS_OVERLAY
    bool gyro = true;              // RBREW_GYRO
    int gyro_sensitivity = 100;    // percent
    bool gyro_invert_pitch = false;  // RBREW_GYRO_INVERT_PITCH
    int mouse_sensitivity = 100;   // percent
    int volume = 100;              // percent
    int keys[kActionCount];        // keyboard/mouse binding per action
    int pad[kActionCount];         // controller binding per action
    Values() { default_bindings(keys, pad); }
};

// Reads settings.ini once (main, before any subsystem reads the environment)
// and exports the start-only values (resolution) to the environment.
void load();
Values get();
// Stores and writes settings.ini.
void set(const Values& values);
// Resolution the renderer started with (the menu marks a pending change).
int active_resolution();

// Hot-path views of get().
inline std::atomic<bool> g_plaza_60fps{true};
inline std::atomic<bool> g_gyro{true};
inline std::atomic<bool> g_gyro_invert_pitch{false};
inline std::atomic<float> g_gyro_scale{1.0f};
inline std::atomic<float> g_mouse_scale{1.0f};
inline std::atomic<float> g_volume{1.0f};
inline std::atomic<int> g_keys[kActionCount];
inline std::atomic<int> g_pad[kActionCount];

}  // namespace rbrew_settings
