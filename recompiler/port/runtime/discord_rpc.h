#pragma once

#include <cstdint>
#include <string>

namespace rbrew {

// Initializes Discord Rich Presence on a background worker thread.
// Non-blocking; does not delay game initialization.
// Will auto-detect discord_app_id.txt, RBREW_DISCORD_APP_ID, or fall back to default.
void discord_rpc_init(const char* custom_client_id = nullptr);

// Updates the presence status (state, details, large_image, large_text).
void discord_rpc_update_activity(const char* state, const char* details = nullptr, const char* large_image = nullptr,
                                 const char* large_text = nullptr);

// Non-blocking poll called periodically (e.g. once every 60 frames) to check active scene and update RPC.
void discord_rpc_poll_game_state(uint8_t* guest_mem, uint32_t frame_count);

// Shuts down Discord RPC and cleans up worker thread & pipe handles.
void discord_rpc_shutdown();

} // namespace rbrew
