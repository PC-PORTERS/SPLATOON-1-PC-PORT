// gx2_weather.h - the plaza's sky, clouds, light shafts and rain
// (gx2_weather.cpp). The lights themselves follow gx2_env_light.h.
#pragma once
#include <cstdint>
#include "vk/gx2_vk.h"
namespace rbrew_settings {
struct Values;
}
namespace gx2weather {
struct Camera;
struct RainCover;
bool set_rain_cover(const RainCover& cover);
// Before a draw: the plaza's sky dome (drawn at the far plane while true).
bool sky_draw(uint8_t* memory, uint32_t count);
// Inside rendering, instead of the dome's colour draw into the HDR scene
// target: true if the sky was drawn (the caller skips the game's draw and
// resets its command tracking).
bool draw_sky(uint8_t* memory, VkCommandBuffer cb, const VkFormat* colors, VkFormat depth, bool stencil);
// After every draw: finds the camera and the final scene pass.
void observe_draw(uint8_t* memory, uint32_t primitive, uint32_t count);
// Cinema mode: true for HUD draws into the TV image after the scene.
bool hud_hidden();
bool apply(gx2vk::Image& color, gx2vk::Image& depth, const Camera& camera, const rbrew_settings::Values& settings,
           double seconds, double cycle_seconds = -1);
void shutdown();
} // namespace gx2weather
