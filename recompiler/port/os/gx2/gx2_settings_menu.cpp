#include "gx2_settings_menu.h"
#include "../../runtime/port_settings.h"
#include "gx2_env_light.h"
#include "gx2_render.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>

namespace {
namespace rs = rbrew_settings;

enum Page { kMain, kKeyboard, kController, kLighting };
// Main page, in display order (the selected row is the enum value).
enum MainItem { kResume, kPreset, kGraphicsPage, kWeatherPage, kCameraPage, kToolsPage, kKeyboardPage, kControllerPage,
                kResolution, kPlaza60, kFps120, kGsync, kFullscreen, kFpsOverlay, kGyro, kGyroSensitivity, kGyroInvert,
                kVolume, kQuit, kMainCount };
// Options of the three beginner pages (Graphics, Weather & time, Camera & mouse).
enum LightItem { kPresetRow, kQuality, kWeather, kTime, kCycle, kRealTime, kDayLength, kClouds, kCloudAmount, kCumulusAmount, kCloudSpeed, kBigCloud,
                 kFog, kRays, kBloom, kLook, kSaturation, kContrast, kExposure, kGamma, kShadows, kHighlights, kSharpness, kClarity, kLookSlot, kSaveLook, kLoadLook, kResetPicture, kAntialiasing, kMotionBlur,
                 kMouseDirect, kMouseSens, kCameraFov, kCameraDistance, kCameraHeight, kFilmLook, kCinema, kCameraMotion,
                 kFlyCamera, kTakePhoto, kResetCamera, kFocusBlur, kVignette, kWhiteBalance, kSwayStrength, kWindDirection, kFlySpot, kSaveSpot, kGoSpot, kFlyZoom, kSpawnSpot, kSaveSpawn, kSpawnThere, kSpawnCamera, kLightBack };
enum Section { kSecGraphics, kSecWeather, kSecCamera, kSecTools };

const std::vector<LightItem>& section_items(Section section) {
    static const std::vector<LightItem> graphics = {kPresetRow, kQuality, kLook, kClouds, kBigCloud, kRays, kBloom, kFog, kAntialiasing,
                                                    kFilmLook, kSaturation, kContrast, kExposure, kGamma, kShadows, kHighlights, kSharpness, kClarity,
                                                    kWhiteBalance, kVignette, kLookSlot, kSaveLook, kLoadLook, kResetPicture, kLightBack};
    static const std::vector<LightItem> weather = {kWeather, kTime, kCycle, kRealTime, kDayLength, kCloudAmount, kCumulusAmount, kCloudSpeed, kWindDirection, kLightBack};
    static const std::vector<LightItem> camera = {kMouseDirect, kMouseSens, kCameraFov, kCameraDistance, kCameraHeight, kMotionBlur,
                                                  kCameraMotion, kSwayStrength, kResetCamera, kLightBack};
    static const std::vector<LightItem> tools = {kCinema, kTakePhoto, kFocusBlur, kCameraMotion, kSwayStrength, kFlyCamera, kFlyZoom, kFlySpot, kSaveSpot, kGoSpot,
                                                 kSpawnSpot, kSaveSpawn, kSpawnThere, kSpawnCamera, kLightBack};
    return section == kSecGraphics ? graphics : section == kSecWeather ? weather : section == kSecCamera ? camera : tools;
}

// Graphics presets: one switch for everything a beginner needs. The picture
// sliders (saturation, contrast, exposure, fog) are personal and stay.
enum Preset { kPresetOriginal, kPresetBalanced, kPresetRealistic, kPresetCinematic, kPresetCount };
const char* const kPresetLabels[kPresetCount] = {"Original (fastest)", "Balanced", "Realistic", "Cinematic (no HUD)"};
void apply_preset(rbrew_settings::Values& v, int preset) {
    if (preset == kPresetOriginal) { v.shader_quality = rbrew_settings::kShadersOff; return; }
    v.shader_quality = preset == kPresetBalanced ? rbrew_settings::kShadersBalanced : rbrew_settings::kShadersHigh;
    v.clouds = v.big_cloud = v.sun_rays = v.realistic_look = v.antialiasing = v.film_look = v.motion_blur = true;
    v.bloom = preset == kPresetCinematic ? 35 : 25;
    v.cinema_mode = v.camera_motion = preset == kPresetCinematic;
    v.focus_blur = preset == kPresetCinematic ? 40 : 0;
}
// -1: the options were changed one by one ("Custom").
int preset_of(const rbrew_settings::Values& v) {
    for (int preset = 0; preset < kPresetCount; ++preset) {
        auto t = v;
        apply_preset(t, preset);
        if (t.shader_quality == v.shader_quality && t.clouds == v.clouds && t.big_cloud == v.big_cloud && t.sun_rays == v.sun_rays &&
            t.realistic_look == v.realistic_look && t.antialiasing == v.antialiasing && t.film_look == v.film_look &&
            t.motion_blur == v.motion_blur && t.bloom == v.bloom && t.cinema_mode == v.cinema_mode && t.camera_motion == v.camera_motion &&
            (preset == kPresetOriginal || t.focus_blur == v.focus_blur))
            return preset;
    }
    return -1;
}

std::mutex s_lock;
Gx2MenuActions s_actions;
bool s_open = false;
bool s_block_until_released = false;
Page s_page = kMain;
Section s_section = kSecGraphics;
int s_spot = 0;              // fly spot chosen on the hacks page
int s_spawn = 0;             // spawn spot chosen on the hacks page
int s_look_slot=0;
int s_selected[4] = {};
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
int item_count(Page page) {
    return page == kMain ? kMainCount : page == kLighting ? int(section_items(s_section).size()) : action_count(page) + 2;
}
LightItem selected_light_item() {
    const auto& items = section_items(s_section);
    return items[std::clamp(s_selected[kLighting], 0, int(items.size()) - 1)];
}

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
        auto item = [&](MainItem id, const char* label, std::string value, bool adjustable, const char* help) {
            line(label, std::move(value), adjustable, s_selected[kMain] == id);
            if (s_selected[kMain] == id) view.help = help;
        };
        item(kResume, "Resume", "", false, "Back to the game.");
        const int preset = preset_of(v);
        item(kPreset, "Graphics preset (experimental)", preset < 0 ? "Custom" : kPresetLabels[preset], true,
             "One switch for the whole look. Fine-tune single options under Graphics.");
        item(kGraphicsPage, "Graphics (experimental)", ">", false, "New shaders: clouds, sun rays, colours, picture quality. Experimental.");
        item(kWeatherPage, "Weather & time (experimental)", ">", false, "Weather, time of day, day/night cycle and clouds. Experimental.");
        item(kCameraPage, "Camera & mouse", ">", false, "Mouse aiming, field of view, camera distance.");
        item(kToolsPage, "Cinema, photo & hacks (experimental)", ">", false, "Cinema mode (no HUD), photos, focus blur, camera sway, free fly camera.");
        item(kKeyboardPage, "Keyboard & mouse buttons", ">", false, "Change which key or mouse button does what.");
        item(kControllerPage, "Controller buttons", ">", false, "Change which controller button does what.");
        std::string resolution = kResolutionLabels[v.resolution];
        if (v.resolution != rs::active_resolution()) resolution += "  (after restart)";
        item(kResolution, "Render resolution", resolution, true, "Higher is sharper but needs a stronger graphics card.");
        item(kPlaza60, "Plaza 60 FPS", v.shader_quality?"On (lighting shaders)":on_off(v.plaza_60fps), !v.shader_quality,
             "The plaza runs at 60 instead of 30 FPS.");
        const char* rate = v.frame_rate == 30 ? "30 (low-end PC)" : v.frame_rate == 120 ? "120" : v.frame_rate == 180 ? "180 (plaza 120)" : "60";
        item(kFps120, "Frame rate", v.shader_quality ? "60 (lighting shaders)" : rate, !v.shader_quality,
             "120/180 add in-between frames; needs the graphics preset Original.");
        item(kGsync, "G-Sync / FreeSync", on_off(v.gsync), true, "For G-Sync/FreeSync monitors: frames are shown without waiting.");
        item(kFullscreen, "Fullscreen", on_off(s_actions.fullscreen ? s_actions.fullscreen() : v.fullscreen), true, "Window or fullscreen.");
        item(kFpsOverlay, "FPS overlay", on_off(v.fps_overlay), true, "Shows the frame rate in the corner (F3).");
        item(kGyro, "Controller gyro", on_off(v.gyro), true, "Aim by moving a PlayStation or Switch Pro controller.");
        item(kGyroSensitivity, "Gyro sensitivity", percent(v.gyro_sensitivity), true, "How far the camera turns when you move the controller.");
        item(kGyroInvert, "Invert gyro up/down", on_off(v.gyro_invert_pitch), true, "Flips up/down for controller motion aiming.");
        item(kVolume, "Volume", percent(v.volume), true, "Game volume.");
        item(kQuit, "Quit game", s_quit_armed ? "Press again to confirm" : "", false, "Closes the game.");
        return view;
    }
    if (s_page == kLighting) {
        static const char* const kTitles[] = {"Graphics (experimental)", "Weather & time (experimental)", "Camera & mouse", "Cinema, photo & hacks (experimental)"};
        view.title = kTitles[s_section];
        view.hint = v.shader_quality ? "Up/Down: select    Left/Right: change    Esc: back"
                                     : "Graphics preset Original: the new effects are off    Esc: back";
        const char* qualities[] = {"Off (original)", "Low", "Balanced", "High"};
        const char* weather[] = {"Clear", "Overcast", "Rain", "Thunderstorm", "After the rain", "Live (changes by itself)"};
        char clock[16]; std::snprintf(clock,sizeof(clock),"%02d:%02d",v.time_of_day/60,v.time_of_day%60);
        const LightItem selected = selected_light_item();
        for (const LightItem id : section_items(s_section)) {
            std::string label, value;
            const char* help = "";
            switch (id) {
            case kPresetRow: { const int preset = preset_of(v); label = "Preset"; value = preset < 0 ? "Custom" : kPresetLabels[preset];
                help = "One switch for everything below. Changing an option makes it Custom."; break; }
            case kQuality: label = "Shader quality"; value = qualities[v.shader_quality];
                help = "Higher: finer clouds and light. Off: the original game, unlocks 30/120/180 FPS."; break;
            case kLook: label = "Colours"; value = v.realistic_look ? "Realistic" : "Original";
                help = "Realistic: colours and contrast of a modern game."; break;
            case kClouds: label = "Volumetric clouds"; value = on_off(v.clouds); help = "Real 3D clouds that drift with the wind."; break;
            case kBigCloud: label = "Big cumulus clouds"; value = on_off(v.big_cloud);
                help = "Dense 3D cloud banks, with sunlit edges and shaded interiors."; break;
            case kRays: label = "Sun rays"; value = on_off(v.sun_rays); help = "Light shafts through clouds and around buildings."; break;
            case kBloom: label = "Glow (bloom)"; value = percent(v.bloom); help = "Glow around the sun, lamps and bright signs."; break;
            case kFog: label = "Ground fog"; value = percent(v.fog); help = "Fog over the ground. 0 % = clear air."; break;
            case kAntialiasing: label = "Edge smoothing"; value = on_off(v.antialiasing); help = "Smooths jagged edges (FXAA + TAA)."; break;
            case kFilmLook: label = "Film look"; value = on_off(v.film_look); help = "Fine film grain and lens effects at the picture edges."; break;
            case kSaturation: label = "Saturation"; value = percent(v.saturation); help = "Colour strength. 100 % = normal."; break;
            case kContrast: label = "Contrast"; value = percent(v.contrast); help = "Difference between dark and bright. 100 % = normal."; break;
            case kExposure: { char ev[16]; std::snprintf(ev,sizeof(ev),"%+.2f EV",v.exposure/4.0); label = "Brightness"; value = ev;
                help = "Overall brightness. 0 = normal."; break; }
            case kGamma: label="Gamma / midtones";value=percent(v.gamma);help="100 % = neutral. Higher brightens midtones.";break;
            case kShadows: label="Shadow brightness";value=std::to_string(v.shadow_lift);help="Lift or deepen dark areas. 0 = neutral.";break;
            case kHighlights: label="Highlight softness";value=percent(v.highlight_softness);help="Softens bright highlights while keeping their colour.";break;
            case kSharpness: label="Sharpening";value=percent(v.sharpness);help="0 = off; 100 % = normal; higher adds stronger edges.";break;
            case kClarity: label="Surface clarity";value=percent(v.clarity);help="Fine local contrast on surfaces. 100 % = normal.";break;
            case kLookSlot: label="Custom look slot";value=std::to_string(s_look_slot+1)+(v.looks[s_look_slot].saved?" (saved)":" (empty)");help="Choose one of three personal picture presets.";break;
            case kSaveLook: label="Save current look";help="Save these picture controls in the selected slot (Enter).";break;
            case kLoadLook: label="Load saved look";value=v.looks[s_look_slot].saved?"Load":"Empty";help="Recall the selected picture preset (Enter).";break;
            case kResetPicture: label = "Reset colours and brightness"; help = "Reset picture controls; saved looks stay (Enter)."; break;
            case kWhiteBalance: label = "Colour warmth"; value = v.white_balance == 0 ? std::string("Neutral") : (v.white_balance > 0 ? "Warmer +" : "Cooler ") + std::to_string(v.white_balance);
                help = "Warmer (golden) or cooler (blue) picture, like a camera's white balance."; break;
            case kVignette: label = "Vignette"; value = percent(v.vignette); help = "Darker picture corners, like a camera lens. 0 % = off."; break;
            case kFocusBlur: label = "Focus blur"; value = percent(v.focus_blur);
                help = "Sharp on what you look at, soft in front and behind (photos, cinema). 0 % = off."; break;
            case kWindDirection: { static const char* const kDirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
                label = "Wind direction"; value = std::to_string(v.wind_direction) + " deg (" + kDirs[((v.wind_direction + 22) % 360) / 45] + ")";
                help = "Where the clouds drift to."; break; }
            case kSpawnSpot: label = "Spawn spot"; value = "Spot " + std::to_string(s_spawn + 1) + (gx2env::spawn_saved(s_spawn) ? " (saved)" : " (empty)");
                help = "Three places for your character. Choose one, then save or spawn there."; break;
            case kSaveSpawn: label = "Save where the character stands"; help = "Remembers your character's position (plaza)."; break;
            case kSpawnThere: label = "Spawn the character at the spot"; help = "Puts your character at the saved place."; break;
            case kSpawnCamera: label = "Spawn the character at the camera"; help = "Puts your character in front of the (free) camera; it drops to the ground."; break;
            case kFlyZoom: { char z[24]; std::snprintf(z, sizeof(z), "%.0f", gx2env::fly_fov()); label = "Free camera field of view"; value = z;
                help = "Zoom of the free camera. Also mouse wheel or Q/E while flying."; break; }
            case kFlySpot: label = "Fly spot"; value = "Spot " + std::to_string(s_spot + 1) + (gx2env::fly_spot_saved(s_spot) ? " (saved)" : " (empty)");
                help = "Three places to remember. Choose one, then save or fly to it."; break;
            case kSaveSpot: label = "Save this view as the spot"; help = "Remembers where the camera is now (plaza)."; break;
            case kGoSpot: label = "Fly to the spot"; help = "Turns the fly camera on at the saved place. F7 returns to the game camera."; break;
            case kSwayStrength: label = "Wiggle strength"; value = percent(v.sway_strength); help = "How much the cinema camera sways."; break;
            case kWeather: label = "Weather"; value = weather[v.weather]; help = "Live: the weather changes by itself, with storms and sunshine."; break;
            case kTime: label = "Time of day"; value = clock; help = "Where the day starts (or the fixed time when the cycle is off)."; break;
            case kCycle: label = "Day / night cycle"; value = on_off(v.day_cycle); help = "The sun moves: sunrise, sunset and night follow."; break;
            case kRealTime: label = "Use the PC clock"; value = on_off(v.real_time); help = "The time of day follows your PC's clock."; break;
            case kDayLength: label = "Day length"; value = std::to_string(v.day_length) + " min";
                help = "How many real minutes a whole game day takes."; break;
            case kCloudAmount: label = "Cloud amount"; value = v.cloud_amount < 0 ? std::string("Auto (weather)") : percent(v.cloud_amount);
                help = "Auto follows the weather; or choose how cloudy the sky is."; break;
            case kCumulusAmount: label = "Dense cumulus coverage"; value = v.cumulus_amount < 0 ? std::string("Auto (weather)") : percent(v.cumulus_amount);
                help = "Coverage of tall cloud banks. 0 % = off; Auto is fuller after rain."; break;
            case kCloudSpeed: label = "Cloud speed"; value = percent(v.cloud_speed); help = "Drift and evolution of all clouds. 0 % = frozen clouds."; break;
            case kMouseDirect: label = "Mouse aiming"; value = v.mouse_direct ? "Direct (PC)" : "Classic (stick)";
                help = "Direct turns 1:1 like a PC game. Needs Motion Controls On in the game's options."; break;
            case kMouseSens: label = "Mouse sensitivity"; value = percent(v.mouse_sensitivity); help = "How far the camera turns per mouse movement."; break;
            case kCameraFov: label = "Field of view"; value = std::to_string(v.camera_fov) + (v.camera_fov == 55 ? " (original)" : "");
                help = "Wider shows more of the plaza. 55 = the game's own camera."; break;
            case kCameraDistance: label = "Camera distance"; value = percent(v.camera_distance) + (v.camera_distance == 100 ? " (original)" : "");
                help = "How far behind your character the camera stays (plaza)."; break;
            case kCameraHeight: label = "Camera height"; value = std::to_string(v.camera_height) + (v.camera_height == 0 ? " (original)" : ""); help = "Raises the camera above the game's own (plaza)."; break;
            case kMotionBlur: label = "Motion blur (experimental)"; value = on_off(v.motion_blur); help = "Blur when the camera turns quickly."; break;
            case kCinema: label = "Cinema mode"; value = v.cinema_mode ? "On (no HUD)" : "Off";
                help = "No HUD, free camera: mouse looks, WASD moves, Space/Ctrl up/down, Shift fast, F7 back."; break;
            case kCameraMotion: label = "Camera wiggle"; value = on_off(v.camera_motion);
                help = "The camera moves gently like a hand-held camera (also in normal play)."; break;
            case kFlyCamera: label = "Free fly camera (F7)"; value = on_off(gx2env::fly_camera());
                help = "Plaza. Mouse looks, WASD moves, Space/Ctrl up/down, Shift fast. The character waits."; break;
            case kTakePhoto: label = "Take a photo (F12)"; value = "";
                help = "Saves the picture without menu and FPS display to the Screenshots folder."; break;
            case kResetCamera: label = "Reset camera to the original"; value = "";
                help = "Field of view 55, distance 100 %, height 0: the game's own camera."; break;
            case kLightBack: label = "Back"; break;
            }
            const bool adjustable = id != kLightBack && id != kTakePhoto && id != kResetPicture && id != kSaveLook && id != kLoadLook && id != kResetCamera && id != kSaveSpot && id != kGoSpot &&
                                    id != kSaveSpawn && id != kSpawnThere && id != kSpawnCamera;
            line(label, std::move(value), adjustable, id == selected);
            if (id == selected) view.help = help;
        }
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
void touch() {
    ++s_revision; s_view = build_view();
    // Long pages show a window around the selected row (a panel taller than
    // the screen would not be drawn at all).
    constexpr size_t kRows = 16;
    if (s_view.lines.size() > kRows) {
        size_t sel = 0;
        for (size_t i = 0; i < s_view.lines.size(); ++i) if (s_view.lines[i].selected) sel = i;
        const size_t first = std::min(sel > kRows / 2 ? sel - kRows / 2 : 0, s_view.lines.size() - kRows);
        s_view.lines = std::vector<Gx2MenuLine>(s_view.lines.begin() + first, s_view.lines.begin() + first + kRows);
    }
}

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
    case kPlaza60: if (v.shader_quality) return; v.plaza_60fps = !v.plaza_60fps; break;
    case kFps120: {   // 30 -> 60 -> 120 -> 180 (game speed stays the same in all)
        if (v.shader_quality) return;
        static const int kRates[] = {30, 60, 120, 180};
        int i = 1;
        for (int k = 0; k < 4; ++k) if (kRates[k] == v.frame_rate) i = k;
        v.frame_rate = kRates[(i + (direction < 0 ? 3 : 1)) % 4];
        v.fps120 = v.frame_rate >= 120;
        break;
    }
    case kGsync: v.gsync = !v.gsync; break;   // the render thread rebuilds the swapchain
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
    case kVolume: step(v.volume, 10, 0, 100); break;
    case kPreset: {
        const int now = preset_of(v), d = direction < 0 ? -1 : 1;
        apply_preset(v, now < 0 ? (d > 0 ? kPresetBalanced : kPresetRealistic) : (now + d + kPresetCount) % kPresetCount);
        break;
    }
    case kGraphicsPage: if (!direction) { s_section = kSecGraphics; set_page(kLighting); } return;
    case kWeatherPage: if (!direction) { s_section = kSecWeather; set_page(kLighting); } return;
    case kCameraPage: if (!direction) { s_section = kSecCamera; set_page(kLighting); } return;
    case kToolsPage: if (!direction) { s_section = kSecTools; set_page(kLighting); gx2env::want_spawn(); } return;
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
void change_lighting(int direction) {
    auto v=rs::get();
    const int d=direction<0?-1:1;
    switch (selected_light_item()) {
    case kPresetRow: {
        const int now = preset_of(v);
        apply_preset(v, now < 0 ? (d > 0 ? kPresetBalanced : kPresetRealistic) : (now + d + kPresetCount) % kPresetCount);
        break;
    }
    case kResetPicture: if (direction) return; v.saturation=100;v.contrast=100;v.exposure=0;v.fog=0;v.white_balance=0;v.vignette=0;
        v.gamma=100;v.shadow_lift=0;v.highlight_softness=0;v.sharpness=100;v.clarity=100;break;
    case kLookSlot: s_look_slot=(s_look_slot+d+3)%3;touch();return;
    case kSaveLook: if(direction)return;rs::save_look(v,s_look_slot);break;
    case kLoadLook: if(direction)return;rs::load_look(v,s_look_slot);break;
    case kWhiteBalance: v.white_balance = std::clamp(v.white_balance + d * 5, -50, 50); break;
    case kVignette: v.vignette = std::clamp(v.vignette + d * 10, 0, 100); break;
    case kFocusBlur: v.focus_blur = std::clamp(v.focus_blur + d * 10, 0, 100); break;
    case kWindDirection: v.wind_direction = (v.wind_direction + d * 15 + 360) % 360; break;
    case kSpawnSpot: s_spawn = (s_spawn + d + 3) % 3; touch(); return;
    case kSaveSpawn: if (direction) return; gx2env::save_spawn(s_spawn); touch(); return;
    case kSpawnThere: if (direction) return; if (gx2env::spawn_at(s_spawn)) set_open(false); else touch(); return;
    case kSpawnCamera: if (direction) return; if (gx2env::spawn_at_camera()) set_open(false); else touch(); return;
    case kFlyZoom: gx2env::set_fly_fov(gx2env::fly_fov() + d * 5.f); touch(); return;
    case kFlySpot: s_spot = (s_spot + d + 3) % 3; touch(); return;
    case kSaveSpot: if (direction) return; gx2env::save_fly_spot(s_spot); touch(); return;
    case kGoSpot: if (direction) return; if (gx2env::go_fly_spot(s_spot)) set_open(false); else touch(); return;
    case kSwayStrength: v.sway_strength = std::clamp(v.sway_strength + d * 25, 25, 300); break;
    case kFlyCamera: gx2env::set_fly_camera(!gx2env::fly_camera()); touch(); return;
    case kTakePhoto: if (direction) return; set_open(false); gx2_render_request_photo(); return;
    case kResetCamera: if (direction) return; v.camera_fov = 55; v.camera_distance = 100; v.camera_height = 0; break;
    case kCloudSpeed: v.cloud_speed = std::clamp(v.cloud_speed + d * 25, 0, 400); break;
    case kMouseDirect: v.mouse_direct = !v.mouse_direct; break;
    case kMouseSens: v.mouse_sensitivity = std::clamp(v.mouse_sensitivity + d * 25, 25, 400); break;
    case kQuality: v.shader_quality=(v.shader_quality+d+rs::kShaderQualityCount)%rs::kShaderQualityCount; break;
    case kWeather: v.weather=(v.weather+d+rs::kWeatherCount)%rs::kWeatherCount; break;
    case kTime: v.time_of_day=(v.time_of_day+d*15+1440)%1440; break;
    case kCycle: v.day_cycle=!v.day_cycle; break;
    case kDayLength: v.day_length=std::clamp(v.day_length+d*5,5,120); break;
    case kClouds: v.clouds=!v.clouds; break;
    case kCloudAmount: v.cloud_amount=v.cloud_amount<0?(d>0?0:100):std::clamp(v.cloud_amount+d*10,-1,100); if(v.cloud_amount>100) v.cloud_amount=-1; break;
    case kCumulusAmount: v.cumulus_amount=v.cumulus_amount<0?(d>0?0:100):std::clamp(v.cumulus_amount+d*10,-1,100); break;
    case kBigCloud: v.big_cloud=!v.big_cloud; break;
    case kRealTime: v.real_time=!v.real_time; break;
    case kFog: v.fog=std::clamp(v.fog+d*10,0,100); break;
    case kSaturation: v.saturation=std::clamp(v.saturation+d*10,0,200); break;
    case kContrast: v.contrast=std::clamp(v.contrast+d*5,50,150); break;
    case kExposure: v.exposure=std::clamp(v.exposure+d,-8,8); break;
    case kGamma: v.gamma=std::clamp(v.gamma+d*5,50,150);break;
    case kShadows: v.shadow_lift=std::clamp(v.shadow_lift+d,-25,25);break;
    case kHighlights: v.highlight_softness=std::clamp(v.highlight_softness+d*5,0,100);break;
    case kSharpness: v.sharpness=std::clamp(v.sharpness+d*10,0,200);break;
    case kClarity: v.clarity=std::clamp(v.clarity+d*10,0,200);break;
    case kRays: v.sun_rays=!v.sun_rays; break;
    case kBloom: v.bloom=std::clamp(v.bloom+d*5,0,100); break;
    case kLook: v.realistic_look=!v.realistic_look; break;
    case kAntialiasing: v.antialiasing=!v.antialiasing; break;
    case kMotionBlur: v.motion_blur=!v.motion_blur; break;
    case kCameraFov: v.camera_fov=std::clamp(v.camera_fov+d,40,90); break;
    case kCameraDistance: v.camera_distance=std::clamp(v.camera_distance+d*10,50,250); break;
    case kCameraHeight: v.camera_height=std::clamp(v.camera_height+d*2,-20,60); break;
    case kFilmLook: v.film_look=!v.film_look; break;
    case kCinema: v.cinema_mode=!v.cinema_mode; gx2env::set_fly_camera(v.cinema_mode); break;   // cinema = free camera for photos
    case kCameraMotion: v.camera_motion=!v.camera_motion; break;
    case kLightBack: if (!direction) set_page(kMain); return;
    }
    rs::set(v); touch();
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
    case Key::Left: if (main) change(-1); else if (s_page==kLighting) change_lighting(-1); break;
    case Key::Right: if (main) change(1); else if (s_page==kLighting) change_lighting(1); break;
    case Key::Confirm: if (main) change(0); else if (s_page==kLighting) change_lighting(0); else confirm_controls(); break;
    case Key::Back: if (main) set_open(false); else set_page(kMain); break;
    case Key::Clear:
        if ((s_page==kKeyboard || s_page==kController) && s_selected[s_page] < action_count(s_page)) bind(rs::kUnbound);
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
    // The press that closed the menu must not reach the game, but the block
    // never outlasts half a second (a button held on purpose, or one that
    // reads as pressed, must not leave the pad dead).
    static std::chrono::steady_clock::time_point s_closed_at;
    static bool s_was_open = false;
    if (s_open) { s_was_open = true; return true; }
    if (s_was_open) { s_closed_at = std::chrono::steady_clock::now(); s_was_open = false; }
    if (s_block_until_released && (!any_input_held || std::chrono::steady_clock::now() - s_closed_at > std::chrono::milliseconds(500)))
        s_block_until_released = false;
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
