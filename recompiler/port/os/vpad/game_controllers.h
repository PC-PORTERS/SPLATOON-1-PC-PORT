#pragma once
#include "../os_common.h"
#if defined(HAVE_SDL2)
#include <SDL2/SDL.h>
#endif
#include <stdint.h>
#include <algorithm>
#include <cmath>

struct ControllerDevice {
#if defined(HAVE_SDL2)
    SDL_GameController* gc = nullptr;
    SDL_JoystickID instance_id = -1;
    char name[128] = {};
    char guid_str[64] = {};
    bool has_gyro = false;
    bool has_accel = false;
#endif
};

// Shape analog stick with deadzone and circular clamping
inline void shape_stick(float& x, float& y) {
    constexpr float kDeadzone = 0.10f; // responsive 10% deadzone
    float len = std::sqrt(x * x + y * y);
    if (len < kDeadzone) {
        x = y = 0.0f;
    } else {
        float scale = std::min(1.0f, (len - kDeadzone) / (1.0f - kDeadzone)) / len;
        x *= scale;
        y *= scale;
    }
}

// Big-endian float writer for guest memory
inline void write_guest_float(uint8_t* mem, uint32_t addr, float v) {
    union {
        float f;
        uint32_t u;
    } cvt;
    cvt.f = v;
    rbrew_write32(mem, addr, cvt.u);
}

// Diagnostic logger writing to stderr and controller_log.txt
void controller_log(const char* fmt, ...);

// Initialize controller subsystem hints, SDL subsystems, and mappings
void init_game_controllers();

// Get the controller assigned to player (0 = Player 1 GamePad, 1 = Player 2 Pro Controller)
// Fully lock-free, O(1) direct pointer lookup
ControllerDevice* get_controller(int player);

// Notification on SDL hotplug events
void on_controller_device_added(int which);
void on_controller_device_removed(int which);

// Infrequent periodic hotplug fallback check (called once every 5 seconds)
void check_controller_hotplug_periodic();

// Log live inputs (throttled to avoid any frame hitching)
void log_controller_input(int player, uint32_t buttons, float lx, float ly, float rx, float ry, bool has_gyro,
                          const float* gyro);

// Query total connected and recognized controllers
int get_connected_controller_count();
