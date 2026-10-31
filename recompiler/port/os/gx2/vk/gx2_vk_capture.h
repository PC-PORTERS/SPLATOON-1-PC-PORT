// gx2_vk_capture.h - a frame's Vulkan commands, captured for the 120 FPS
// in-between frame (gx2_frame_interp.h).
//
// While enabled, a second layer of wrappers over the volk vkCmd* pointers
// copies every command (without its command buffer) into a frame buffer and
// passes it on. replay() issues the captured commands again into the current
// command buffer: the in-between frame then costs the Vulkan commands only,
// not the GX2 decoding, state resolution and cache checks that produced them.
// Queries, timestamps, readbacks, texture uploads, compute and stream-out are
// not captured; draws inside a transform-feedback section are dropped too.
#pragma once
#include "gx2_vk.h"

namespace gx2vk::capture {
void install();                 // render thread, after the device exists; idempotent
void set_enabled(bool enabled); // render thread: capture following commands
bool enabled();
// Annotation for the following descriptor binds: the draw unit whose uniform
// buffers they bind (gx2_frame_interp.cpp).
void set_unit(uint32_t unit);
// Game thread, per frame: the plaza blends every scene pass (see replay()).
void set_plaza(bool plaza);
bool plaza();
// A GPU-side staging buffer the renderer copies images through (render thread).
void register_staging(VkBuffer buffer, VkBuffer replaced);
// End of a frame: the captured commands become the replay frame.
void finish_frame();
void discard(); // drop both the capture and the replay frame
size_t replay_command_count();
size_t replay_set_binds(); // descriptor-set binds in the replay frame (not substituted)
bool replay_complete();    // the replay frame captured every command it needs
// Uniform buffer substitution for a push-descriptor write of `unit`: return
// true and the buffer/offset to bind instead.
using Substitute = bool (*)(uint32_t unit, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range,
                            VkBuffer& out_buffer, VkDeviceSize& out_offset);
void replay(Substitute substitute);
} // namespace gx2vk::capture

namespace gx2vk {
// Keep the upload chunks and deferred destructions used since `since_serial`
// alive until the submission now being recorded completes (the replay reuses
// frame N's data).
void retain_until_current_submission(uint64_t since_serial);
} // namespace gx2vk
