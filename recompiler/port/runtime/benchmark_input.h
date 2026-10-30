#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

// Opt-in deterministic input in presented-frame coordinates, independent of
// how often VPADRead is polled. Normal controller input never uses this path.
namespace rbrew_benchmark {
inline std::atomic<uint32_t> frame{0}, captured_frame{0};
inline std::atomic<bool> profiling{false};
// Guest draw calls and display-list calls: counted during the frame, then the
// last whole frame's total (a loading screen draws a handful, a scene hundreds).
inline std::atomic<uint32_t> guest_draws{0}, frame_draws{0};
struct Input {
    uint32_t frame = 0, buttons = 0;
    float lx = 0, ly = 0, rx = 0, ry = 0;
    unsigned touched = 0, x = 0, y = 0; // GamePad pixels, 854x480
};
inline std::vector<Input> read_inputs(std::istream& stream) {
    std::vector<Input> inputs;
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') continue;
        Input input;
        std::istringstream row(line);
        if (!(row >> std::dec >> input.frame >> std::hex >> input.buttons >> std::dec
                  >> input.lx >> input.ly >> input.rx >> input.ry
                  >> input.touched >> input.x >> input.y) ||
            (!inputs.empty() && input.frame <= inputs.back().frame) ||
            input.touched > 1 || input.x >= 854 || input.y >= 480 ||
            !(input.lx >= -1 && input.lx <= 1 && input.ly >= -1 && input.ly <= 1 &&
              input.rx >= -1 && input.rx <= 1 && input.ry >= -1 && input.ry <= 1)) {
            std::fprintf(stderr, "[benchmark] invalid input script; automation disabled\n");
            return {};
        }
        inputs.push_back(input);
    }
    return inputs;
}
inline const Input* input_at(const std::vector<Input>& inputs, uint32_t index) {
    const auto it = std::upper_bound(inputs.begin(), inputs.end(), index,
        [](uint32_t f, const Input& input) { return f < input.frame; });
    return it == inputs.begin() ? nullptr : &*std::prev(it);
}
inline bool scripted_input(Input& input) {
    static const char* path = std::getenv("RBREW_INPUT_SCRIPT");
    if (!path || !*path) return false;
    static std::vector<Input> inputs;
    static std::filesystem::file_time_type modified{};
    static uint32_t checked = UINT32_MAX;
    const uint32_t bucket = frame.load(std::memory_order_relaxed) / 60;
    if (checked != bucket) {
        checked = bucket;
        std::error_code error;
        const auto stamp = std::filesystem::last_write_time(path, error);
        if (!error && stamp != modified) {
            std::ifstream file(path);
            auto updated = read_inputs(file);
            if (!updated.empty()) { inputs = std::move(updated); modified = stamp; }
        }
    }
    if (inputs.empty()) return false;
    const auto* current = input_at(inputs, frame.load(std::memory_order_relaxed));
    input = current ? *current : Input{};
    return true;
}

// Session recording (RBREW_INPUT_RECORD=<file>) and replay
// (RBREW_INPUT_REPLAY=<file>): one pad per presented frame, including the
// controller gyro and the motion time step, so a replay turns the camera
// exactly as the player did. "@ frame kind id" lines mark when a scene became
// active (kind as active_scene_type, id the game's scene ID; old recordings
// have no id). "% frame n kind id" lines mark the n-th time a loading screen
// ended within that scene (the game drew almost nothing for a while, then a
// full frame: the title after boot, the hub after the plaza load). At each
// mark the replay waits while the game is still loading and skips ahead when
// it loads sooner, so load times do not shift later inputs.
struct Pad {
    uint32_t buttons = 0;
    float lx = 0, ly = 0, rx = 0, ry = 0;
    unsigned touched = 0, x = 0, y = 0;   // GamePad pixels, 854x480
    float gyro[3] = {};                   // rad/s, as SDL reports it
    float dt = 0;                         // motion seconds since the last poll
};
inline bool same_pad(const Pad& a, const Pad& b) {
    return a.buttons == b.buttons && a.lx == b.lx && a.ly == b.ly && a.rx == b.rx && a.ry == b.ry &&
           a.touched == b.touched && a.x == b.x && a.y == b.y && a.gyro[0] == b.gyro[0] &&
           a.gyro[1] == b.gyro[1] && a.gyro[2] == b.gyro[2] && a.dt == b.dt;
}
// The recorded game sees the printed values, so a replay is bit-identical.
inline float quantize(float v) { return float(std::round(double(v) * 10000.0) / 10000.0); }
inline void quantize(Pad& pad) {
    for (float* v : {&pad.lx, &pad.ly, &pad.rx, &pad.ry, &pad.gyro[0], &pad.gyro[1], &pad.gyro[2], &pad.dt})
        *v = quantize(*v);
}

// The live scene and how many loading screens ended in it, updated once per
// frame identically while recording and replaying.
struct LiveState {
    int kind = -1, id = -1;          // last stable scene
    uint32_t rises = 0, low = 0;     // loading screens ended; frames below the threshold
};
constexpr uint32_t kLoadingDraws = 200;   // loading screens draw 8-36, scenes thousands
inline const LiveState& live_state(int kind, int id) {
    static LiveState state;
    static uint32_t last = UINT32_MAX;
    const uint32_t now = frame.load(std::memory_order_relaxed);
    if (now == last) return state;
    last = now;
    if (kind >= 0 && (kind != state.kind || id != state.id)) {
        state.kind = kind; state.id = id; state.rises = 0;
    }
    if (frame_draws.load(std::memory_order_relaxed) < kLoadingDraws) ++state.low;
    else {
        if (state.low >= 10) ++state.rises;
        state.low = 0;
    }
    return state;
}

inline FILE* record_file() {
    static FILE* file = [] {
        const char* path = std::getenv("RBREW_INPUT_RECORD");
        const char* replay = std::getenv("RBREW_INPUT_REPLAY");
        if (!path || !*path || (replay && *replay)) return static_cast<FILE*>(nullptr);
        FILE* f = std::fopen(path, "w");
        if (f) std::fputs("# rbrew input recording 1: frame buttons lx ly rx ry touched x y gx gy gz dt | @ frame kind id | % frame n kind id\n", f);
        else std::fprintf(stderr, "[record] cannot write %s\n", path);
        return f;
    }();
    return file;
}
inline bool recording() { return record_file() != nullptr; }

// The first poll of a frame is recorded; later polls of that frame reuse it.
inline void record_input(Pad& pad, int scene, int scene_id) {
    FILE* f = record_file();
    if (!f) return;
    static uint32_t last_frame = UINT32_MAX;
    static Pad held, written;
    static bool any = false;
    static int last_scene = -1, last_id = -1;
    static uint32_t last_rises = 0;
    const uint32_t now = frame.load(std::memory_order_relaxed);
    if (now == last_frame) { pad = held; pad.dt = 0; return; }
    const LiveState& live = live_state(scene, scene_id);
    last_frame = now;
    quantize(pad);
    held = pad;
    if (scene >= 0 && (scene != last_scene || scene_id != last_id)) {
        last_scene = scene;
        last_id = scene_id;
        std::fprintf(f, "@ %u %d %d\n", now, scene, scene_id);
    }
    if (live.rises != last_rises) {
        last_rises = live.rises;
        if (live.rises) std::fprintf(f, "%% %u %u %d %d\n", now, live.rises, live.kind, live.id);
    }
    if (!any || !same_pad(pad, written)) {
        any = true;
        written = pad;
        std::fprintf(f, "%u %x %.4f %.4f %.4f %.4f %u %u %u %.4f %.4f %.4f %.4f\n", now, pad.buttons,
                     pad.lx, pad.ly, pad.rx, pad.ry, pad.touched, pad.x, pad.y,
                     pad.gyro[0], pad.gyro[1], pad.gyro[2], pad.dt);
    }
    std::fflush(f);
}

struct SceneMark {
    uint32_t frame = 0;
    int kind = -1, id = -1;   // id -1: match the kind only
    uint32_t rise = 0;        // 0: the scene became active; n: its n-th loading screen ended
    bool matches(int live_kind, int live_id) const { return live_kind == kind && (id < 0 || live_id == id); }
};
struct Replay {
    std::vector<std::pair<uint32_t, Pad>> pads;
    std::vector<SceneMark> scenes;
};
inline bool read_replay(std::istream& stream, Replay& replay) {
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        if (line[0] == '@') {
            char at_sign; SceneMark mark;
            if (!(row >> at_sign >> mark.frame >> mark.kind)) return false;
            if (!(row >> mark.id)) mark.id = -1;
            replay.scenes.push_back(mark);
            continue;
        }
        if (line[0] == '%') {
            char percent; SceneMark mark;
            if (!(row >> percent >> mark.frame >> mark.rise >> mark.kind >> mark.id) || !mark.rise) return false;
            replay.scenes.push_back(mark);
            continue;
        }
        uint32_t at; Pad pad;
        if (!(row >> std::dec >> at >> std::hex >> pad.buttons >> std::dec >> pad.lx >> pad.ly >> pad.rx >> pad.ry
                  >> pad.touched >> pad.x >> pad.y >> pad.gyro[0] >> pad.gyro[1] >> pad.gyro[2] >> pad.dt) ||
            (!replay.pads.empty() && at <= replay.pads.back().first) || pad.touched > 1 || pad.x >= 854 || pad.y >= 480)
            return false;
        replay.pads.emplace_back(at, pad);
    }
    return !replay.pads.empty();
}

inline std::atomic<bool> replay_running{false};
// Recorded input position, not elapsed live frames; loading pauses invalidate it.
inline std::atomic<uint32_t> replay_capture_frame{UINT32_MAX};
inline bool replaying() {
    static const bool on = [] { const char* p = std::getenv("RBREW_INPUT_REPLAY"); return p && *p; }();
    return on;
}
// Fills pad for this frame and returns true while the replay drives the
// game. Player input (a controller button, a stick pushed past half, or a key) hands control
// back to the player at once; so does the end of the recording.
inline bool replay_input(Pad& pad, int scene, int scene_id, bool physical_input) {
    if (!replaying()) return false;
    static Replay replay;
    static bool loaded = false, done = false;
    if (!loaded) {
        loaded = true;
        std::ifstream file(std::getenv("RBREW_INPUT_REPLAY"));
        if (!file || !read_replay(file, replay)) {
            std::fprintf(stderr, "[replay] cannot read %s; controller input only\n", std::getenv("RBREW_INPUT_REPLAY"));
            done = true;
        } else {
            std::fprintf(stderr, "[replay] %zu inputs, %zu scene/load marks\n", replay.pads.size(), replay.scenes.size());
            replay_running.store(true);
        }
    }
    if (done) return false;
    const uint32_t now = frame.load(std::memory_order_relaxed);
    const auto finish = [&](const char* why) {
        done = true;
        replay_running.store(false);
        replay_capture_frame.store(UINT32_MAX,std::memory_order_relaxed);
        std::fprintf(stderr, "[replay] %s at frame %u; the controller takes over\n", why, now);
        return false;
    };
    // Input held over several frames: a controller reports one stray full
    // deflection while it initializes, which must not end the replay.
    // RBREW_REPLAY_TAKEOVER=0 (benchmarks): live input never ends the replay.
    static const bool takeover = [] { const char* v = std::getenv("RBREW_REPLAY_TAKEOVER"); return !(v && v[0] == '0'); }();
    static uint32_t input_frames = 0, last_input_frame = UINT32_MAX;
    if (!physical_input || !takeover) input_frames = 0;
    else if (now != last_input_frame && ++input_frames >= 6) return finish("player input");
    last_input_frame = now;
    static int64_t offset = 0;   // live frame - recorded frame
    static size_t next_scene = 0;
    static SceneMark recorded_scene;
    static uint32_t last_frame = UINT32_MAX, hold_since = 0;
    static bool holding = false;
    static Pad held;
    if (now == last_frame) { pad = held; pad.dt = 0; return true; }
    last_frame = now;
    const LiveState& state = live_state(scene, scene_id);
    int64_t at = int64_t(now) - offset;
    bool hold = false;
    while (next_scene < replay.scenes.size()) {
        const SceneMark& mark = replay.scenes[next_scene];
        const bool live = mark.rise ? mark.matches(state.kind, state.id) && state.rises >= mark.rise
                                    : mark.matches(scene, scene_id);
        if (int64_t(mark.frame) <= at) {
            if (live) {
                if (!mark.rise) recorded_scene = mark;
                ++next_scene; holding = false; continue;
            }
            // Still loading: stay at the mark until the scene is up.
            offset = int64_t(now) - mark.frame;
            at = mark.frame;
            hold = true;
            if (!holding) { holding = true; hold_since = now; }
            else if (now - hold_since > 3600) return finish("the game did not reach the recorded scene or load within 60 s");
            break;
        }
        if (live && (mark.rise || !recorded_scene.matches(scene, scene_id))) {
            // Loaded sooner than in the recording: skip the rest of the load.
            offset = int64_t(now) - mark.frame;
            at = mark.frame;
            if (!mark.rise) recorded_scene = mark;
            ++next_scene;
            continue;
        }
        break;
    }
    if (!hold && next_scene == replay.scenes.size() && at > int64_t(replay.pads.back().first))
        return finish("recording finished");
    pad = Pad{};
    if (!hold) {
        const auto it = std::upper_bound(replay.pads.begin(), replay.pads.end(), at,
            [](int64_t f, const std::pair<uint32_t, Pad>& p) { return f < int64_t(p.first); });
        if (it != replay.pads.begin()) pad = std::prev(it)->second;
    }
    replay_capture_frame.store(hold?UINT32_MAX:uint32_t(at),std::memory_order_relaxed);
    held = pad;
    return true;
}
}
