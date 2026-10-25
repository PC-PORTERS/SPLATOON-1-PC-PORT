#include "port_settings.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace rbrew_settings {

// Defaults: the port's original layout. Controller buttons are mapped by
// position like on the GamePad (south = B, east = A, west = Y, north = X).
void default_bindings(int (&keys)[kActionCount], int (&pad)[kActionCount]) {
    for (int i = 0; i < kActionCount; ++i) keys[i] = pad[i] = kUnbound;
    // SDL scancodes
    keys[kActA] = 40 /*Return*/; keys[kActB] = 44 /*Space*/; keys[kActX] = 20 /*Q*/; keys[kActY] = 8 /*E*/;
    keys[kActR] = 21 /*R*/; keys[kActPlus] = 9 /*F*/; keys[kActMinus] = 10 /*G*/; keys[kActHome] = 11 /*H*/;
    keys[kActZR] = kMouseBase + 1 /*left*/; keys[kActZL] = kMouseBase + 3 /*right*/;
    keys[kActMoveUp] = 26 /*W*/; keys[kActMoveDown] = 22 /*S*/; keys[kActMoveLeft] = 4 /*A*/; keys[kActMoveRight] = 7 /*D*/;
    // SDL_GameControllerButton
    pad[kActB] = 0; pad[kActA] = 1; pad[kActY] = 2; pad[kActX] = 3;
    pad[kActMinus] = 4; pad[kActHome] = 5; pad[kActPlus] = 6; pad[kActStickL] = 7; pad[kActStickR] = 8;
    pad[kActL] = 9; pad[kActR] = 10; pad[kActUp] = 11; pad[kActDown] = 12; pad[kActLeft] = 13; pad[kActRight] = 14;
    pad[kActZL] = kPadTriggerLeft; pad[kActZR] = kPadTriggerRight;
}

namespace {
const char* const kActionKeys[kActionCount] = {"a", "b", "x", "y", "l", "r", "zl", "zr", "plus", "minus", "home",
    "up", "down", "left", "right", "stick_l", "stick_r", "move_up", "move_down", "move_left", "move_right"};
std::mutex s_lock;
Values s_values;
int s_active_resolution = k720p;
const char* const kResolutionNames[kResolutionCount] = {"720p", "1080p", "1440p", "native"};

std::filesystem::path file_path() {
#if defined(_WIN32)
    wchar_t module[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
    if (length && length < MAX_PATH) return std::filesystem::path(module).parent_path() / "settings.ini";
#endif
    return "settings.ini";
}
bool env_set(const char* name) { const char* v = std::getenv(name); return v && *v; }
bool env_on(const char* name) { const char* v = std::getenv(name); return v && v[0] == '1'; }
bool env_off(const char* name) { const char* v = std::getenv(name); return v && v[0] == '0'; }

void publish(const Values& v) {
    g_plaza_60fps.store(v.plaza_60fps, std::memory_order_relaxed);
    g_gyro.store(v.gyro, std::memory_order_relaxed);
    g_gyro_invert_pitch.store(v.gyro_invert_pitch, std::memory_order_relaxed);
    g_gyro_scale.store(v.gyro_sensitivity / 100.0f, std::memory_order_relaxed);
    g_mouse_scale.store(v.mouse_sensitivity / 100.0f, std::memory_order_relaxed);
    g_volume.store(v.volume / 100.0f, std::memory_order_relaxed);
    for (int i = 0; i < kActionCount; ++i) {
        g_keys[i].store(v.keys[i], std::memory_order_relaxed);
        g_pad[i].store(v.pad[i], std::memory_order_relaxed);
    }
}
void clamp(Values& v) {
    v.resolution = std::clamp(v.resolution, 0, kResolutionCount - 1);
    v.gyro_sensitivity = std::clamp(v.gyro_sensitivity, 25, 300);
    v.mouse_sensitivity = std::clamp(v.mouse_sensitivity, 25, 400);
    v.volume = std::clamp(v.volume, 0, 100);
    for (int i = 0; i < kActionCount; ++i) {
        const int key = v.keys[i], button = v.pad[i];
        if (key < kUnbound || (key >= 512 && key < kMouseBase) || key > kMouseBase + 5) v.keys[i] = kUnbound;
        if (button < kUnbound || button == kPadTouchpad || (button > 20 && button != kPadTriggerLeft && button != kPadTriggerRight) ||
            i >= kFirstMoveAction) v.pad[i] = kUnbound;
    }
}
void write(const Values& v) {
    std::ofstream out(file_path(), std::ios::trunc);
    if (!out) return;
    out << "# Splatoon PC port settings (ESC menu)\n"
        << "resolution=" << kResolutionNames[v.resolution] << "\n"
        << "plaza_60fps=" << v.plaza_60fps << "\n"
        << "fullscreen=" << v.fullscreen << "\n"
        << "fps_overlay=" << v.fps_overlay << "\n"
        << "gyro=" << v.gyro << "\n"
        << "gyro_sensitivity=" << v.gyro_sensitivity << "\n"
        << "gyro_invert_pitch=" << v.gyro_invert_pitch << "\n"
        << "mouse_sensitivity=" << v.mouse_sensitivity << "\n"
        << "volume=" << v.volume << "\n";
    for (int i = 0; i < kActionCount; ++i)
        out << "key_" << kActionKeys[i] << "=" << v.keys[i] << "\n" << "pad_" << kActionKeys[i] << "=" << v.pad[i] << "\n";
}
}  // namespace

void load() {
    std::lock_guard lock(s_lock);
    Values v;
    std::ifstream in(file_path());
    for (std::string line; in && std::getline(in, line);) {
        const size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        const int number = std::atoi(value.c_str());
        if (key == "resolution") {
            for (int i = 0; i < kResolutionCount; ++i) if (value == kResolutionNames[i]) v.resolution = i;
        }
        else if (key == "plaza_60fps") v.plaza_60fps = number != 0;
        else if (key == "fullscreen") v.fullscreen = number != 0;
        else if (key == "fps_overlay") v.fps_overlay = number != 0;
        else if (key == "gyro") v.gyro = number != 0;
        else if (key == "gyro_sensitivity") v.gyro_sensitivity = number;
        else if (key == "gyro_invert_pitch") v.gyro_invert_pitch = number != 0;
        else if (key == "mouse_sensitivity") v.mouse_sensitivity = number;
        else if (key == "volume") v.volume = number;
        else for (int i = 0; i < kActionCount; ++i) {
            if (key == std::string("key_") + kActionKeys[i]) v.keys[i] = number;
            if (key == std::string("pad_") + kActionKeys[i]) v.pad[i] = number;
        }
    }
    clamp(v);
    // Environment overrides (see header).
    if (env_set("RBREW_PLAZA_60FPS")) v.plaza_60fps = !env_off("RBREW_PLAZA_60FPS");
    if (env_set("RBREW_FPS_OVERLAY")) v.fps_overlay = !env_off("RBREW_FPS_OVERLAY");
    if (env_set("RBREW_GYRO")) v.gyro = !env_off("RBREW_GYRO");
    if (env_set("RBREW_GYRO_INVERT_PITCH")) v.gyro_invert_pitch = env_on("RBREW_GYRO_INVERT_PITCH");
    if (const char* r = std::getenv("RBREW_RENDER_RESOLUTION"); r && *r) {
        v.resolution = -1;
        for (int i = 0; i < kResolutionCount; ++i) if (!std::strcmp(r, kResolutionNames[i])) v.resolution = i;
        if (!std::strcmp(r, "1280x720")) v.resolution = k720p;
        if (!std::strcmp(r, "1920x1080")) v.resolution = k1080p;
        if (!std::strcmp(r, "2560x1440")) v.resolution = k1440p;
        s_active_resolution = v.resolution < 0 ? k720p : v.resolution;
        if (v.resolution < 0) v.resolution = k720p;   // custom WxH: the menu shows 720p
    } else {
        s_active_resolution = v.resolution;
#if defined(_WIN32)
        if (v.resolution != k720p) _putenv_s("RBREW_RENDER_RESOLUTION", kResolutionNames[v.resolution]);
#endif
    }
    s_values = v;
    publish(v);
}
Values get() { std::lock_guard lock(s_lock); return s_values; }
void set(const Values& values) {
    Values v = values;
    clamp(v);
    std::lock_guard lock(s_lock);
    s_values = v;
    publish(v);
    write(v);
}
int active_resolution() { std::lock_guard lock(s_lock); return s_active_resolution; }

}  // namespace rbrew_settings
