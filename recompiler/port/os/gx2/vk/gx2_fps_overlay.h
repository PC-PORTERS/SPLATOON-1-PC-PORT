// gx2_fps_overlay.h - real-time FPS overlay drawn into presented images.
//
// Measured at actual presentation (gx2vk::present), averaged over half-second
// windows, with the longest frame interval of the window as a stutter hint.
// The panel (below) or the classic 5x7 text line is rasterised on the CPU into
// a small opaque strip and copied into the swapchain image's top-left corner
// after the frame's blit: no shaders, pipelines or extra render passes.
#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#pragma comment(lib, "gdi32")
#endif

namespace gx2vk::overlay {

// Rows top to bottom; bit 4 is the leftmost of five columns.
inline const uint8_t* glyph(char c) {
    static const uint8_t blank[7] = {};
    static const uint8_t digits[10][7] = {
        {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
        {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
        {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
        {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
        {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}};
    static const uint8_t dot[7] = {0,0,0,0,0,0x0C,0x0C};
    static const uint8_t F[7] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10};
    static const uint8_t P[7] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10};
    static const uint8_t S[7] = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E};
    static const uint8_t m[7] = {0,0,0x1A,0x15,0x15,0x11,0x11};
    static const uint8_t s[7] = {0,0,0x0E,0x10,0x0E,0x01,0x1E};
    static const uint8_t a[7] = {0,0,0x0E,0x01,0x0F,0x11,0x0F};
    static const uint8_t x[7] = {0,0,0x11,0x0A,0x04,0x0A,0x11};
    if (c >= '0' && c <= '9') return digits[c - '0'];
    switch (c) {
    case '.': return dot; case 'F': return F; case 'P': return P; case 'S': return S;
    case 'm': return m; case 's': return s; case 'a': return a; case 'x': return x;
    default: return blank;
    }
}

constexpr unsigned kCellWidth = 6, kCellHeight = 9, kPadding = 2;   // in font pixels

// Strip size, in image pixels, for `text` at integer `scale`.
inline void strip_size(const std::string& text, unsigned scale, unsigned& width, unsigned& height) {
    width = unsigned(text.size() * kCellWidth + kPadding * 2 - 1) * scale;
    height = (kCellHeight + kPadding * 2 - 2) * scale;
}

// Fill `out` (width * height 32-bit texels, row pitch = width) with `text`.
inline void rasterize(const std::string& text, unsigned scale, uint32_t foreground, uint32_t background,
                      uint32_t* out, unsigned width, unsigned height) {
    std::fill(out, out + size_t(width) * height, background);
    for (size_t i = 0; i < text.size(); ++i) {
        const uint8_t* rows = glyph(text[i]);
        const unsigned x0 = unsigned(kPadding + i * kCellWidth) * scale, y0 = (kPadding - 1) * scale;
        for (unsigned row = 0; row < 7; ++row) for (unsigned column = 0; column < 5; ++column) {
            if (!(rows[row] >> (4 - column) & 1)) continue;
            for (unsigned dy = 0; dy < scale; ++dy) {
                const unsigned y = y0 + row * scale + dy;
                if (y >= height) continue;
                for (unsigned dx = 0; dx < scale; ++dx) {
                    const unsigned px = x0 + column * scale + dx;
                    if (px < width) out[size_t(y) * width + px] = foreground;
                }
            }
        }
    }
}

// Rolling presentation statistics.
class Stats {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr size_t kHistory = 512;   // frame intervals kept for the graph
    // Returns true when the displayed text changed.
    bool presented(Clock::time_point now) {
        if (last_.time_since_epoch().count()) {
            const double interval = std::chrono::duration<double, std::milli>(now - last_).count();
            worst_ = std::max(worst_, interval);
            ++frames_;
            history_[head_++ % kHistory] = float(interval);
        } else window_ = now;
        last_ = now;
        const double elapsed = std::chrono::duration<double>(now - window_).count();
        if (elapsed < 0.5 || !frames_) return false;
        fps_ = frames_ / elapsed;
        window_worst_ = worst_;
        low1_ = one_percent_low();
        char line[64];
        std::snprintf(line, sizeof(line), "FPS %.1f  %.1fms  max %.1fms", fps_, 1000.0 / fps_, worst_);
        text_ = line;
        frames_ = 0; worst_ = 0; window_ = now;
        return true;
    }
    const std::string& text() const { return text_; }
    double fps() const { return fps_; }
    double worst_ms() const { return window_worst_; }   // longest interval of the last window
    double low1_fps() const { return low1_; }           // FPS of the slowest 1% over ~2 s
    // i = 0 is the newest interval; 0 when not yet recorded.
    float interval(size_t i) const { return i < std::min<size_t>(head_, kHistory) ? history_[(head_ - 1 - i) % kHistory] : 0.0f; }
private:
    double one_percent_low() const {
        const size_t count = std::min<size_t>(std::min<size_t>(head_, kHistory), 120);
        if (!count) return 0;
        float sorted[120];
        for (size_t i = 0; i < count; ++i) sorted[i] = interval(i);
        const size_t index = (count - 1) - (count - 1) / 100;   // 99th percentile interval
        std::nth_element(sorted, sorted + index, sorted + count);
        return sorted[index] > 0 ? 1000.0 / sorted[index] : 0;
    }
    Clock::time_point last_{}, window_{};
    unsigned frames_ = 0;
    double worst_ = 0, fps_ = 0, window_worst_ = 0, low1_ = 0;
    float history_[kHistory]{};
    size_t head_ = 0;
    std::string text_ = "FPS --";
};

// ---------------------------------------------------------------------------
// Panel overlay: anti-aliased system font (GDI glyph outlines, rasterised once
// per size), FPS, frame time / 1% low / max and a frame-time graph with a
// 16.7 ms reference, monochrome and translucent (blended in gx2vk::present).
// RBREW_FPS_OVERLAY_STYLE=classic keeps the original 5x7 text line.
// ---------------------------------------------------------------------------
struct Glyph { int width = 0, height = 0, left = 0, top = 0, advance = 0; std::vector<uint8_t> alpha; };

class Font {
public:
    // pixel_height: em height in pixels; weight: FW_* (400 normal, 600 semibold).
    bool load(const wchar_t* face, int pixel_height, int weight) {
        if (pixel_height == size_ && weight == weight_ && loaded_) return true;
        size_ = pixel_height; weight_ = weight; loaded_ = false;
#if defined(_WIN32)
        HDC dc = CreateCompatibleDC(nullptr);
        if (!dc) return false;
        HFONT font = CreateFontW(-pixel_height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
        if (!font) { DeleteDC(dc); return false; }
        HGDIOBJ previous = SelectObject(dc, font);
        TEXTMETRICW metrics{};
        GetTextMetricsW(dc, &metrics);
        ascent_ = metrics.tmAscent; descent_ = metrics.tmDescent;
        const MAT2 identity{{0, 1}, {0, 0}, {0, 0}, {0, 1}};
        bool ok = true;
        for (int c = 32; c < 127; ++c) {
            Glyph& g = glyphs_[c - 32];
            GLYPHMETRICS gm{};
            const DWORD bytes = GetGlyphOutlineW(dc, UINT(c), GGO_GRAY8_BITMAP, &gm, 0, nullptr, &identity);
            if (bytes == GDI_ERROR) { ok = false; break; }
            g = {};
            g.advance = gm.gmCellIncX;
            if (!bytes) continue;   // blank (space)
            std::vector<uint8_t> raw(bytes);
            if (GetGlyphOutlineW(dc, UINT(c), GGO_GRAY8_BITMAP, &gm, bytes, raw.data(), &identity) == GDI_ERROR) { ok = false; break; }
            g.width = int(gm.gmBlackBoxX); g.height = int(gm.gmBlackBoxY);
            g.left = gm.gmptGlyphOrigin.x; g.top = gm.gmptGlyphOrigin.y;
            const size_t pitch = (size_t(g.width) + 3) & ~size_t(3);   // DWORD-aligned rows, levels 0..64
            g.alpha.resize(size_t(g.width) * g.height);
            for (int y = 0; y < g.height; ++y) for (int x = 0; x < g.width; ++x)
                g.alpha[size_t(y) * g.width + x] = uint8_t(std::min(64, int(raw[y * pitch + x])) * 255 / 64);
        }
        SelectObject(dc, previous);
        DeleteObject(font);
        DeleteDC(dc);
        loaded_ = ok;
#endif
        return loaded_;
    }
    int width(const std::string& text) const {
        int w = 0;
        for (char c : text) w += glyph(c).advance;
        return w;
    }
    int ascent() const { return ascent_; }
    int descent() const { return descent_; }
    // Composites `text` (straight colour, `opacity` 0..1) with its baseline at
    // y over a premultiplied RGBA float buffer of width*height texels.
    void draw(const std::string& text, int x, int baseline, float gray, float opacity,
              float* rgba, unsigned width, unsigned height) const {
        for (char c : text) {
            const Glyph& g = glyph(c);
            for (int gy = 0; gy < g.height; ++gy) {
                const int y = baseline - g.top + gy;
                if (y < 0 || y >= int(height)) continue;
                for (int gx = 0; gx < g.width; ++gx) {
                    const int px = x + g.left + gx;
                    const uint8_t coverage = g.alpha[size_t(gy) * g.width + gx];
                    if (!coverage || px < 0 || px >= int(width)) continue;
                    over(rgba + (size_t(y) * width + px) * 4, gray, opacity * coverage / 255.0f);
                }
            }
            x += g.advance;
        }
    }
    // Premultiplied "source over": a gray of `gray` (0..1) with alpha `a`.
    static void over(float* d, float gray, float a) {
        const float keep = 1.0f - a;
        d[0] = gray * a + d[0] * keep; d[1] = gray * a + d[1] * keep; d[2] = gray * a + d[2] * keep; d[3] = a + d[3] * keep;
    }
private:
    const Glyph& glyph(char c) const {
        static const Glyph blank;
        return c >= 32 && c < 127 ? glyphs_[c - 32] : blank;
    }
    Glyph glyphs_[95];
    int size_ = 0, weight_ = 0, ascent_ = 0, descent_ = 0;
    bool loaded_ = false;
};

// Monochrome, translucent panel: rounded dark glass, white numbers, grey
// labels and a white frame-time graph. Rendered as premultiplied RGBA8 and
// alpha-blended over the frame by gx2vk::present.
class Panel {
public:
    // False when no system font is available (then use the classic line).
    bool layout(double ui) {
        const int big_px = std::max(14, int(24 * ui + 0.5)), small_px = std::max(9, int(11.5 * ui + 0.5));
        if (!big_.load(L"Segoe UI Semibold", big_px, 600) || !small_.load(L"Segoe UI", small_px, 400)) return false;
        pad_ = std::max(6, int(10 * ui + 0.5));
        gap_ = std::max(2, int(3 * ui + 0.5));
        radius_ = std::max(3, int(7 * ui + 0.5));
        graph_height_ = std::max(14, int(24 * ui + 0.5));
        width_ = unsigned(std::max(150, int(212 * ui + 0.5)));
        height_ = unsigned(pad_ * 2 + big_.ascent() + big_.descent() / 2 + gap_ + small_.ascent() + small_.descent() + gap_ * 2 + graph_height_);
        return true;
    }
    unsigned width() const { return width_; }
    unsigned height() const { return height_; }

    // Writes width*height premultiplied RGBA8 texels.
    void draw(const Stats& stats, uint8_t* out) {
        const unsigned w = width_, h = height_;
        // The background is the same every frame: built once per size (it was
        // ~3.6% of the render thread when redrawn every frame).
        if (background_.size() != size_t(w) * h * 4) {
            background_.assign(size_t(w) * h * 4, 0.0f);
            // Rounded rectangle with a soft (anti-aliased) edge and a faint rim.
            const float r = float(radius_);
            for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
                const float cx = std::max(0.0f, std::max(r - (x + 0.5f), (x + 0.5f) - (w - r)));
                const float cy = std::max(0.0f, std::max(r - (y + 0.5f), (y + 0.5f) - (h - r)));
                const float dist = std::sqrt(cx * cx + cy * cy) - r;       // < 0 inside
                const float inside = std::clamp(0.5f - dist, 0.0f, 1.0f);
                if (inside <= 0) continue;
                float* d = &background_[(size_t(y) * w + x) * 4];
                Font::over(d, 0.05f, 0.58f * inside);                         // dark glass
                const float rim = std::clamp(1.0f - std::fabs(dist + 0.5f), 0.0f, 1.0f);
                if (rim > 0) Font::over(d, 1.0f, 0.10f * rim * inside);      // hairline edge
            }
        }
        rgba_ = background_;
        // Frame-time graph: one column per frame, newest on the right, 33.3 ms full height.
        const int gx0 = pad_, gx1 = int(w) - pad_, gy1 = int(h) - pad_, gy0 = gy1 - graph_height_;
        for (int y = gy0; y < gy1; ++y) for (int x = gx0; x < gx1; ++x) Font::over(&rgba_[(size_t(y) * w + x) * 4], 1.0f, 0.04f);
        const auto to_y = [&](double ms) { return gy1 - int(std::min(1.0, ms / 33.3) * graph_height_ + 0.5); };
        for (int x = gx0; x < gx1; x += 3) Font::over(&rgba_[(size_t(to_y(16.7)) * w + x) * 4], 1.0f, 0.22f);
        for (int x = gx1 - 1, i = 0; x >= gx0; --x, ++i) {
            const float ms = stats.interval(size_t(i));
            if (ms <= 0) break;
            const float a = ms <= 17.5f ? 0.38f : 0.92f;   // spikes stand out by brightness, not colour
            for (int y = std::max(gy0, to_y(ms)); y < gy1; ++y) Font::over(&rgba_[(size_t(y) * w + x) * 4], 1.0f, a);
        }
        // Text: "60 FPS" left, frame time right, then 1% low and max.
        const double fps = stats.fps();
        char number[16], frame_ms[24], detail[64];
        std::snprintf(number, sizeof(number), fps > 0 ? "%.0f" : "--", fps);
        std::snprintf(frame_ms, sizeof(frame_ms), fps > 0 ? "%.1f ms" : "-- ms", fps > 0 ? 1000.0 / fps : 0.0);
        std::snprintf(detail, sizeof(detail), "1%% low %.0f    max %.1f ms", stats.low1_fps(), stats.worst_ms());
        const int base1 = pad_ + big_.ascent();
        big_.draw(number, pad_, base1, 1.0f, 0.96f, rgba_.data(), w, h);
        small_.draw("FPS", pad_ + big_.width(number) + gap_ + 1, base1, 1.0f, 0.55f, rgba_.data(), w, h);
        small_.draw(frame_ms, int(w) - pad_ - small_.width(frame_ms), base1, 1.0f, 0.80f, rgba_.data(), w, h);
        const int base2 = base1 + big_.descent() / 2 + gap_ + small_.ascent();
        small_.draw(detail, pad_, base2, 1.0f, 0.55f, rgba_.data(), w, h);
        for (size_t i = 0; i < rgba_.size(); ++i) out[i] = uint8_t(std::clamp(rgba_[i], 0.0f, 1.0f) * 255.0f + 0.5f);
    }
private:
    Font big_, small_;
    std::vector<float> rgba_, background_;
    int pad_ = 0, gap_ = 0, radius_ = 0, graph_height_ = 0;
    unsigned width_ = 0, height_ = 0;
};

// Settings menu (ESC): a centred panel in the same monochrome glass style.
// One row per entry: label left, value right; the selected row is lit and its
// adjustable value carries arrows. Rebuilt only when the view changes.
class MenuPanel {
public:
    bool layout(double ui) {
        const int title_px = std::max(16, int(30 * ui + 0.5)), row_px = std::max(12, int(19 * ui + 0.5)), hint_px = std::max(9, int(12.5 * ui + 0.5));
        if (!title_.load(L"Segoe UI Semibold", title_px, 600) || !row_.load(L"Segoe UI", row_px, 400) ||
            !hint_.load(L"Segoe UI", hint_px, 400)) return false;
        pad_ = std::max(10, int(26 * ui + 0.5));
        row_height_ = std::max(18, int(34 * ui + 0.5));
        radius_ = std::max(4, int(12 * ui + 0.5));
        width_ = unsigned(std::max(360, int(620 * ui + 0.5)));
        return true;
    }
    unsigned width() const { return width_; }
    unsigned height(size_t rows) const {
        return unsigned(pad_ * 2 + title_.ascent() + title_.descent() + pad_ / 2 + int(rows) * row_height_ + pad_ / 2 +
                        hint_.ascent() + hint_.descent());
    }
    template <class View>
    void draw(const View& view, uint8_t* out, unsigned w, unsigned h) {
        rgba_.assign(size_t(w) * h * 4, 0.0f);
        const float r = float(radius_);
        for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
            const float cx = std::max(0.0f, std::max(r - (x + 0.5f), (x + 0.5f) - (w - r)));
            const float cy = std::max(0.0f, std::max(r - (y + 0.5f), (y + 0.5f) - (h - r)));
            const float dist = std::sqrt(cx * cx + cy * cy) - r;
            const float inside = std::clamp(0.5f - dist, 0.0f, 1.0f);
            if (inside <= 0) continue;
            float* d = &rgba_[(size_t(y) * w + x) * 4];
            Font::over(d, 0.04f, 0.82f * inside);
            const float rim = std::clamp(1.0f - std::fabs(dist + 0.5f), 0.0f, 1.0f);
            if (rim > 0) Font::over(d, 1.0f, 0.14f * rim * inside);
        }
        int y = pad_ + title_.ascent();
        title_.draw(view.title, pad_, y, 1.0f, 0.97f, rgba_.data(), w, h);
        y += title_.descent() + pad_ / 2;
        for (const auto& line : view.lines) {
            const int top = y, base = top + (row_height_ + row_.ascent() - row_.descent()) / 2;
            if (line.selected)
                for (int py = top + 1; py < top + row_height_ - 1 && py < int(h); ++py)
                    for (int px = pad_ / 2; px < int(w) - pad_ / 2; ++px) Font::over(&rgba_[(size_t(py) * w + px) * 4], 1.0f, 0.13f);
            row_.draw(line.label, pad_, base, 1.0f, line.selected ? 1.0f : 0.72f, rgba_.data(), w, h);
            if (!line.value.empty()) {
                const std::string value = line.selected && line.adjustable ? "<  " + line.value + "  >" : line.value;
                row_.draw(value, int(w) - pad_ - row_.width(value), base, 1.0f, line.selected ? 1.0f : 0.62f, rgba_.data(), w, h);
            }
            y += row_height_;
        }
        y += pad_ / 2 + hint_.ascent();
        hint_.draw(view.hint, pad_, y, 1.0f, 0.5f, rgba_.data(), w, h);
        for (size_t i = 0; i < rgba_.size(); ++i) out[i] = uint8_t(std::clamp(rgba_[i], 0.0f, 1.0f) * 255.0f + 0.5f);
    }
private:
    Font title_, row_, hint_;
    std::vector<float> rgba_;
    int pad_ = 0, row_height_ = 0, radius_ = 0;
    unsigned width_ = 0;
};

}  // namespace gx2vk::overlay
