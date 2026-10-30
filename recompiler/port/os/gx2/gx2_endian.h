#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

// Read-only guest-to-host conversion. Unaligned inputs and scalar tails are
// supported; no read extends beyond count words. SSE2 is baseline on Win64.
inline void gx2_copy_be_words(int32_t* destination, const uint8_t* source, uint32_t count) {
    uint32_t i = 0;
#if defined(_M_X64) || defined(__SSE2__)
    for (; i + 4 <= count; i += 4) {
        auto words = _mm_loadu_si128(reinterpret_cast<const __m128i*>(source + size_t(i) * 4));
        words = _mm_or_si128(_mm_srli_epi16(words, 8), _mm_slli_epi16(words, 8));
        words = _mm_shufflelo_epi16(words, _MM_SHUFFLE(2, 3, 0, 1));
        words = _mm_shufflehi_epi16(words, _MM_SHUFFLE(2, 3, 0, 1));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(destination + i), words);
    }
#endif
    for (; i < count; ++i) {
        const auto* p = source + size_t(i) * 4;
        const uint32_t word = uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
        std::memcpy(destination + i, &word, 4);
    }
}
