// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GXRUNTIME_CPU_FLOAT_INLINE_H
#define GXRUNTIME_CPU_FLOAT_INLINE_H

/* Header-only fast paths of the instruction-shaped FP / paired-single unit.
 *
 * Generated code calls ppc_fmuls(), ppc_fcmp(), ... once per guest FP
 * instruction. Out of line, every call forces the C compiler to spill guest
 * state around it. This header defines each of those as an always-inline
 * `<name>_i` and (unless GXRUNTIME_NO_FP_INLINE_ALIASES is defined) maps
 * `<name>` onto it with an object-like macro. cpu_interpreter_float.c defines
 * the real out-of-line symbols as one-line wrappers around the same inline
 * bodies, so there is ONE implementation of the common path.
 *
 * Rarely taken paths stay out of line behind __builtin_expect: NaN results,
 * FPSCR exception raising (set_fp_exception), denormal frC rounding, and
 * estimate tables. Semantics are bit-identical to the former out-of-line code
 * (verified by tools/fp_inline_test style harness, see the session notes). */

#include <math.h>
#include "types.h"

#if defined(__GNUC__) || defined(__clang__)
#define FPI_INLINE static inline __attribute__((always_inline))
#define FPI_LIKELY(x) __builtin_expect(!!(x), 1)
#define FPI_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define FPI_INLINE static inline
#define FPI_LIKELY(x) (x)
#define FPI_UNLIKELY(x) (x)
#endif

// IBM to LSB bit masks
#define FPSCR_FX_BIT     0x80000000u
#define FPSCR_FEX_BIT    0x40000000u
#define FPSCR_VX_BIT     0x20000000u
#define FPSCR_OX_BIT     0x10000000u
#define FPSCR_UX_BIT     0x08000000u
#define FPSCR_ZX_BIT     0x04000000u
#define FPSCR_XX_BIT     0x02000000u
#define FPSCR_VXSNAN_BIT 0x01000000u
#define FPSCR_VXISI_BIT  0x00800000u
#define FPSCR_VXIDI_BIT  0x00400000u
#define FPSCR_VXZDZ_BIT  0x00200000u
#define FPSCR_VXIMZ_BIT  0x00100000u
#define FPSCR_VXVC_BIT   0x00080000u
#define FPSCR_FR_BIT     0x00040000u
#define FPSCR_FI_BIT     0x00020000u
#define FPSCR_C_BIT      0x00010000u
#define FPSCR_FPCC_MASK  0x0000F000u
#define FPSCR_VXSOFT_BIT 0x00000400u
#define FPSCR_VXSQRT_BIT 0x00000200u
#define FPSCR_VXCVI_BIT  0x00000100u
#define FPSCR_VE_BIT     0x00000080u
#define FPSCR_OE_BIT     0x00000040u
#define FPSCR_UE_BIT     0x00000020u
#define FPSCR_ZE_BIT     0x00000010u
#define FPSCR_XE_BIT     0x00000008u
#define FPSCR_NI_BIT     0x00000004u
#define FPSCR_RN_MASK    0x00000003u
#define FPSCR_VX_ANY_MASK (FPSCR_VXSNAN_BIT | FPSCR_VXISI_BIT | FPSCR_VXIDI_BIT | \
                           FPSCR_VXZDZ_BIT | FPSCR_VXIMZ_BIT | FPSCR_VXVC_BIT | \
                           FPSCR_VXSOFT_BIT | FPSCR_VXSQRT_BIT | FPSCR_VXCVI_BIT)
#define FPSCR_ANY_X_MASK (FPSCR_OX_BIT | FPSCR_UX_BIT | FPSCR_ZX_BIT | FPSCR_XX_BIT | \
                          FPSCR_VX_ANY_MASK)
#define PPC_F64_QNAN_BITS 0x7FF8000000000000ull

typedef struct {
    f64 value;
    u32 exception;
} FPRes;

#ifdef __cplusplus
extern "C" {
#endif

/* Out-of-line slow paths / reference implementations (cpu_interpreter_float.c). */
void set_fp_exception(CPUState* cpu, u32 bit);
f64 force_25bit_c(f64 d);
f64 force_25_bit(f64 d);
FPRes ni_add(CPUState* cpu, f64 a, f64 b);
FPRes ni_sub(CPUState* cpu, f64 a, f64 b);
FPRes ni_mul(CPUState* cpu, f64 a, f64 b);
FPRes ni_div(CPUState* cpu, f64 a, f64 b);
FPRes ni_madd_msub(CPUState* cpu, f64 a, f64 c, f64 b, bool sub, bool single);
bool ppc_fma_nan(CPUState* cpu, f64 a, f64 c, f64 b, bool single, bool negative,
                 f64 result, f64* output);

#ifdef __cplusplus
}
#endif

/* ---- small primitives ------------------------------------------------- */

FPI_INLINE void fpi_clear_fifr(CPUState* cpu) {
    cpu->fpscr &= ~(FPSCR_FI_BIT | FPSCR_FR_BIT);
}

FPI_INLINE bool fpi_is_snan(f64 value) {
    u64 bits = f64_bits(value);
    u64 fraction = bits & 0x000FFFFFFFFFFFFFull;
    return (bits & 0x7FF0000000000000ull) == 0x7FF0000000000000ull &&
           fraction != 0 && (fraction & 0x0008000000000000ull) == 0;
}

FPI_INLINE u32 fpi_classify_f64(f64 value) {
    u64 bits = f64_bits(value);
    u64 sign = bits >> 63;
    u64 exponent = bits & 0x7FF0000000000000ull;
    u64 fraction = bits & 0x000FFFFFFFFFFFFFull;
    if (exponent == 0x7FF0000000000000ull)
        return fraction ? 0x11u : (sign ? 0x09u : 0x05u);
    if (exponent == 0)
        return fraction ? (sign ? 0x18u : 0x14u) : (sign ? 0x12u : 0x02u);
    return sign ? 0x08u : 0x04u;
}

FPI_INLINE u32 fpi_classify_f32(f32 value) {
    u32 bits;
    memcpy(&bits, &value, sizeof(bits));
    u32 sign = bits >> 31;
    u32 exponent = bits & 0x7F800000u;
    u32 fraction = bits & 0x007FFFFFu;
    if (exponent == 0x7F800000u)
        return fraction ? 0x11u : (sign ? 0x09u : 0x05u);
    if (exponent == 0)
        return fraction ? (sign ? 0x18u : 0x14u) : (sign ? 0x12u : 0x02u);
    return sign ? 0x08u : 0x04u;
}

/* GXRUNTIME_NO_FPRF: skip the FPSCR result-class bits, like Dolphin's JITs by default ("FPRF"
 * off); games that read FPRF enable it in their GameSettings ini. */
FPI_INLINE void fpi_set_fprf(CPUState* cpu, u32 value) {
#ifdef GXRUNTIME_NO_FPRF
    (void)cpu; (void)value;
#else
    cpu->fpscr = (cpu->fpscr & ~(0x1Fu << 12)) | ((value & 0x1Fu) << 12);
#endif
}

FPI_INLINE f64 fpi_make_quiet(f64 value) {
    return f64_value(f64_bits(value) | 0x0008000000000000ull);
}

FPI_INLINE f32 fpi_force_single(const CPUState* cpu, f64 value) {
    if (cpu->fpscr & FPSCR_NI_BIT) {
        u64 no_sign = f64_bits(value) & 0x7FFFFFFFFFFFFFFFull;
        if (no_sign < 0x3810000000000000ull) {
            u32 flushed = (u32)((f64_bits(value) & 0x8000000000000000ull) >> 32);
            return f32_value(flushed);
        }
    }
    return (f32)value;
}

/* Normal numbers and zero round with the same mask/add in both variants; the
 * denormal case differs between them, so each defers to its own out-of-line
 * version. */
FPI_INLINE f64 fpi_force_25bit_c(f64 d) {
    u64 integral = f64_bits(d);
    if (FPI_UNLIKELY((integral & 0x7FF0000000000000ull) == 0 &&
                     (integral & 0x000FFFFFFFFFFFFFull) != 0))
        return force_25bit_c(d);
    integral = (integral & 0xFFFFFFFFF8000000ull) + (integral & 0x8000000ull);
    return f64_value(integral);
}

FPI_INLINE f64 fpi_force_25_bit(f64 d) {
    u64 bits = f64_bits(d);
    if (FPI_UNLIKELY((bits & 0x7FF0000000000000ull) == 0 &&
                     (bits & 0x000FFFFFFFFFFFFFull) != 0))
        return force_25_bit(d);
    bits = (bits & 0xFFFFFFFFF8000000ull) + (bits & 0x0000000008000000ull);
    return f64_value(bits);
}

FPI_INLINE bool fpi_invalid_gated(const CPUState* cpu, const FPRes* res) {
    return (cpu->fpscr & FPSCR_VE_BIT) != 0 && (res->exception & FPSCR_VX_ANY_MASK) != 0;
}

FPI_INLINE void fpi_write_single(CPUState* cpu, u8 d, f32 rounded) {
    cpu->fpr[d] = (f64)rounded;
    cpu->ps1[d] = (f64)rounded;
    fpi_set_fprf(cpu, fpi_classify_f32(rounded));
}

FPI_INLINE void fpi_write_double(CPUState* cpu, u8 d, f64 value) {
    cpu->fpr[d] = value;
    fpi_set_fprf(cpu, fpi_classify_f64(value));
}

FPI_INLINE void fpi_ps_write_both(CPUState* cpu, u8 d, f32 ps0, f32 ps1) {
    cpu->fpr[d] = (f64)ps0;
    cpu->ps1[d] = (f64)ps1;
}

/* ---- NI_* arithmetic: fast path inline, NaN result via out-of-line ni_* ---- */

FPI_INLINE FPRes fpi_ni_add(CPUState* cpu, f64 a, f64 b) {
    FPRes result = {a + b, 0};
    if (FPI_UNLIKELY(isnan(result.value)))
        return ni_add(cpu, a, b);
    if (FPI_UNLIKELY(isinf(a) || isinf(b)))
        fpi_clear_fifr(cpu);
    return result;
}

FPI_INLINE FPRes fpi_ni_sub(CPUState* cpu, f64 a, f64 b) {
    FPRes result = {a - b, 0};
    if (FPI_UNLIKELY(isnan(result.value)))
        return ni_sub(cpu, a, b);
    if (FPI_UNLIKELY(isinf(a) || isinf(b)))
        fpi_clear_fifr(cpu);
    return result;
}

FPI_INLINE FPRes fpi_ni_mul(CPUState* cpu, f64 a, f64 b) {
    FPRes result = {a * b, 0};
    if (FPI_UNLIKELY(isnan(result.value)))
        return ni_mul(cpu, a, b);
    return result;
}

FPI_INLINE FPRes fpi_ni_div(CPUState* cpu, f64 a, f64 b) {
    FPRes result = {a / b, 0};
    if (FPI_UNLIKELY(isinf(result.value) || isnan(result.value)))
        return ni_div(cpu, a, b);
    return result;
}

/* Single-precision fused multiply-add with the Gekko round-to-single fixup. */
FPI_INLINE FPRes fpi_ni_madd_msub(CPUState* cpu, f64 a, f64 c, f64 b, bool sub, bool single) {
    FPRes result = {0.0, 0};

    if (!single) {
        result.value = fma(a, c, sub ? -b : b);
    } else {
        f64 c_round = fpi_force_25bit_c(c);
        f64 b_sign = sub ? -b : b;
        result.value = fma(a, c_round, b_sign);

        u64 result_bits = f64_bits(result.value);
        if (FPI_UNLIKELY((result_bits & 0x000000001FFFFFFFull) == 0x0000000010000000ull)) {
            f64 a_prime = b_sign - result.value;
            f64 b_prime = result.value + a_prime;
            f64 delta_a = fma(a, c_round, a_prime);
            f64 delta_b = b_sign - b_prime;
            f64 error = delta_a + delta_b;
            if (error != 0.0) {
                if ((error > 0.0) == (result.value > 0.0))
                    result.value = f64_value(result_bits + 1);
                else
                    result.value = f64_value(result_bits - 1);
            }
        }
    }

    if (FPI_UNLIKELY(isnan(result.value)))
        return ni_madd_msub(cpu, a, c, b, sub, single);
    if (FPI_UNLIKELY(isinf(a) || isinf(b) || isinf(c)))
        fpi_clear_fifr(cpu);
    return result;
}

/* ---- scalar single / double arithmetic -------------------------------- */

FPI_INLINE void ppc_fadds_i(CPUState* cpu, u8 d, u8 a, u8 b) {
    FPRes sum = fpi_ni_add(cpu, cpu->fpr[a], cpu->fpr[b]);
    if (!fpi_invalid_gated(cpu, &sum))
        fpi_write_single(cpu, d, fpi_force_single(cpu, sum.value));
}

FPI_INLINE void ppc_fsubs_i(CPUState* cpu, u8 d, u8 a, u8 b) {
    FPRes diff = fpi_ni_sub(cpu, cpu->fpr[a], cpu->fpr[b]);
    if (!fpi_invalid_gated(cpu, &diff))
        fpi_write_single(cpu, d, fpi_force_single(cpu, diff.value));
}

FPI_INLINE void ppc_fmuls_i(CPUState* cpu, u8 d, u8 a, u8 c) {
    f64 c_value = fpi_force_25bit_c(cpu->fpr[c]);
    FPRes product = fpi_ni_mul(cpu, cpu->fpr[a], c_value);
    if (!fpi_invalid_gated(cpu, &product)) {
        fpi_write_single(cpu, d, fpi_force_single(cpu, product.value));
        cpu->fpscr &= ~(FPSCR_FI_BIT | FPSCR_FR_BIT);
    }
}

FPI_INLINE void ppc_fdivs_i(CPUState* cpu, u8 d, u8 a, u8 b) {
    FPRes quotient = fpi_ni_div(cpu, cpu->fpr[a], cpu->fpr[b]);
    bool not_divide_by_zero =
        (cpu->fpscr & FPSCR_ZE_BIT) == 0 || quotient.exception != FPSCR_ZX_BIT;
    if (not_divide_by_zero && !fpi_invalid_gated(cpu, &quotient))
        fpi_write_single(cpu, d, fpi_force_single(cpu, quotient.value));
}

FPI_INLINE void ppc_fadd_i(CPUState* cpu, u8 d, u8 a, u8 b) {
    FPRes sum = fpi_ni_add(cpu, cpu->fpr[a], cpu->fpr[b]);
    if (!fpi_invalid_gated(cpu, &sum))
        fpi_write_double(cpu, d, sum.value);
}

FPI_INLINE void ppc_fsub_i(CPUState* cpu, u8 d, u8 a, u8 b) {
    FPRes diff = fpi_ni_sub(cpu, cpu->fpr[a], cpu->fpr[b]);
    if (!fpi_invalid_gated(cpu, &diff))
        fpi_write_double(cpu, d, diff.value);
}

FPI_INLINE void ppc_fmul_i(CPUState* cpu, u8 d, u8 a, u8 c) {
    FPRes product = fpi_ni_mul(cpu, cpu->fpr[a], cpu->fpr[c]);
    if (!fpi_invalid_gated(cpu, &product)) {
        fpi_write_double(cpu, d, product.value);
        cpu->fpscr &= ~(FPSCR_FI_BIT | FPSCR_FR_BIT);
    }
}

FPI_INLINE void ppc_fdiv_i(CPUState* cpu, u8 d, u8 a, u8 b) {
    FPRes quotient = fpi_ni_div(cpu, cpu->fpr[a], cpu->fpr[b]);
    bool not_divide_by_zero =
        (cpu->fpscr & FPSCR_ZE_BIT) == 0 || quotient.exception != FPSCR_ZX_BIT;
    if (not_divide_by_zero && !fpi_invalid_gated(cpu, &quotient))
        fpi_write_double(cpu, d, quotient.value);
}

/* ppc_fma: fmadd/fmsub/fnmadd/fnmsub (+ s forms). NaN results (all the
 * exception logic) are handled by ppc_fma_nan(). */
FPI_INLINE bool ppc_fma_i(CPUState* cpu, f64 a, f64 c, f64 b, bool single,
                          bool subtract, bool negative, f64* output) {
    f64 addend = subtract ? -b : b;
    f64 result;

    if (!single) {
        result = fma(a, c, addend);
    } else {
        f64 rounded_c = fpi_force_25_bit(c);
        result = fma(a, rounded_c, addend);
        u64 bits = f64_bits(result);
        if (FPI_UNLIKELY((bits & 0x000000001FFFFFFFull) == 0x0000000010000000ull)) {
            f64 a_prime = addend - result;
            f64 b_prime = result + a_prime;
            f64 error = fma(a, rounded_c, a_prime) + (addend - b_prime);
            if (error != 0.0) {
                if ((error > 0.0) == (result > 0.0)) bits++;
                else bits--;
                result = f64_value(bits);
            }
        }
        result = (f64)(f32)result;
    }

    if (FPI_UNLIKELY(isnan(result)))
        return ppc_fma_nan(cpu, a, c, b, single, negative, result, output);
    if (FPI_UNLIKELY(isinf(a) || isinf(b) || isinf(c)))
        fpi_clear_fifr(cpu);

    if (negative)
        result = -result;
    fpi_set_fprf(cpu, single ? fpi_classify_f32((f32)result) : fpi_classify_f64(result));
    *output = result;
    return true;
}

FPI_INLINE void ppc_frsp_i(CPUState* cpu, u8 d, u8 b) {
    f64 value = cpu->fpr[b];
    f32 rounded = fpi_force_single(cpu, value);

    if (FPI_UNLIKELY(isnan(value))) {
        bool snan = fpi_is_snan(value);
        if (snan)
            set_fp_exception(cpu, FPSCR_VXSNAN_BIT);
        if (!snan || (cpu->fpscr & FPSCR_VE_BIT) == 0) {
            cpu->fpr[d] = (f64)rounded;
            cpu->ps1[d] = (f64)rounded;
            fpi_set_fprf(cpu, fpi_classify_f32(rounded));
        }
        fpi_clear_fifr(cpu);
    } else {
        if (value != (f64)rounded) {
            set_fp_exception(cpu, FPSCR_XX_BIT);
            cpu->fpscr |= FPSCR_FI_BIT;
        } else {
            cpu->fpscr &= ~FPSCR_FI_BIT;
        }
        cpu->fpscr = (cpu->fpscr & ~FPSCR_FR_BIT) |
                     ((fabs((f64)rounded) > fabs(value)) ? FPSCR_FR_BIT : 0u);
        fpi_set_fprf(cpu, fpi_classify_f32(rounded));
        cpu->fpr[d] = (f64)rounded;
        cpu->ps1[d] = (f64)rounded;
    }
}

FPI_INLINE void ppc_fcmp_i(CPUState* cpu, u8 crfd, f64 a, f64 b, bool ordered) {
    u32 compare;

    if (FPI_UNLIKELY(isnan(a) || isnan(b))) {
        compare = 0x1u;
        if (fpi_is_snan(a) || fpi_is_snan(b)) {
            set_fp_exception(cpu, FPSCR_VXSNAN_BIT);
            if (ordered && (cpu->fpscr & FPSCR_VE_BIT) == 0)
                set_fp_exception(cpu, FPSCR_VXVC_BIT);
        } else if (ordered) {
            set_fp_exception(cpu, FPSCR_VXVC_BIT);
        }
    } else if (a < b) {
        compare = 0x8u;
    } else if (a > b) {
        compare = 0x4u;
    } else {
        compare = 0x2u;
    }

    cpu->fpscr = (cpu->fpscr & ~(0xFu << 12)) | (compare << 12);
    u32 shift = 4u * (7u - crfd);
    cpu->cr = (cpu->cr & ~(0xFu << shift)) | (compare << shift);
}

/* ---- fres / frsqrte: table lookups stay out of line ---------------------- */

FPI_INLINE bool ppc_fres_i(CPUState* cpu, f64 value, f64* result) {
    if (FPI_UNLIKELY(value == 0.0)) {
        set_fp_exception(cpu, 0x04000000u);
        fpi_clear_fifr(cpu);
        if (cpu->fpscr & 0x10u)
            return false;
    } else if (FPI_UNLIKELY(fpi_is_snan(value))) {
        set_fp_exception(cpu, 0x01000000u);
        fpi_clear_fifr(cpu);
        if (cpu->fpscr & 0x80u)
            return false;
    } else if (FPI_UNLIKELY(isnan(value) || isinf(value))) {
        fpi_clear_fifr(cpu);
    }

    *result = ppc_approx_reciprocal(value);
    fpi_set_fprf(cpu, fpi_classify_f32((f32)*result));
    return true;
}

FPI_INLINE bool ppc_frsqrte_i(CPUState* cpu, f64 value, f64* result) {
    if (FPI_UNLIKELY(value < 0.0)) {
        set_fp_exception(cpu, 0x00000200u);
        fpi_clear_fifr(cpu);
        if (cpu->fpscr & 0x80u)
            return false;
    } else if (FPI_UNLIKELY(value == 0.0)) {
        set_fp_exception(cpu, 0x04000000u);
        fpi_clear_fifr(cpu);
        if (cpu->fpscr & 0x10u)
            return false;
    } else if (FPI_UNLIKELY(fpi_is_snan(value))) {
        set_fp_exception(cpu, 0x01000000u);
        fpi_clear_fifr(cpu);
        if (cpu->fpscr & 0x80u)
            return false;
    } else if (FPI_UNLIKELY(isnan(value) || isinf(value))) {
        fpi_clear_fifr(cpu);
    }

    *result = ppc_approx_rsqrt(value);
    fpi_set_fprf(cpu, fpi_classify_f64(*result));
    return true;
}

/* fctiw/fctiwz: the (rare) invalid / inexact bookkeeping is out of line. */
bool ppc_fctiw(CPUState* cpu, f64 value, bool toward_zero, u64* output);

FPI_INLINE bool ppc_fctiw_i(CPUState* cpu, f64 value, bool toward_zero, u64* output) {
    /* Exact in-range, integral-valued input (no flags touched except
     * clear_fifr) is the common case after a guest floor/trunc; everything
     * else, including rounding, goes through the full routine. */
    if (FPI_LIKELY(toward_zero && value > -2147483648.0 && value < 2147483648.0)) {
        f64 rounded = trunc(value);
        if (FPI_LIKELY(rounded == value)) {
            u32 result = (u32)(s32)rounded;
            fpi_clear_fifr(cpu);
            *output = 0xFFF8000000000000ull | result |
                      ((result == 0 && signbit(value)) ? 0x100000000ull : 0ull);
            return true;
        }
    }
    return ppc_fctiw(cpu, value, toward_zero, output);
}

/* ---- paired single ------------------------------------------------------ */

FPI_INLINE void ppc_ps_add_op_i(CPUState* cpu, u8 d, u8 a, u8 b) {
    f32 ps0 = fpi_force_single(cpu, fpi_ni_add(cpu, cpu->fpr[a], cpu->fpr[b]).value);
    f32 ps1 = fpi_force_single(cpu, fpi_ni_add(cpu, cpu->ps1[a], cpu->ps1[b]).value);
    fpi_ps_write_both(cpu, d, ps0, ps1);
    fpi_set_fprf(cpu, fpi_classify_f32(ps0));
}

FPI_INLINE void ppc_ps_sub_op_i(CPUState* cpu, u8 d, u8 a, u8 b) {
    f32 ps0 = fpi_force_single(cpu, fpi_ni_sub(cpu, cpu->fpr[a], cpu->fpr[b]).value);
    f32 ps1 = fpi_force_single(cpu, fpi_ni_sub(cpu, cpu->ps1[a], cpu->ps1[b]).value);
    fpi_ps_write_both(cpu, d, ps0, ps1);
    fpi_set_fprf(cpu, fpi_classify_f32(ps0));
}

FPI_INLINE void ppc_ps_mul_op_i(CPUState* cpu, u8 d, u8 a, u8 c) {
    f64 c0 = fpi_force_25bit_c(cpu->fpr[c]);
    f64 c1 = fpi_force_25bit_c(cpu->ps1[c]);
    f32 ps0 = fpi_force_single(cpu, fpi_ni_mul(cpu, cpu->fpr[a], c0).value);
    f32 ps1 = fpi_force_single(cpu, fpi_ni_mul(cpu, cpu->ps1[a], c1).value);
    fpi_ps_write_both(cpu, d, ps0, ps1);
    fpi_set_fprf(cpu, fpi_classify_f32(ps0));
}

FPI_INLINE void ppc_ps_div_op_i(CPUState* cpu, u8 d, u8 a, u8 b) {
    f32 ps0 = fpi_force_single(cpu, fpi_ni_div(cpu, cpu->fpr[a], cpu->fpr[b]).value);
    f32 ps1 = fpi_force_single(cpu, fpi_ni_div(cpu, cpu->ps1[a], cpu->ps1[b]).value);
    fpi_ps_write_both(cpu, d, ps0, ps1);
    fpi_set_fprf(cpu, fpi_classify_f32(ps0));
}

FPI_INLINE void ppc_ps_madd_op_i(CPUState* cpu, u8 d, u8 a, u8 c, u8 b,
                                 bool subtract, bool negative) {
    f32 tmp0 = fpi_force_single(
        cpu, fpi_ni_madd_msub(cpu, cpu->fpr[a], cpu->fpr[c], cpu->fpr[b], subtract, true).value);
    f32 tmp1 = fpi_force_single(
        cpu, fpi_ni_madd_msub(cpu, cpu->ps1[a], cpu->ps1[c], cpu->ps1[b], subtract, true).value);
    f32 ps0 = (negative && !isnan(tmp0)) ? -tmp0 : tmp0;
    f32 ps1 = (negative && !isnan(tmp1)) ? -tmp1 : tmp1;
    fpi_ps_write_both(cpu, d, ps0, ps1);
    fpi_set_fprf(cpu, fpi_classify_f32(ps0));
}

FPI_INLINE void ppc_ps_madds0_i(CPUState* cpu, u8 d, u8 a, u8 c, u8 b) {
    f32 ps0 = fpi_force_single(
        cpu, fpi_ni_madd_msub(cpu, cpu->fpr[a], cpu->fpr[c], cpu->fpr[b], false, true).value);
    f32 ps1 = fpi_force_single(
        cpu, fpi_ni_madd_msub(cpu, cpu->ps1[a], cpu->fpr[c], cpu->ps1[b], false, true).value);
    fpi_ps_write_both(cpu, d, ps0, ps1);
    fpi_set_fprf(cpu, fpi_classify_f32(ps0));
}

FPI_INLINE void ppc_ps_madds1_i(CPUState* cpu, u8 d, u8 a, u8 c, u8 b) {
    f32 ps0 = fpi_force_single(
        cpu, fpi_ni_madd_msub(cpu, cpu->fpr[a], cpu->ps1[c], cpu->fpr[b], false, true).value);
    f32 ps1 = fpi_force_single(
        cpu, fpi_ni_madd_msub(cpu, cpu->ps1[a], cpu->ps1[c], cpu->ps1[b], false, true).value);
    fpi_ps_write_both(cpu, d, ps0, ps1);
    fpi_set_fprf(cpu, fpi_classify_f32(ps0));
}

FPI_INLINE void ppc_ps_sum0_i(CPUState* cpu, u8 d, u8 a, u8 c, u8 b) {
    f32 ps0 = fpi_force_single(cpu, fpi_ni_add(cpu, cpu->fpr[a], cpu->ps1[b]).value);
    f32 ps1 = fpi_force_single(cpu, cpu->ps1[c]);
    fpi_ps_write_both(cpu, d, ps0, ps1);
    fpi_set_fprf(cpu, fpi_classify_f32(ps0));
}

FPI_INLINE void ppc_ps_sum1_i(CPUState* cpu, u8 d, u8 a, u8 c, u8 b) {
    f32 ps0 = fpi_force_single(cpu, cpu->fpr[c]);
    f32 ps1 = fpi_force_single(cpu, fpi_ni_add(cpu, cpu->fpr[a], cpu->ps1[b]).value);
    fpi_ps_write_both(cpu, d, ps0, ps1);
    fpi_set_fprf(cpu, fpi_classify_f32(ps1));
}

FPI_INLINE void ppc_ps_muls0_i(CPUState* cpu, u8 d, u8 a, u8 c) {
    f64 c0 = fpi_force_25bit_c(cpu->fpr[c]);
    f32 ps0 = fpi_force_single(cpu, fpi_ni_mul(cpu, cpu->fpr[a], c0).value);
    f32 ps1 = fpi_force_single(cpu, fpi_ni_mul(cpu, cpu->ps1[a], c0).value);
    fpi_ps_write_both(cpu, d, ps0, ps1);
    fpi_set_fprf(cpu, fpi_classify_f32(ps0));
}

FPI_INLINE void ppc_ps_muls1_i(CPUState* cpu, u8 d, u8 a, u8 c) {
    f64 c1 = fpi_force_25bit_c(cpu->ps1[c]);
    f32 ps0 = fpi_force_single(cpu, fpi_ni_mul(cpu, cpu->fpr[a], c1).value);
    f32 ps1 = fpi_force_single(cpu, fpi_ni_mul(cpu, cpu->ps1[a], c1).value);
    fpi_ps_write_both(cpu, d, ps0, ps1);
    fpi_set_fprf(cpu, fpi_classify_f32(ps0));
}

/* ---- name aliases: calls to ppc_X() in generated code resolve to ppc_X_i() */
#if defined(GXRUNTIME_FP_INLINE_HOT) && !defined(GXRUNTIME_NO_FP_INLINE_ALIASES)
/* Only the hottest helpers (iPad profile of The Sims 2 Pets): the paired-single ops have few call
 * sites but run in inner loops; fmuls/fadds are the most frequent scalar ops. Inlining everything
 * grew the code enough to cancel out on the A14. */
#define ppc_fadds ppc_fadds_i
#define ppc_fmuls ppc_fmuls_i
#define ppc_ps_add_op ppc_ps_add_op_i
#define ppc_ps_sub_op ppc_ps_sub_op_i
#define ppc_ps_mul_op ppc_ps_mul_op_i
#define ppc_ps_madd_op ppc_ps_madd_op_i
#define ppc_ps_madds0 ppc_ps_madds0_i
#define ppc_ps_madds1 ppc_ps_madds1_i
#define ppc_ps_sum0 ppc_ps_sum0_i
#define ppc_ps_sum1 ppc_ps_sum1_i
#define ppc_ps_muls0 ppc_ps_muls0_i
#define ppc_ps_muls1 ppc_ps_muls1_i
#elif !defined(GXRUNTIME_NO_FP_INLINE_ALIASES)
#define ppc_fadds ppc_fadds_i
#define ppc_fsubs ppc_fsubs_i
#define ppc_fmuls ppc_fmuls_i
#define ppc_fdivs ppc_fdivs_i
#define ppc_fadd ppc_fadd_i
#define ppc_fsub ppc_fsub_i
#define ppc_fmul ppc_fmul_i
#define ppc_fdiv ppc_fdiv_i
#define ppc_fma ppc_fma_i
#define ppc_frsp ppc_frsp_i
#define ppc_fcmp ppc_fcmp_i
#define ppc_fres ppc_fres_i
#define ppc_frsqrte ppc_frsqrte_i
#define ppc_fctiw ppc_fctiw_i
#define ppc_ps_add_op ppc_ps_add_op_i
#define ppc_ps_sub_op ppc_ps_sub_op_i
#define ppc_ps_mul_op ppc_ps_mul_op_i
#define ppc_ps_div_op ppc_ps_div_op_i
#define ppc_ps_madd_op ppc_ps_madd_op_i
#define ppc_ps_madds0 ppc_ps_madds0_i
#define ppc_ps_madds1 ppc_ps_madds1_i
#define ppc_ps_sum0 ppc_ps_sum0_i
#define ppc_ps_sum1 ppc_ps_sum1_i
#define ppc_ps_muls0 ppc_ps_muls0_i
#define ppc_ps_muls1 ppc_ps_muls1_i
#endif

#endif /* GXRUNTIME_CPU_FLOAT_INLINE_H */
