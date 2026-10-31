// Authored guest skeletal animation, written in the form RebrewU emits for
// recompiled PowerPC (no game code). Per bone, as a skeletal model update
// does: ten Hermite curves sampled through a virtual call, quaternion/scale/
// translation to a 3x4 matrix with paired singles, parent concatenation,
// inverse-bind concatenation and a byte-reversed palette write. Functions
// keep the PowerPC ABI: stack frames, saved LR and non-volatile registers,
// guest-memory stores for every spill. Paired-single words are literals.
//
// GUEST_PS_WORD makes each word a compile-time constant, as the literals of
// generated code are. Include with GUEST_PS(word) defined; it expands inside a namespace so a
// test can build the same program against different paired-single paths.
#ifndef GUEST_SKELETON_COMMON
#define GUEST_SKELETON_COMMON
namespace guest_skeleton {
constexpr uint32_t kBones = 96, kPaletteBones = 64, kChannels = 10, kKeys = 16;
constexpr uint32_t kKeyBytes = 12, kCurveBytes = 8 + kKeys * kKeyBytes, kBoneCurveBytes = kChannels * kCurveBytes;
constexpr uint32_t kTransformBytes = 40, kMatrixBytes = 48;
// Guest addresses of the program's functions (dispatch slots).
constexpr uint32_t kCurveSampleAddress = 0x00008000u;

// Encoders (IBM bit numbering: field at bits [first, last]).
constexpr uint32_t ps_a(uint32_t xo, uint32_t d, uint32_t a, uint32_t b, uint32_t c) {
    return 4u << 26 | d << 21 | a << 16 | b << 11 | c << 6 | xo << 1;
}
constexpr uint32_t ps_x(uint32_t xo, uint32_t d, uint32_t a, uint32_t b) {
    return 4u << 26 | d << 21 | a << 16 | b << 11 | xo << 1;
}
constexpr uint32_t psq_l(uint32_t d, int32_t off, uint32_t a, uint32_t w = 0, uint32_t i = 0) {
    return 56u << 26 | d << 21 | a << 16 | w << 15 | i << 12 | (uint32_t(off) & 0xFFFu);
}
constexpr uint32_t psq_st(uint32_t s, int32_t off, uint32_t a, uint32_t w = 0, uint32_t i = 0) {
    return 60u << 26 | s << 21 | a << 16 | w << 15 | i << 12 | (uint32_t(off) & 0xFFFu);
}
constexpr uint32_t ps_mul(uint32_t d, uint32_t a, uint32_t c) {
    return ps_a(25, d, a, 0, c);
}
constexpr uint32_t ps_muls0(uint32_t d, uint32_t a, uint32_t c) {
    return ps_a(12, d, a, 0, c);
}
constexpr uint32_t ps_muls1(uint32_t d, uint32_t a, uint32_t c) {
    return ps_a(13, d, a, 0, c);
}
constexpr uint32_t ps_madds0(uint32_t d, uint32_t a, uint32_t c, uint32_t b) {
    return ps_a(14, d, a, b, c);
}
constexpr uint32_t ps_madds1(uint32_t d, uint32_t a, uint32_t c, uint32_t b) {
    return ps_a(15, d, a, b, c);
}
constexpr uint32_t ps_madd(uint32_t d, uint32_t a, uint32_t c, uint32_t b) {
    return ps_a(29, d, a, b, c);
}
constexpr uint32_t ps_add(uint32_t d, uint32_t a, uint32_t b) {
    return ps_a(21, d, a, b, 0);
}
constexpr uint32_t ps_sub(uint32_t d, uint32_t a, uint32_t b) {
    return ps_a(20, d, a, b, 0);
}
constexpr uint32_t ps_sum0(uint32_t d, uint32_t a, uint32_t c, uint32_t b) {
    return ps_a(10, d, a, b, c);
}
constexpr uint32_t ps_sum1(uint32_t d, uint32_t a, uint32_t c, uint32_t b) {
    return ps_a(11, d, a, b, c);
}
constexpr uint32_t ps_neg(uint32_t d, uint32_t b) {
    return ps_x(40, d, 0, b);
}
constexpr uint32_t ps_mr(uint32_t d, uint32_t b) {
    return ps_x(72, d, 0, b);
}
constexpr uint32_t ps_merge00(uint32_t d, uint32_t a, uint32_t b) {
    return ps_x(528, d, a, b);
}
constexpr uint32_t ps_merge01(uint32_t d, uint32_t a, uint32_t b) {
    return ps_x(560, d, a, b);
}
constexpr uint32_t ps_merge10(uint32_t d, uint32_t a, uint32_t b) {
    return ps_x(592, d, a, b);
}
constexpr uint32_t ps_merge11(uint32_t d, uint32_t a, uint32_t b) {
    return ps_x(624, d, a, b);
}
} // namespace guest_skeleton

// Generated-code idioms.
#define G_R(n) cpu->r[n]
#define G_F(n) cpu->f[n]
#define G_LWZ(d, off, a) (G_R(d) = rbrew_read32(cpu->mem, G_R(a) + uint32_t(off)))
#define G_LHZ(d, off, a) (G_R(d) = rbrew_read16(cpu->mem, G_R(a) + uint32_t(off)))
#define G_STW(s, off, a) rbrew_write32(cpu->mem, G_R(a) + uint32_t(off), G_R(s))
#define G_STWBRX(s, a, b) rbrew_write32(cpu->mem, G_R(a) + G_R(b), rbrew_bswap32(G_R(s)))
#define G_STWU(s, off, a)                                                                                              \
    do {                                                                                                               \
        const uint32_t t_ = G_R(a) + uint32_t(off);                                                                    \
        rbrew_write32(cpu->mem, t_, G_R(s));                                                                           \
        G_R(a) = t_;                                                                                                   \
    } while (0)
#define G_LFS(d, off, a)                                                                                               \
    do {                                                                                                               \
        G_F(d) = rbrew_read_f32(cpu->mem, G_R(a) + uint32_t(off));                                                     \
        cpu->ps1[d] = G_F(d);                                                                                          \
    } while (0)
#define G_STFS(s, off, a) rbrew_write_f32(cpu->mem, G_R(a) + uint32_t(off), G_F(s))
#define G_STFD(s, off, a) rbrew_write_f64(cpu->mem, G_R(a) + uint32_t(off), G_F(s))
#define G_LFD(d, off, a) (G_F(d) = rbrew_read_f64(cpu->mem, G_R(a) + uint32_t(off)))
#define G_SINGLE(d, expr)                                                                                              \
    do {                                                                                                               \
        G_F(d) = rbrew_fround(expr);                                                                                   \
        cpu->ps1[d] = G_F(d);                                                                                          \
    } while (0)
#define G_FMULS(d, a, c) G_SINGLE(d, G_F(a) * G_F(c))
#define G_FADDS(d, a, b) G_SINGLE(d, G_F(a) + G_F(b))
#define G_FSUBS(d, a, b) G_SINGLE(d, G_F(a) - G_F(b))
#define G_FDIVS(d, a, b) G_SINGLE(d, G_F(a) / G_F(b))
#define G_FMADDS(d, a, c, b) G_SINGLE(d, fma(G_F(a), G_F(c), G_F(b)))
#define G_FMR(d, b) (G_F(d) = G_F(b))
#define G_PROLOGUE(frame, saved)                                                                                       \
    do {                                                                                                               \
        G_STWU(1, -int32_t(frame), 1);                                                                                 \
        G_R(0) = cpu->lr;                                                                                              \
        G_STW(0, (frame) + 4, 1);                                                                                      \
        for (uint32_t r_ = 32 - (saved); r_ < 32; ++r_)                                                                \
            rbrew_write32(cpu->mem, G_R(1) + (frame) - 4u * (32 - r_), G_R(r_));                                       \
    } while (0)
#define G_EPILOGUE(frame, saved)                                                                                       \
    do {                                                                                                               \
        for (uint32_t r_ = 32 - (saved); r_ < 32; ++r_)                                                                \
            G_R(r_) = rbrew_read32(cpu->mem, G_R(1) + (frame) - 4u * (32 - r_));                                       \
        G_LWZ(0, (frame) + 4, 1);                                                                                      \
        cpu->lr = G_R(0);                                                                                              \
        G_R(1) += (frame);                                                                                             \
    } while (0)
#endif

namespace GUEST_SKELETON_NAMESPACE {
using namespace guest_skeleton;

// f1 = frame, r3 = curve {u32 count, pad, keys[count] {frame, value, slope}},
// r13 = constants: floats 0, 1, 2, 3 at +0..+12, the curve object's sample
// slot at +16, pairs (2,2) (1,1) (0,0) at +24, +32, +40. Returns f1.
inline void curve_sample(CPUState* cpu) {
    G_PROLOGUE(32, 2);
    G_R(31) = G_R(3);
    G_LWZ(30, 0, 31);
    G_R(4) = 0;
    G_R(5) = G_R(30) - 1;
    while (G_R(5) - G_R(4) > 1) { // binary search
        G_R(6) = (G_R(4) + G_R(5)) >> 1;
        G_R(7) = G_R(6) * kKeyBytes + G_R(31);
        G_LFS(2, 8, 7);
        cpu->cr[0] = rbrew_cmp_f64(G_F(1), G_F(2));
        if (cpu->cr[0] & 8u)
            G_R(5) = G_R(6);
        else
            G_R(4) = G_R(6);
    }
    G_R(7) = G_R(4) * kKeyBytes + G_R(31);
    G_LFS(2, 8, 7);
    G_LFS(3, 12, 7);
    G_LFS(4, 16, 7);
    G_LFS(5, 20, 7);
    G_LFS(6, 24, 7);
    G_LFS(7, 28, 7);
    G_LFS(8, 4, 13);
    G_LFS(9, 8, 13);
    G_LFS(10, 12, 13); // 1, 2, 3
    G_FSUBS(5, 5, 2);  // dt
    G_FSUBS(0, 1, 2);
    G_FDIVS(0, 0, 5); // t
    G_FMULS(11, 0, 0);
    G_FMULS(12, 11, 0); // t2, t3
    G_FMULS(1, 9, 12);
    G_FMULS(2, 10, 11);
    G_FSUBS(1, 1, 2);
    G_FADDS(1, 1, 8); // h00
    G_FMULS(2, 9, 11);
    G_FSUBS(2, 12, 2);
    G_FADDS(2, 2, 0);   // h10
    G_FSUBS(0, 8, 1);   // h01 = 1 - h00
    G_FSUBS(9, 12, 11); // h11
    G_FMULS(4, 4, 5);
    G_FMULS(7, 7, 5);
    G_FMULS(1, 1, 3);
    G_FMADDS(1, 2, 4, 1);
    G_FMADDS(1, 0, 6, 1);
    G_FMADDS(1, 9, 7, 1);
    G_EPILOGUE(32, 2);
}

// r3 = bone curves, r4 = transform {q xyzw, t xyz, s xyz}, f1 = frame.
inline void bone_animate(CPUState* cpu) {
    G_PROLOGUE(48, 4);
    G_STFD(31, 8, 1);
    G_R(31) = G_R(3);
    G_R(30) = G_R(4);
    G_FMR(31, 1);
    for (G_R(29) = 0; G_R(29) < kChannels; ++G_R(29)) {
        G_R(3) = G_R(31) + G_R(29) * kCurveBytes;
        G_FMR(1, 31);
        G_LWZ(12, 16, 13); // the curve object's sample slot
        cpu->ctr = G_R(12);
        cpu->lr = 0x00008100u;
        rbrew_call_indirect(cpu, cpu->ctr);
        G_R(28) = G_R(29) << 2;
        rbrew_write_f32(cpu->mem, G_R(30) + G_R(28), G_F(1));
    }
    // Normalize the quaternion.
    GUEST_PS(psq_l(2, 0, 30));
    GUEST_PS(psq_l(3, 8, 30));
    GUEST_PS(ps_mul(4, 2, 2));
    GUEST_PS(ps_madd(4, 3, 3, 4));
    GUEST_PS(ps_sum0(5, 4, 4, 4));
    G_F(5) = sqrt(G_F(5));
    G_LFS(6, 4, 13);
    G_FDIVS(5, 6, 5);
    GUEST_PS(ps_muls0(2, 2, 5));
    GUEST_PS(ps_muls0(3, 3, 5));
    GUEST_PS(psq_st(2, 0, 30));
    GUEST_PS(psq_st(3, 8, 30));
    G_LFD(31, 8, 1);
    G_EPILOGUE(48, 4);
}

// r3 = transform, r4 = 3x4 out.
inline void transform_to_matrix(CPUState* cpu) {
    GUEST_PS(psq_l(1, 0, 3));
    GUEST_PS(psq_l(2, 8, 3)); // (x,y) (z,w)
    GUEST_PS(psq_l(3, 16, 3));
    GUEST_PS(psq_l(4, 24, 3));
    GUEST_PS(psq_l(5, 32, 3)); // (tx,ty) (tz,sx) (sy,sz)
    GUEST_PS(psq_l(12, 24, 13));
    GUEST_PS(psq_l(11, 32, 13)); // (2,2) (1,1)
    GUEST_PS(ps_mul(6, 1, 12));
    GUEST_PS(ps_mul(7, 2, 12));   // (2x,2y) (2z,2w)
    GUEST_PS(ps_mul(8, 1, 6));    // (xx2, yy2)
    GUEST_PS(ps_muls0(9, 6, 2));  // (xz2, yz2)
    GUEST_PS(ps_muls1(10, 6, 2)); // (wx2, wy2)
    GUEST_PS(ps_muls0(0, 7, 2));  // (zz2, wz2)
    GUEST_PS(ps_muls1(13, 1, 6)); // (xy2, .)
    GUEST_PS(ps_merge10(14, 8, 8));
    GUEST_PS(ps_merge00(15, 0, 0));
    GUEST_PS(ps_add(14, 14, 15));
    GUEST_PS(ps_sub(16, 11, 14)); // (r00, r11)
    GUEST_PS(ps_sum0(17, 8, 8, 8));
    GUEST_PS(ps_sub(17, 11, 17)); // (r22, .)
    GUEST_PS(ps_merge00(14, 13, 13));
    GUEST_PS(ps_merge11(15, 0, 0));
    GUEST_PS(ps_sub(18, 14, 15));
    GUEST_PS(ps_add(19, 14, 15));    // r01, r10
    GUEST_PS(ps_sum0(20, 9, 9, 10)); // (r02, yz2)
    GUEST_PS(ps_neg(21, 10));
    GUEST_PS(ps_sum0(22, 9, 9, 21));  // (r20, yz2)
    GUEST_PS(ps_sum1(23, 21, 21, 9)); // (., r12)
    GUEST_PS(ps_sum1(24, 10, 10, 9)); // (., r21)
    GUEST_PS(ps_merge10(25, 4, 5));   // (sx, sy)
    GUEST_PS(ps_merge10(26, 5, 11));  // (sz, 1)
    GUEST_PS(ps_merge00(27, 16, 18));
    GUEST_PS(ps_mul(27, 27, 25));
    GUEST_PS(psq_st(27, 0, 4));
    GUEST_PS(ps_merge00(28, 20, 3));
    GUEST_PS(ps_mul(28, 28, 26));
    GUEST_PS(psq_st(28, 8, 4));
    GUEST_PS(ps_merge01(27, 19, 16));
    GUEST_PS(ps_mul(27, 27, 25));
    GUEST_PS(psq_st(27, 16, 4));
    GUEST_PS(ps_merge11(28, 23, 3));
    GUEST_PS(ps_mul(28, 28, 26));
    GUEST_PS(psq_st(28, 24, 4));
    GUEST_PS(ps_merge01(27, 22, 24));
    GUEST_PS(ps_mul(27, 27, 25));
    GUEST_PS(psq_st(27, 32, 4));
    GUEST_PS(ps_merge00(28, 17, 4));
    GUEST_PS(ps_mul(28, 28, 26));
    GUEST_PS(psq_st(28, 40, 4));
}

// r3 = A, r4 = B, r5 = A*B (3x4, implicit last row 0 0 0 1).
inline void matrix_concat(CPUState* cpu) {
    GUEST_PS(psq_l(2, 0, 4));
    GUEST_PS(psq_l(3, 8, 4));
    GUEST_PS(psq_l(4, 16, 4));
    GUEST_PS(psq_l(5, 24, 4));
    GUEST_PS(psq_l(6, 32, 4));
    GUEST_PS(psq_l(7, 40, 4));
    GUEST_PS(psq_l(13, 40, 13)); // (0, 0)
    for (int32_t row = 0; row < 3; ++row) {
        // Unrolled by the compiler as the original is; literal words per row.
        if (row == 0) {
            GUEST_PS(psq_l(8, 0, 3));
            GUEST_PS(psq_l(9, 8, 3));
        }
        if (row == 1) {
            GUEST_PS(psq_l(8, 16, 3));
            GUEST_PS(psq_l(9, 24, 3));
        }
        if (row == 2) {
            GUEST_PS(psq_l(8, 32, 3));
            GUEST_PS(psq_l(9, 40, 3));
        }
        GUEST_PS(ps_muls0(10, 2, 8));
        GUEST_PS(ps_madds1(10, 4, 8, 10));
        GUEST_PS(ps_muls0(11, 3, 8));
        GUEST_PS(ps_madds1(11, 5, 8, 11));
        GUEST_PS(ps_madds0(10, 6, 9, 10));
        GUEST_PS(ps_madds0(11, 7, 9, 11));
        GUEST_PS(ps_merge01(12, 13, 9));
        GUEST_PS(ps_add(11, 11, 12));
        if (row == 0) {
            GUEST_PS(psq_st(10, 0, 5));
            GUEST_PS(psq_st(11, 8, 5));
        }
        if (row == 1) {
            GUEST_PS(psq_st(10, 16, 5));
            GUEST_PS(psq_st(11, 24, 5));
        }
        if (row == 2) {
            GUEST_PS(psq_st(10, 32, 5));
            GUEST_PS(psq_st(11, 40, 5));
        }
    }
}

// r3 = character curves, r4 = character state {transforms, locals, worlds,
// skin scratch}, r5 = model {u16 parents[], inverse binds}, r6 = palette,
// f1 = frame.
inline void skeleton_update(CPUState* cpu) {
    G_PROLOGUE(64, 8);
    G_STFD(31, 16, 1);
    G_R(31) = G_R(3);
    G_R(30) = G_R(4);
    G_R(29) = G_R(5);
    G_R(28) = G_R(6);
    G_FMR(31, 1);
    const uint32_t locals = kBones * kTransformBytes, worlds = locals + kBones * kMatrixBytes,
                   scratch = worlds + kBones * kMatrixBytes;
    const uint32_t inverse = (kBones * 2 + 15) & ~15u;
    for (G_R(27) = 0; G_R(27) < kBones; ++G_R(27)) {
        G_R(26) = G_R(30) + G_R(27) * kTransformBytes;
        G_R(25) = G_R(27) * kMatrixBytes;
        G_R(3) = G_R(31) + G_R(27) * kBoneCurveBytes;
        G_R(4) = G_R(26);
        G_FMR(1, 31);
        cpu->lr = 0x00008200u;
        bone_animate(cpu);
        G_R(3) = G_R(26);
        G_R(4) = G_R(30) + locals + G_R(25);
        cpu->lr = 0x00008204u;
        transform_to_matrix(cpu);
        G_R(0) = G_R(27) << 1;
        G_R(24) = rbrew_read16(cpu->mem, G_R(29) + G_R(0));
        if (G_R(24) == 0xFFFFu) {
            for (uint32_t w = 0; w < 12; ++w) {
                G_LWZ(0, w * 4, 4);
                rbrew_write32(cpu->mem, G_R(30) + worlds + G_R(25) + w * 4, G_R(0));
            }
        } else {
            G_R(3) = G_R(30) + worlds + G_R(24) * kMatrixBytes;
            G_R(5) = G_R(30) + worlds + G_R(25);
            cpu->lr = 0x00008208u;
            matrix_concat(cpu);
        }
        if (G_R(27) < kPaletteBones) {
            G_R(3) = G_R(30) + worlds + G_R(25);
            G_R(4) = G_R(29) + inverse + G_R(25);
            G_R(5) = G_R(30) + scratch;
            cpu->lr = 0x0000820Cu;
            matrix_concat(cpu);
            G_R(7) = G_R(28) + G_R(25);
            for (uint32_t w = 0; w < 12; ++w) {
                G_LWZ(0, w * 4, 5);
                G_R(8) = w * 4;
                G_STWBRX(0, 7, 8);
            }
        }
    }
    G_LFD(31, 16, 1);
    G_EPILOGUE(64, 8);
}
} // namespace GUEST_SKELETON_NAMESPACE
