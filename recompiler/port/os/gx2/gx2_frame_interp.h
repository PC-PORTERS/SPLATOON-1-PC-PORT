// gx2_frame_interp.h - 120 FPS by rendering an in-between frame.
//
// Splatoon steps its logic once per frame (player, physics and ink have no
// time step), so 120 real frames per second with normal game speed cannot
// come from running the game faster. Instead the game keeps 60 logic steps
// per second and the render thread draws every frame twice:
//
//   1. Frame N runs normally. Each draw's uniform data (camera, object and
//      bone matrices, ...) is recorded, and every posted GX2 command is kept.
//   2. At N's present the finished image is held, the game continues, and
//      the render thread replays N's commands with each draw's uniforms
//      blended halfway between N-1 and N (draws matched by program, vertex
//      buffer, count and occurrence). That frame is presented at once.
//   3. The held frame N is presented half a frame interval later.
//
// The replay never writes guest memory (queries, stream-out, readbacks are
// skipped), so game logic only ever sees frame N. RBREW_FPS120=interp turns
// it on outside the plaza (experimental).
#pragma once
#include <cstddef>
#include <cstdint>

struct Gx2AsyncWork;

namespace gx2interp {
// Game thread, per swap: interpolation wanted for the frames now being made.
void set_wanted(bool wanted);
bool mode_enabled();   // RBREW_FPS120=interp

// Render thread.
bool capturing();      // recording the current frame
bool replaying();      // running the in-between replay
void keep(Gx2AsyncWork* work);   // a work that ran; takes ownership
void draw_unit(uint64_t key, bool transparent = false);    // a draw (one instance-loop iteration) starts
bool replay_draw_known();        // replay: this draw was recorded in frame N
void add_busy(uint64_t ns);      // render-thread time spent on the frame's work
void record_uf(unsigned stage, const void* data, size_t size);
void record_bank(unsigned stage, unsigned bank, const void* data, size_t size);
// The uniform buffer (Vulkan handle, offset) a draw bound for stage/bank
// (255: the register block), for the Vulkan replay's substitution.
void note_ubo(unsigned stage, unsigned bank, uint64_t buffer, uint64_t offset);
void present_begin();            // gx2_render_present starts (not part of the frame)
void before_item();              // render thread: a queue item is about to run
// Replay: blended bytes for this draw, or false/nullptr (use guest memory).
bool replay_uf(unsigned stage, void* data, size_t size);
const uint8_t* replay_bank(unsigned stage, unsigned bank, size_t size);
// gx2_render_present: true when this frame is held for interpolation (the
// caller must not present it now).
bool frame_complete(unsigned scan_index);
// Render-thread loop: runs a pending replay, presents a held frame when due.
void service();
// Earliest time (steady clock ns) service() has work, or 0.
uint64_t deadline_ns();
}

// Implemented by the renderer (gx2_render_vk.cpp).
void gx2_render_interp_hold(unsigned scan_index);       // copy the scan image aside
void gx2_render_interp_present_scan(unsigned scan_index);
void gx2_render_interp_present_held();
void gx2_render_interp_keep_mid(unsigned scan_index);    // 180 FPS: keep the second in-between frame
void gx2_render_interp_present_mid();
