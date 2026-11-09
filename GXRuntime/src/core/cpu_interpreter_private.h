#ifndef GXRUNTIME_CPU_INTERPRETER_PRIVATE_H
#define GXRUNTIME_CPU_INTERPRETER_PRIVATE_H

#include "core/cpu.h"
#include <math.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <stdatomic.h>
#endif

typedef struct {
    s32 base;
    s32 dec;
} EstimateEntry;

extern const EstimateEntry frsqrte_table[32];
extern const EstimateEntry fres_table[32];

f64 ppc_approx_rsqrt(f64 value);
f64 ppc_approx_reciprocal(f64 value);
void ppc_fpscr_updated(CPUState* cpu);
void ppc_fpscr_control_updated(CPUState* cpu);
void set_fp_exception(CPUState* cpu, u32 bit);
void clear_fifr(CPUState* cpu);
bool is_snan(f64 value);
u32 classify_f64(f64 value);
u32 classify_f32(f32 value);
void set_fprf(CPUState* cpu, u32 value);
f32 force_single(const CPUState* cpu, f64 value);
f64 force_double(const CPUState* cpu, f64 d);
f64 force_25bit_c(f64 d);
f64 make_quiet(f64 value);
unsigned leading_zeroes_u64(u64 value);

FPRes ni_add(CPUState* cpu, f64 a, f64 b);
FPRes ni_sub(CPUState* cpu, f64 a, f64 b);
FPRes ni_mul(CPUState* cpu, f64 a, f64 b);
FPRes ni_div(CPUState* cpu, f64 a, f64 b);
FPRes ni_madd_msub(CPUState* cpu, f64 a, f64 c, f64 b, bool sub, bool single);
bool fp_invalid_gated(const CPUState* cpu, const FPRes* res);
void fp_write_single(CPUState* cpu, u8 d, f32 rounded);
void fp_write_double(CPUState* cpu, u8 d, f64 value);
void ps_write_both(CPUState* cpu, u8 d, f32 ps0, f32 ps1);

#endif /* GXRUNTIME_CPU_INTERPRETER_PRIVATE_H */
