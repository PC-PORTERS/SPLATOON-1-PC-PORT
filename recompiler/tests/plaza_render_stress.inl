#include "../port/os/gx2/gx2_surface_layout.h"
// Authored populated-scene proxy through the complete guest draw entry:
// big-endian guest indices, register-selected R700 bytecode, exact program-key
// resolution, translation/linking, pipelines, descriptors and submission.
// Bytecode encodings are constructed here (as in gambit_shader_bench); no
// title shader, asset or capture is used. Positions are constant, so this is
// a CPU translation-layer workload; GPU shading is covered by `heavy`.
void gx2_render_vk_test_plaza() {
    using namespace render_stress;
    using Clock = std::chrono::steady_clock;
    auto elapsed = [](Clock::time_point t) {
        return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
    };
    constexpr unsigned pairs = 32, meshes = 16, extent = 512, indices_per_draw = 384;
    // RBREW_BENCH_FRAMES lengthens profiling runs; comparisons use 12.
    const unsigned frames = [] {
        const char* v = std::getenv("RBREW_BENCH_FRAMES");
        const int n = v ? std::atoi(v) : 0;
        return n > 0 ? unsigned(n) : 12u;
    }();
    constexpr unsigned shadow_draws = 384, opaque_draws = 1024, blended_draws = 256,
                       draws_per_frame = shadow_draws + opaque_draws + blended_draws;
    constexpr uint32_t code_base = 65536, code_stride = 8192, index_base = code_base + pairs * code_stride,
                       index_bytes = indices_per_draw * 2;
    // Material textures: guest GX2Texture descriptors over macro-tiled images,
    // four bound per draw through the guest bind entry (GX2SetPixelTexture).
    constexpr unsigned textures = 64, texture_size = 32;
    constexpr uint32_t descriptor_base = index_base + meshes * index_bytes,
                       image_base = descriptor_base + textures * 256, image_stride = 65536;
    const size_t arena_bytes = image_base + size_t(textures) * image_stride;
#ifdef _WIN32
    auto* mem = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, arena_bytes, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE));
    require(mem != nullptr, "plaza write-watch arena failed");
#else
    std::vector<uint8_t> arena(arena_bytes);
    auto* mem = arena.data();
#endif
    // One ALU clause (MOVs then ADDs) and an export; unreferenced trailing
    // words give each program a realistic multi-KiB hashed/compared size.
    auto bytecode = [](unsigned instructions, bool pixel, unsigned variant, unsigned bytes, unsigned seed) {
        std::vector<uint32_t> words{2, (8u << 26) | ((instructions - 1) << 18),
                                    (1u << 15) | (pixel ? 0u : (1u << 13) | 60u), (0x28u << 23) | (1u << 21) | 0x688u};
        for (unsigned i = 0; i < instructions; ++i) {
            const unsigned lane = i % 4, src = i < 4 ? 249u : 1u, op = i < 4 ? 0x19u : 0u;
            words.push_back(src | (lane << 10) | ((variant & 1 ? 252u : 249u) << 13) | (lane == 3 ? 1u << 31 : 0u));
            words.push_back((op << 7) | (1u << 4) | (1u << 21) | (lane << 29));
        }
        for (uint32_t x = seed * 2654435761u; words.size() * 4 < bytes;)
            words.push_back(x = x * 1664525u + 1013904223u);
        return words;
    };
    struct Pair {
        uint32_t vs, vs_size, ps, ps_size;
    };
    std::array<Pair, pairs> table;
    for (unsigned i = 0; i < pairs; ++i) {
        const auto vs = bytecode(8 + 4 * (i % 30), false, i, 1536 + (i % 8) * 256, i * 2 + 1),
                   ps = bytecode(4 + 4 * i, true, i, 1280 + (i % 6) * 384, i * 2 + 2);
        auto& p = table[i];
        p.vs = code_base + i * code_stride;
        p.ps = p.vs + 4096;
        p.vs_size = uint32_t(vs.size() * 4);
        p.ps_size = uint32_t(ps.size() * 4);
        require(p.vs_size % 8 == 0 && p.ps_size % 8 == 0 && p.vs_size <= 4096 && p.ps_size <= 4096,
                "plaza bytecode layout");
        std::memcpy(mem + p.vs, vs.data(), p.vs_size);
        std::memcpy(mem + p.ps, ps.data(), p.ps_size);
    }
    for (unsigned m = 0; m < meshes; ++m)
        for (unsigned i = 0; i < indices_per_draw; ++i) {
            const uint16_t v = uint16_t((i * 7 + m) % indices_per_draw);
            mem[index_base + m * index_bytes + i * 2] = uint8_t(v >> 8);
            mem[index_base + m * index_bytes + i * 2 + 1] = uint8_t(v);
        }
    {
        RbrewGx2SurfaceDesc sd{1, texture_size, texture_size, 1, 1, 0x1a, 0, 1, 4, 0};
        const auto layout = rbrew_gx2_calc_surface_layout(sd);
        require(layout && layout->image_size <= image_stride, "plaza texture layout");
        for (unsigned i = 0; i < textures; ++i) {
            const uint32_t d = descriptor_base + i * 256, image = image_base + i * image_stride;
            auto word = [&](unsigned offset, uint32_t value) { rbrew_write32(mem, d + offset, value); };
            word(GX2SURF_DIM, 1);
            word(GX2SURF_WIDTH, texture_size);
            word(GX2SURF_HEIGHT, texture_size);
            word(GX2SURF_DEPTH, 1);
            word(GX2SURF_MIP_LEVELS, 1);
            word(GX2SURF_FORMAT, 0x1a);
            word(GX2SURF_USE, 1);
            word(GX2SURF_IMAGE_SIZE, uint32_t(layout->image_size));
            word(GX2SURF_IMAGE, image);
            word(GX2SURF_TILE_MODE, 4);
            word(GX2SURF_PITCH, layout->pitch);
            word(GX2TEX_VIEW_NUM_MIPS, 1);
            word(GX2TEX_VIEW_NUM_SLICES, 1);
            word(GX2TEX_COMP_MAP, 0x00010203);
            for (uint32_t b = 0; b < layout->image_size; ++b) mem[image + b] = uint8_t(i * 13 + b);
        }
    }
    // Regular CB_SHADER_MASK/clip-space state; alpha-tested materials and the
    // shadow casters' far-clip switch vary exact-key registers between draws.
    S.regs[0xA08F] = 15;
    S.vs_from_regs = S.ps_from_regs = true;
    S.cur_vs_attribs = 0;
    S.active_fetch = 0;
    auto select = [&](unsigned pair, bool alpha_test, bool shadow_pass) {
        const auto& p = table[pair];
        S.regs[0xA216] = p.vs >> 8;
        S.regs[0xA217] = p.vs_size >> 3;
        S.regs[0xA210] = p.ps >> 8;
        S.regs[0xA211] = p.ps_size >> 3;
        S.regs[0xA104] = alpha_test ? 0xCu : 0u;
        S.regs[0xA204] = (1u << 19) | (shadow_pass ? 1u << 27 : 0u);
    };
    std::array<ColorTarget, 2> targets;
    DepthTarget shadow;
    for (auto& t : targets) {
        t.width = t.height = extent;
        gx2vk::ImageDesc d;
        d.width = d.height = extent;
        d.usage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        require(gx2vk::create_image(t.image, d), "plaza target allocation failed");
        VkClearColorValue zero{};
        clear_image_color(t.image, zero);
    }
    {
        gx2vk::ImageDesc d;
        d.width = d.height = extent;
        d.format = VK_FORMAT_D32_SFLOAT;
        d.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        require(gx2vk::create_image(shadow.image, d), "plaza shadow allocation failed");
        shadow.width = shadow.height = extent;
    }
    S.raster = RasterState{};
    S.raster.viewport[2] = S.raster.viewport[3] = 16;
    S.conditional_draw = true;
    S.framebuffer = {};
    S.framebuffer.valid = true;
    auto draw = [&](unsigned pass, unsigned o, unsigned f) {
        const unsigned pair = pass == 0 ? 28 + (o >> 1) % 4 : pass == 1 ? ((o >> 1) * 7 + f) % 28 : 24 + (o >> 1) % 4;
        select(pair, pass == 1 && (o >> 1) % 5 == 0, pass == 0);
        S.raster.blend = pass == 2;
        S.raster.blend_src_rgb = wire::SRC_ALPHA;
        S.raster.blend_dst_rgb = wire::ONE_MINUS_SRC_ALPHA;
        S.raster.depth_test = pass == 0;
        S.raster.depth_write = pass == 0;
        S.target_masks = pass == 0 ? 0 : 1;
        S.framebuffer.depth = pass == 0 ? &shadow : nullptr;
        S.framebuffer.colors[0] = pass == 0 ? nullptr : &targets[(o / 128) & 1];
        S.raster.viewport[0] = int32_t(o % 32 * 16);
        S.raster.viewport[1] = int32_t(o / 32 % 32 * 16);
        uint32_t pose[64];
        for (unsigned i = 0; i < 64; ++i) pose[i] = (o * 64 + i + f) * 0x9E3779B9u;
        uint32_t tint[8];
        for (unsigned i = 0; i < 8; ++i) tint[i] = o + i + f;
        gx2_render_set_uniform_words(false, 0, pose, 64);
        gx2_render_set_uniform_words(true, 0, tint, 8);
        if (pass)
            for (unsigned t = 0; t < 4; ++t)
                gx2_render_bind_texture(descriptor_base + ((o >> 1) * 5 + t * 17 + f) % textures * 256, t, mem,
                                        nullptr);
        gx2_render_draw_indexed(4, indices_per_draw, GX2_INDEX_TYPE_U16_BE, index_base + (o % meshes) * index_bytes, 0,
                                mem, 1, 0);
    };
    auto frame = [&](unsigned f) {
        for (unsigned pass = 0; pass < 3; ++pass) {
            const unsigned n = pass == 0 ? shadow_draws : pass == 1 ? opaque_draws : blended_draws;
            for (unsigned o = 0; o < n; ++o) draw(pass, o, f);
        }
    };
    // Contract: the memoized key equals a full recomputation for every
    // combination and is invalidated by bytecode and register changes.
    struct Seen {
        uint64_t key;
        uint32_t program;
    };
    std::vector<Seen> reference;
    gx2_write_watch_begin_batch();
    s_test_key_memo_enabled = false;
    // A first translation learns each base key's read masks, which then
    // become part of later keys: record the reference after that.
    for (unsigned pass = 0; pass < 3; ++pass)
        for (unsigned o = 0; o < 64; ++o) draw(pass, o, 0);
    for (unsigned pass = 0; pass < 3; ++pass)
        for (unsigned o = 0; o < 64; ++o) {
            s_last_program = 0;
            draw(pass, o, 0);
            reference.push_back({s_last_exact_key, s_last_program});
            require(s_last_program != 0, "plaza program resolution failed");
        }
    s_test_key_memo_enabled = true;
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        size_t at = 0;
        for (unsigned pass = 0; pass < 3; ++pass)
            for (unsigned o = 0; o < 64; ++o, ++at) {
                draw(pass, o, 0);
                require(s_last_exact_key == reference[at].key && s_last_program == reference[at].program,
                        "memoized plaza key differs from recomputation");
            }
    }
    {
        draw(1, 2, 0);
        const uint64_t before = s_last_exact_key;
        const uint32_t ps = S.regs[0xA210] << 8, ps_size = S.regs[0xA211] << 3;
        auto again = [&] {
            gx2_render_draw_indexed(4, indices_per_draw, GX2_INDEX_TYPE_U16_BE, index_base, 0, mem, 1, 0);
        };
        // Guest stores land before the batch that consumes them is taken.
        for (unsigned repeat = 0; repeat < 2; ++repeat) {
            draw(1, 2, 0);
            require(s_last_exact_key == before, "unchanged bytecode lost its key");
        }
        const uint8_t saved = mem[ps + ps_size - 1];
        mem[ps + ps_size - 1] ^= 0x5A;
        gx2_write_watch_begin_batch();
        again();
        require(s_last_exact_key != before, "in-place bytecode change reused a memoized key");
        mem[ps + ps_size - 1] = saved;
        gx2_write_watch_begin_batch();
        again();
        require(s_last_exact_key == before, "restored bytecode did not return its key");
        const uint32_t vs = S.regs[0xA216] << 8;
        const uint8_t vs_saved = mem[vs + 8];
        mem[vs + 8] ^= 0x01;
        gx2_write_watch_begin_batch();
        again();
        require(s_last_exact_key != before, "in-place vertex bytecode change reused a memoized key");
        mem[vs + 8] = vs_saved;
        gx2_write_watch_begin_batch();
        again();
        require(s_last_exact_key == before, "restored vertex bytecode did not return its key");
        S.regs[0xA08F] = 7;
        again();
        require(s_last_exact_key != before, "changed key register reused a memoized key");
        S.regs[0xA08F] = 15;
        again();
        require(s_last_exact_key == before, "restored key register did not return its key");
        // Pairs 0 and 24 have equal program sizes and key registers. After both
        // pages are written, a draw of 0 follows one of 24 whose page has the
        // newer generation: the previous draw's memo must not vouch for it.
        auto key_of = [&](unsigned pair) {
            select(pair, false, false);
            again();
            return std::pair{s_last_exact_key, s_last_program};
        };
        s_test_key_memo_enabled = false;
        const auto first = key_of(0), second = key_of(24);
        s_test_key_memo_enabled = true;
        require(first.first != second.first, "plaza memo contract needs distinct programs");
        for (unsigned repeat = 0; repeat < 3; ++repeat) {
            for (unsigned pair : {0u, 24u}) {
                volatile uint8_t* at = mem + table[pair].ps + table[pair].ps_size - 1;
                *at = *at;
            }
            gx2_write_watch_begin_batch();
            require(key_of(0) == first && key_of(24) == second && key_of(0) == first && key_of(24) == second,
                    "memoized key of another program range was reused for equal-sized code");
        }
    }
    end_rendering();
    require(gx2vk::finish(), "plaza contract completion failed");
    // Warm every program and pipeline the measured frames use.
    for (unsigned f = 0; f < frames; ++f) {
        gx2_write_watch_begin_batch();
        frame(f);
        end_rendering();
        gx2vk::submit(false);
    }
    require(gx2vk::finish(), "plaza warm-up completion failed");
    counts = {};
    Hooks hooks;
    const auto draws = C.draws, linked = C.programs_linked, pipelines = C.pipelines,
               submissions = gx2vk::submission_count(), texture_uploads = C.texture_uploads;
    const uint64_t memo_hits = s_exact_key_memo_hits;
    std::vector<double> cpu_samples;
    uint64_t checksum = 0;
    const auto begin = Clock::now();
    for (unsigned f = 0; f < frames; ++f) {
        const auto start = Clock::now();
        gx2_write_watch_begin_batch();
        clear_image_depth(shadow.image, 1, 0);
        for (auto& t : targets) {
            VkClearColorValue zero{};
            clear_image_color(t.image, zero);
        }
        for (unsigned pass = 0; pass < 3; ++pass) {
            const unsigned n = pass == 0 ? shadow_draws : pass == 1 ? opaque_draws : blended_draws;
            for (unsigned o = 0; o < n; ++o) {
                draw(pass, o, f);
                checksum = checksum * 1099511628211ull + s_last_program;
            }
        }
        end_rendering();
        gx2vk::submit(false);
        cpu_samples.push_back(elapsed(start));
    }
    require(gx2vk::finish(), "plaza GPU completion failed");
    const double wall = elapsed(begin);
    require(C.draws - draws == uint64_t(frames) * draws_per_frame, "plaza draws were skipped");
    require(C.programs_linked == linked && C.pipelines == pipelines, "plaza measured frames compiled new objects");
    require(C.texture_uploads == texture_uploads, "plaza measured frames re-uploaded unchanged textures");
    auto percentile = [](std::vector<double> values, double p) {
        std::sort(values.begin(), values.end());
        return values[std::min(values.size() - 1, size_t(std::ceil(p * values.size()) - 1))];
    };
    std::printf(
        "{\"metric\":\"plaza_frames\",\"frames\":%u,\"objects\":%u,\"draws_per_frame\":%u,\"indices_per_draw\":%u,"
        "\"programs\":%llu,\"pipelines\":%llu,\"checksum\":%llu,"
        "\"wall_ms\":%.6f,\"cpu_median_ms\":%.6f,\"cpu_p95_ms\":%.6f,\"cpu_p99_ms\":%.6f,\"draws_per_sec\":%.3f,\"key_"
        "memo_hits\":%llu,\"pipeline_binds\":%llu,\"descriptor_binds\":%llu,\"submissions\":%llu,\"passed\":true}\n",
        frames, pairs, draws_per_frame, indices_per_draw, (unsigned long long)C.programs_linked,
        (unsigned long long)C.pipelines, (unsigned long long)checksum, wall, percentile(cpu_samples, .5),
        percentile(cpu_samples, .95), percentile(cpu_samples, .99), frames * draws_per_frame * 1000. / wall,
        (unsigned long long)(s_exact_key_memo_hits - memo_hits), (unsigned long long)counts.pipelines,
        (unsigned long long)counts.binds, (unsigned long long)(gx2vk::submission_count() - submissions));
    S.framebuffer = {};
    S.units = {};
    S.bound_textures = {};
    S.texture_entries = {};
    S.vs_from_regs = S.ps_from_regs = false;
    S.regs[0xA104] = 0;
    S.regs[0xA204] = 0;
    end_rendering();
    for (auto& t : targets) gx2vk::destroy_image(t.image);
    gx2vk::destroy_image(shadow.image);
    require(gx2vk::finish(), "plaza cleanup wait failed");
    reset_command_tracking();
#ifdef _WIN32
    rbrew_set_write_observer({});
    VirtualFree(mem, 0, MEM_RELEASE);
#endif
}
