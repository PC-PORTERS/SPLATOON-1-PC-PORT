// gx2_texture_pack.h - texture packs keyed by the guest texture's content hash.
//
// RBREW_TEXTURE_DUMP=<dir>: every new 2D colour texture (32x32 and larger)
// is written once as <dir>\<hash>.dds with its level 0 in the same Vulkan
// format (BC1-5 or RGBA8; DX10 header), ready to be repainted or sharpened.
// Game\textures\<hash>.dds (or RBREW_TEXTURE_PACK=<dir>) replaces level 0 of
// the texture with that content hash when format and size match.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "vk/gx2_vk.h"

namespace gx2texpack {
bool dumping();
void dump(uint64_t hash, VkFormat format, uint32_t width, uint32_t height, const uint8_t* level0, size_t bytes);
bool replacing(); // a pack folder with .dds files exists
// Level 0 of the replacement for `hash`, if one matches format and size.
bool replacement(uint64_t hash, VkFormat format, uint32_t width, uint32_t height, size_t bytes,
                 std::vector<uint8_t>& out);
// HD replacement: 2x, 4x or 8x the game texture's size, same format, with
// its own mip chain (all levels in the file are used).
// A BC1/BC2/BC3 texture may also be replaced by uncompressed RGBA8 (same
// sRGB-ness): `format` then reports the storage format to use.
struct HdTexture {
    uint32_t width = 0, height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::vector<std::vector<uint8_t>> levels;
};
bool hd_replacement(uint64_t hash, VkFormat format, uint32_t width, uint32_t height, HdTexture& out);
} // namespace gx2texpack
