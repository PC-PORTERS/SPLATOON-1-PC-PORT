#include "game_controllers.h"
#include "../../runtime/splatoon_diagnostics.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <vector>
#include <chrono>
#include <mutex>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(HAVE_SDL2)

static ControllerDevice s_devices[2]; // 0: P1 (GamePad), 1: P2 (Pro Controller)
static std::mutex s_controller_mutex;
static FILE* s_log_file = nullptr;
static bool s_initialized = false;
static int s_connected_count = 0;

void controller_log(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto timer = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &timer);
#else
    localtime_r(&timer, &tm_buf);
#endif

    char formatted[1200];
    snprintf(formatted, sizeof(formatted), "[%02d:%02d:%02d.%03d] [controller] %s",
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, (int)ms.count(), buffer);

    fputs(formatted, stderr);

    if (!s_log_file) {
        char log_path[512] = {};
        char* base_path = SDL_GetBasePath();
        if (base_path) {
            snprintf(log_path, sizeof(log_path), "%scontroller_log.txt", base_path);
            SDL_free(base_path);
        } else {
            snprintf(log_path, sizeof(log_path), "controller_log.txt");
        }
        // One log per launch: appending made the file grow without bound.
        s_log_file = fopen(log_path, "w");
    }
    if (s_log_file) {
        fputs(formatted, s_log_file);
        fflush(s_log_file);   // rare messages; keep them if the game crashes
    }
}

static void update_controllers_locked() {
    // 1. Check currently assigned controllers and close if disconnected
    for (int p = 0; p < 2; ++p) {
        if (s_devices[p].gc) {
            if (!SDL_GameControllerGetAttached(s_devices[p].gc)) {
                controller_log("Controller detached: Player %d [%s] (instance %d)\n",
                               p + 1, s_devices[p].name, s_devices[p].instance_id);
                SplatoonDiagnostics::RecordEvent(SplatoonDiagnostics::EventCategory::Input,
                                                SplatoonDiagnostics::EventSeverity::Info,
                                                s_devices[p].instance_id, "Controller detached");
                SDL_GameControllerClose(s_devices[p].gc);
                s_devices[p] = {};
            }
        }
    }

    // 2. Discover and open all available game controllers
    const int num_joysticks = SDL_NumJoysticks();
    std::vector<ControllerDevice> available_controllers;

    for (int i = 0; i < num_joysticks; ++i) {
        char guid_str[64] = {};
        SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(i);
        SDL_JoystickGetGUIDString(guid, guid_str, sizeof(guid_str));
        const char* joy_name = SDL_JoystickNameForIndex(i);
        const bool is_gc = SDL_IsGameController(i) != SDL_FALSE;

        SDL_JoystickID jid = SDL_JoystickGetDeviceInstanceID(i);

        // Check if already open in slot 0 or slot 1
        ControllerDevice existing{};
        bool is_open = false;
        for (int p = 0; p < 2; ++p) {
            if (s_devices[p].gc && s_devices[p].instance_id == jid) {
                existing = s_devices[p];
                is_open = true;
                break;
            }
        }

        if (is_open) {
            available_controllers.push_back(existing);
        } else if (is_gc) {
            SDL_GameController* gc = SDL_GameControllerOpen(i);
            if (gc) {
                ControllerDevice dev{};
                dev.gc = gc;
                dev.instance_id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
                const char* gc_name = SDL_GameControllerName(gc);
                std::snprintf(dev.name, sizeof(dev.name), "%s",
                              gc_name ? gc_name : (joy_name ? joy_name : "Unknown"));
                std::snprintf(dev.guid_str, sizeof(dev.guid_str), "%s", guid_str);

                // Check and activate sensors (gyro & accelerometer)
                if (SDL_GameControllerHasSensor(gc, SDL_SENSOR_GYRO)) {
                    SDL_GameControllerSetSensorEnabled(gc, SDL_SENSOR_GYRO, SDL_TRUE);
                    dev.has_gyro = true;
                }
                if (SDL_GameControllerHasSensor(gc, SDL_SENSOR_ACCEL)) {
                    SDL_GameControllerSetSensorEnabled(gc, SDL_SENSOR_ACCEL, SDL_TRUE);
                    dev.has_accel = true;
                }

                controller_log("Device #%d: Opened GameController [%s]\n"
                               "    GUID: %s\n"
                               "    Instance ID: %d\n"
                               "    Sensors: Gyro=%s, Accel=%s\n",
                               i, dev.name, dev.guid_str, dev.instance_id,
                               dev.has_gyro ? "ENABLED" : "NO",
                               dev.has_accel ? "ENABLED" : "NO");

                SplatoonDiagnostics::RecordEvent(SplatoonDiagnostics::EventCategory::Input,
                                                SplatoonDiagnostics::EventSeverity::Info,
                                                dev.instance_id, dev.name);

                available_controllers.push_back(dev);
            }
        }
    }

    s_connected_count = (int)available_controllers.size();

    // 3. Strict Player Allocation Rule:
    // Player 1 (GamePad) ALWAYS has absolute priority.
    // Controller 0 is ALWAYS allocated to Player 1.
    // Player 2 (Battle Dojo Pro Controller) ONLY gets Controller 1 if >= 2 controllers are connected.
    if (available_controllers.size() >= 2) {
        if (s_devices[0].gc != available_controllers[0].gc) {
            s_devices[0] = available_controllers[0];
            controller_log("Assigned Player 1 (GamePad) -> [%s] (ID %d)\n",
                           s_devices[0].name, s_devices[0].instance_id);
        }
        if (s_devices[1].gc != available_controllers[1].gc) {
            s_devices[1] = available_controllers[1];
            controller_log("Assigned Player 2 (Battle Dojo) -> [%s] (ID %d)\n",
                           s_devices[1].name, s_devices[1].instance_id);
        }
    } else if (available_controllers.size() == 1) {
        if (s_devices[0].gc != available_controllers[0].gc) {
            s_devices[0] = available_controllers[0];
            controller_log("Assigned Player 1 (GamePad) -> [%s] (ID %d)\n",
                           s_devices[0].name, s_devices[0].instance_id);
        }
        if (s_devices[1].gc != nullptr) {
            s_devices[1] = {};
        }
    } else {
        s_devices[0] = {};
        s_devices[1] = {};
    }
    // Controllers opened above but not assigned to a player (a third pad)
    // were reopened on every scan without being closed.
    for (const auto& dev : available_controllers)
        if (dev.gc != s_devices[0].gc && dev.gc != s_devices[1].gc) SDL_GameControllerClose(dev.gc);
}

void init_game_controllers() {
    std::lock_guard<std::mutex> lock(s_controller_mutex);
    if (s_initialized) return;
    s_initialized = true;

    controller_log("============================================================\n");
    controller_log("Initializing Game Controller Subsystem (SDL2 backend)\n");
    controller_log("============================================================\n");

    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_JOY_CONS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");

    // Load external database if present
    char db_path[512] = {};
    char* base_path = SDL_GetBasePath();
    if (base_path) {
        snprintf(db_path, sizeof(db_path), "%sgamecontrollerdb.txt", base_path);
        SDL_free(base_path);
    } else {
        snprintf(db_path, sizeof(db_path), "gamecontrollerdb.txt");
    }

    FILE* db_file = fopen(db_path, "r");
    if (db_file) {
        fclose(db_file);
        SDL_GameControllerAddMappingsFromFile(db_path);
    }

    // Windows fallback mappings for PS4 DualShock 4 and PS5 DualSense
    static const char* const kFallbackMappings[] = {
        "030000004c050000c405000000007701,PS4 Controller,a:b1,b:b2,back:b8,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b4,leftstick:b10,lefttrigger:a3,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b11,righttrigger:a4,rightx:a2,righty:a5,start:b9,x:b0,y:b3,platform:Windows,",
        "030000004c050000cc09000000007701,PS4 Controller,a:b1,b:b2,back:b8,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b4,leftstick:b10,lefttrigger:a3,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b11,righttrigger:a4,rightx:a2,righty:a5,start:b9,x:b0,y:b3,platform:Windows,",
        "030000004c050000c405000000000000,PS4 Controller,a:b1,b:b2,back:b8,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b4,leftstick:b10,lefttrigger:a3,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b11,righttrigger:a4,rightx:a2,righty:a5,start:b9,x:b0,y:b3,platform:Windows,",
        "030000004c050000cc09000000000000,PS4 Controller,a:b1,b:b2,back:b8,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b4,leftstick:b10,lefttrigger:a3,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b11,righttrigger:a4,rightx:a2,righty:a5,start:b9,x:b0,y:b3,platform:Windows,",
        "4c05c405000000000000504944564944,PS4 Controller (DInput),a:b1,b:b2,back:b8,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b4,leftstick:b10,lefttrigger:a3,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b11,righttrigger:a4,rightx:a2,righty:a5,start:b9,x:b0,y:b3,platform:Windows,",
        "4c05cc09000000000000504944564944,PS4 Controller (DInput),a:b1,b:b2,back:b8,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b4,leftstick:b10,lefttrigger:a3,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b11,righttrigger:a4,rightx:a2,righty:a5,start:b9,x:b0,y:b3,platform:Windows,",
        "030000004c050000e60c000000007701,PS5 DualSense,a:b1,b:b2,back:b8,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b4,leftstick:b10,lefttrigger:a3,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b11,righttrigger:a4,rightx:a2,righty:a5,start:b9,x:b0,y:b3,platform:Windows,",
        "4c05e60c000000000000504944564944,PS5 DualSense (DInput),a:b1,b:b2,back:b8,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b4,leftstick:b10,lefttrigger:a3,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b11,righttrigger:a4,rightx:a2,righty:a5,start:b9,x:b0,y:b3,platform:Windows,",
    };
    for (const char* mapping : kFallbackMappings) {
        SDL_GameControllerAddMapping(mapping);
    }

    update_controllers_locked();
}

// O(1) lock-free, zero-allocation controller lookup
ControllerDevice* get_controller(int player) {
    if (player < 0 || player >= 2) return nullptr;
    return s_devices[player].gc ? &s_devices[player] : nullptr;
}

void on_controller_device_added(int which) {
    std::lock_guard<std::mutex> lock(s_controller_mutex);
    update_controllers_locked();
}

void on_controller_device_removed(int which) {
    std::lock_guard<std::mutex> lock(s_controller_mutex);
    update_controllers_locked();
}

void check_controller_hotplug_periodic() {
    static auto s_last_poll = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (now - s_last_poll > std::chrono::seconds(5)) {
        s_last_poll = now;
        std::lock_guard<std::mutex> lock(s_controller_mutex);
        update_controllers_locked();
    }
}

int get_connected_controller_count() {
    return s_connected_count;
}

// ---------------------------------------------------------------------------
// Live Input Telemetry (Zero Disk I/O in Hot Path)
// ---------------------------------------------------------------------------
static uint32_t s_last_buttons[2] = {0, 0};

void log_controller_input(int player, uint32_t buttons, float, float, float, float, bool, const float*) {
    if (player < 0 || player >= 2) return;
    if (buttons != s_last_buttons[player]) {
        s_last_buttons[player] = buttons;
    }
}

#else

void controller_log(const char*, ...) {}
void init_game_controllers() {}
ControllerDevice* get_controller(int) { return nullptr; }
void on_controller_device_added(int) {}
void on_controller_device_removed(int) {}
void check_controller_hotplug_periodic() {}
int get_connected_controller_count() { return 0; }
void log_controller_input(int, uint32_t, float, float, float, float, bool, const float*) {}

#endif
