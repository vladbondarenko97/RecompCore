// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DOLRECOMP_CPU_H
#define DOLRECOMP_CPU_H

#include "types.h"
#ifdef __cplusplus
extern "C" {
#endif
// more msvc shit fuck msvc
#if defined(_MSC_VER)
#define GXRUNTIME_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define GXRUNTIME_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define GXRUNTIME_ALWAYS_INLINE inline
#endif

// Minimal CPU support ABI for generated code and CPU tests.
//
// ABI status:
// - Fields up through `ram_size` intentionally mirror DolRecomp's current
//   generated-code CPUState contract.
// - GXRuntime extends that contract only at the tail with `external_pointer`,
//   used as a fast path for host-backed external memory such as locked cache.
// - Do not insert fields into the mirrored prefix without coordinating a
//   generated-code ABI bump with DolRecomp.
// - ABI v2 adds `downcount` at the tail: a guest-cycle charge accumulator.
//   Generated code subtracts each basic block's Gekko cycle cost (mirroring
//   Dolphin's PPCTables costs) at block entry; the embedding environment
//   consumes and resets it (Dolphin chassis: per-dispatch flush into
//   ppc_state.downcount). Hosts that do not meter guest time may ignore it
//   (s64: it cannot wrap in any realistic session).
// - ABI v4 adds `cycle_budget` directly after `downcount`, mirroring the
//   field DolRecomp added to its generated-code CPUState in f0a86be. The
//   prefix is shared with generated code, so the field must sit at the same
//   offset on both sides; hosts that do not meter guest time may ignore it.
#define GXRUNTIME_CPU_ABI_VERSION 4u
#define GXRUNTIME_CPU_ABI_DOLRECOMP_PREFIX 1u
#define GXRUNTIME_CPU_ABI_EXTERNAL_POINTER_EXTENSION 1u

#define GC_MAIN_RAM_SIZE    (24 * 1024 * 1024)
#define GC_RAM_BASE         0x80000000u
#define GC_RAM_UNCACHED     0xC0000000u

#define PPC_EXC_PROGRAM        0x00000001u
#define PPC_EXC_DSI            0x00000002u
#define PPC_EXC_ALIGNMENT      0x00000004u
#define PPC_EXC_SYSTEM_CALL    0x00000008u
#define PPC_EXC_MACHINE_CHECK  0x00000010u
#define PPC_EXC_FP_UNAVAILABLE 0x00000020u

#define PPC_PROGRAM_FP        0x00100000u
#define PPC_PROGRAM_ILLEGAL   0x00080000u
#define PPC_PROGRAM_PRIV      0x00040000u
#define PPC_PROGRAM_TRAP      0x00020000u

#define PPC_DSI_EAR_DISABLED  0x00100000u

#define PPC_VECTOR_MACHINE_CHECK  0x00200u
#define PPC_VECTOR_DSI            0x00300u
#define PPC_VECTOR_ALIGNMENT      0x00600u
#define PPC_VECTOR_PROGRAM        0x00700u
#define PPC_VECTOR_FP_UNAVAILABLE 0x00800u
#define PPC_VECTOR_SYSTEM_CALL    0x00C00u

#define PPC_HID2_LSQE   0x80000000u
#define PPC_HID2_PSE    0x20000000u
#define PPC_HID2_LCE    0x10000000u
#define PPC_HID2_DCHERR 0x00800000u
#define PPC_HID2_DCHEE  0x00080000u

#define PPC_BIT(n) (1u << (31u - (n)))
#define PPC_MSR_RFI_MASK 0x87C0FFFFu
#define PPC_MSR_POW PPC_BIT(13)
#define PPC_MSR_ILE PPC_BIT(15)
#define PPC_MSR_EE  PPC_BIT(16)
#define PPC_MSR_PR  PPC_BIT(17)
#define PPC_MSR_FP  PPC_BIT(18)
#define PPC_MSR_ME  PPC_BIT(19)
#define PPC_MSR_FE0 PPC_BIT(20)
#define PPC_MSR_SE  PPC_BIT(21)
#define PPC_MSR_BE  PPC_BIT(22)
#define PPC_MSR_FE1 PPC_BIT(23)
#define PPC_MSR_IP  PPC_BIT(25)
#define PPC_MSR_IR  PPC_BIT(26)
#define PPC_MSR_DR  PPC_BIT(27)
#define PPC_MSR_PM  PPC_BIT(29)
#define PPC_MSR_RI  PPC_BIT(30)
#define PPC_MSR_LE  PPC_BIT(31)

#define PPC_EAR_ENABLE 0x80000000u
#define PPC_SRR1_MACHINE_CHECK_DCBZL PPC_BIT(10)

typedef struct CPUState CPUState;

typedef u64 (*PPCExternalRead)(CPUState* cpu, u32 ea, u8 size);
typedef void (*PPCExternalWrite)(CPUState* cpu, u32 ea, u64 value, u8 size);
typedef u32 (*PPCExternalRead32)(CPUState* cpu, u32 ea, u8 rid);
typedef void (*PPCExternalWrite32)(CPUState* cpu, u32 ea, u32 value, u8 rid);
typedef void* (*PPCExternalPointer)(CPUState* cpu, u32 ea, u32 size);
typedef void (*PPCInstructionFallback)(CPUState* cpu, u32 raw, u32 cia);
typedef bool (*PPCHostCall)(CPUState* cpu, u32 address);
typedef u32 (*PPCSPRRead)(CPUState* cpu, u16 spr, u32 cia);
typedef void (*PPCSPRWrite)(CPUState* cpu, u16 spr, u32 value, u32 cia);
typedef void (*PPCCacheControl)(CPUState* cpu, u8 operation, u32 ea, u32 cia);

enum {
    PPC_CACHE_DCBST,
    PPC_CACHE_DCBF,
    PPC_CACHE_DCBI,
    PPC_CACHE_ICBI,
};

struct CPUState {
    u32 gpr[32];
    f64 fpr[32];
    f64 ps1[32];
    u32 pc;
    u32 lr;
    u32 ctr;
    u32 cr;
    u32 xer;
    u32 fpscr;
    u32 msr;
    u32 srr0;
    u32 srr1;
    u32 dar;
    u32 dsisr;
    u32 ear;
    u32 hid2;
    u64 timebase;
    u32 sr[16];
    u32 gqr[8];
    u32 exception;
    u32 program_exception;
    u32 tlb_last_vps;
    u32 tlb_last_index;
    u32 tlb_invalidate_count;
    u32 external_addr;
    u32 external_value;
    u8 external_rid;
    u8 external_read_count;
    u8 external_write_count;
    u32 reserve_addr;
    bool reserve_valid;
    u32 locked_cache_tag[512];
    bool locked_cache_valid[512];
    PPCExternalRead external_read;
    PPCExternalWrite external_write;
    PPCExternalRead32 external_read32;
    PPCExternalWrite32 external_write32;
    PPCInstructionFallback instruction_fallback;
    PPCHostCall host_call;
    void* external_user_data;

    u8* ram;
    u32 ram_size;
    PPCExternalPointer external_pointer;
    s64 downcount;
    s64 cycle_budget;
    u8* exram;
    u32 exram_size;
    PPCSPRRead spr_read;
    PPCSPRWrite spr_write;
    PPCCacheControl cache_control;
    struct GXVTlbEntry* vtlb;  /* 2 * GX_VTLB_SIZE entries (reads, then writes), or NULL */
    u32 vtlb_epoch;            /* bumped on tlbie / SDR1 / BAT changes / state loads */
    /* Gather pipe (0xCC008000) fast path: Dolphin's PowerPCState gather_pipe_ptr/base_ptr. */
    u8** gp_ptr;
    u8** gp_base;
    /* Cross-chunk call check cache: one byte per 16 KiB chunk slot from native_base, set by the
     * runtime once a chunk may be entered natively (verified, no mod hook), cleared on any change. */
    const u8* native_ok;
    u32 native_base;
    u32 native_slots;
    u32 native_idle_pc;
};

/* Software TLB for page-table-translated guest addresses (games that keep part of their heap
 * in virtual memory: The Sims 2 Pets uses 0x7E000000+). The runtime fills an entry after a real
 * MMU access, so the PTE's R (and for writes C) bit is already set; entries are keyed by page,
 * segment register value and vtlb_epoch, with separate read and write entries. */
typedef struct GXVTlbEntry {
    u32 vpage;
    u32 sr;
    u32 epoch;
    u32 pad;
    u8* host;
} GXVTlbEntry;
#define GX_VTLB_SIZE 1024u

#include <stdio.h>

typedef void (*PPCMemWriteJournal)(u32 offset, u32 size, void* user);
extern PPCMemWriteJournal g_mem_write_journal;
extern void* g_mem_write_journal_user;
#ifdef GXRUNTIME_GC_FAST_MEM
#define GXRUNTIME_MEM_JOURNAL 0
#else
#define GXRUNTIME_MEM_JOURNAL 1
#endif

#ifdef GXRUNTIME_GC_FAST_MEM
#define GX_RAM_SIZE(cpu) 0x01800000u
#else
#define GX_RAM_SIZE(cpu) ((cpu)->ram_size)
#endif

static GXRUNTIME_ALWAYS_INLINE u8* get_ram_ptr(CPUState* cpu, u32 addr, u32 size, u32* out_offset) {
    u32 masked_addr = addr & ~0x40000000u;
    
#ifndef GXRUNTIME_GC_FAST_MEM
    // Check MEM2 (EXRAM) first as it is much more common in Wii titles
    if (cpu->exram) {
        u32 offset = masked_addr - 0x90000000u;
        if (offset <= cpu->exram_size - size) {
            if (out_offset) *out_offset = (u32)-1;
            return cpu->exram + offset;
        }
    }
#endif
    
    // Check MEM1 (GameCube MEM1 is a fixed 24 MiB: a constant saves a load per access)
    u32 offset = masked_addr - 0x80000000u;
    if (offset <= GX_RAM_SIZE(cpu) - size) {
        if (out_offset) *out_offset = offset;
        return cpu->ram + offset;
    }
    
    return NULL;
}

static GXRUNTIME_ALWAYS_INLINE u8* gx_vtlb_ptr(CPUState* cpu, u32 addr, u32 size, u32 write) {
#ifdef GXRUNTIME_GC_FAST_MEM
    const GXVTlbEntry* e;
    if (!cpu->vtlb)
        return NULL;
    e = &cpu->vtlb[((addr >> 12) & (GX_VTLB_SIZE - 1u)) + (write ? GX_VTLB_SIZE : 0u)];
    if (e->host && e->vpage == (addr >> 12) && e->sr == cpu->sr[addr >> 28] &&
        e->epoch == cpu->vtlb_epoch && (cpu->msr & PPC_MSR_DR) && (addr & 0xFFFu) <= 0x1000u - size)
        return e->host + (addr & 0xFFFu);
#else
    (void)cpu; (void)addr; (void)size; (void)write;
#endif
    return NULL;
}

/* GXRUNTIME_NO_RESERVATION: for games with no lwarx/stwcx. (The Sims 2 Pets has none) the
 * reservation is never set, so stores skip the check. */
static GXRUNTIME_ALWAYS_INLINE void clear_matching_reservation(CPUState* cpu, u32 addr) {
#ifdef GXRUNTIME_NO_RESERVATION
    (void)cpu; (void)addr;
    return;
#endif
    u32 reserve_addr = cpu->reserve_addr & ~0x40000000u;
    u32 store_addr = addr & ~0x40000000u;
    if (cpu->reserve_valid && ((reserve_addr ^ store_addr) & ~31u) == 0)
        cpu->reserve_valid = false;
}

/* Out-of-line slow paths of the mem_read and mem_write helpers (software TLB, then the runtime's external
 * access): keeps the ~300k inlined access sites in generated code small. */
u64 gx_mem_read_slow(CPUState* cpu, u32 addr, u32 size);
void gx_mem_write_slow(CPUState* cpu, u32 addr, u64 value, u32 size);
/* lmw/stmw when the span is not plain RAM (DolRecomp emits a one-check fast path). */
void gx_lmw_slow(CPUState* cpu, u32 ea, u32 first);
void gx_stmw_slow(CPUState* cpu, u32 ea, u32 first);

static GXRUNTIME_ALWAYS_INLINE u64 mem_read64(CPUState* cpu, u32 addr) {
    u8* ptr = get_ram_ptr(cpu, addr, 8, NULL);
    if (ptr == NULL)
        return (u64)gx_mem_read_slow(cpu, addr, 8);
    return read_be64(ptr);
}

static GXRUNTIME_ALWAYS_INLINE void mem_write64(CPUState* cpu, u32 addr, u64 value) {
    u32 offset;
    u8* ptr = get_ram_ptr(cpu, addr, 8, &offset);
    if (ptr == NULL) {
        gx_mem_write_slow(cpu, addr, value, 8);
        return;
    }
    clear_matching_reservation(cpu, addr);
    if (GXRUNTIME_MEM_JOURNAL && g_mem_write_journal && offset != (u32)-1) g_mem_write_journal(offset, 8, g_mem_write_journal_user);
    write_be64(ptr, value);
}

static GXRUNTIME_ALWAYS_INLINE u32 mem_read32(CPUState* cpu, u32 addr) {
    u8* ptr = get_ram_ptr(cpu, addr, 4, NULL);
    if (ptr == NULL)
        return (u32)gx_mem_read_slow(cpu, addr, 4);
    return read_be32(ptr);
}

static GXRUNTIME_ALWAYS_INLINE void mem_write32(CPUState* cpu, u32 addr, u32 value) {
    u32 offset;
    u8* ptr = get_ram_ptr(cpu, addr, 4, &offset);
    if (ptr == NULL) {
        gx_mem_write_slow(cpu, addr, value, 4);
        return;
    }
    clear_matching_reservation(cpu, addr);
    if (GXRUNTIME_MEM_JOURNAL && g_mem_write_journal && offset != (u32)-1) g_mem_write_journal(offset, 4, g_mem_write_journal_user);
    write_be32(ptr, value);
}

#ifdef GXRUNTIME_GC_FAST_MEM
/* Variants taking the RAM base as an argument: DolRecomp's generated code keeps it in a local
 * (dolrecomp_ram), which survives the slow-path calls, instead of reloading cpu->ram. */
#define GX_RB_OFFSET(addr) (((addr) & ~0x40000000u) - 0x80000000u)
#define GX_RB_IN_RAM(off, size) __builtin_expect((off) <= 0x01800000u - (size), 1)
static GXRUNTIME_ALWAYS_INLINE u8 gx_rb_read8(CPUState* cpu, u8* base, u32 addr) {
    u32 off = GX_RB_OFFSET(addr);
    if (GX_RB_IN_RAM(off, 1u)) return base[off];
    return (u8)gx_mem_read_slow(cpu, addr, 1);
}
static GXRUNTIME_ALWAYS_INLINE u16 gx_rb_read16(CPUState* cpu, u8* base, u32 addr) {
    u32 off = GX_RB_OFFSET(addr);
    if (GX_RB_IN_RAM(off, 2u)) return read_be16(base + off);
    return (u16)gx_mem_read_slow(cpu, addr, 2);
}
static GXRUNTIME_ALWAYS_INLINE u32 gx_rb_read32(CPUState* cpu, u8* base, u32 addr) {
    u32 off = GX_RB_OFFSET(addr);
    if (GX_RB_IN_RAM(off, 4u)) return read_be32(base + off);
    return (u32)gx_mem_read_slow(cpu, addr, 4);
}
static GXRUNTIME_ALWAYS_INLINE u64 gx_rb_read64(CPUState* cpu, u8* base, u32 addr) {
    u32 off = GX_RB_OFFSET(addr);
    if (GX_RB_IN_RAM(off, 8u)) return read_be64(base + off);
    return gx_mem_read_slow(cpu, addr, 8);
}
#define GX_RB_WRITE(name, T, N, store)                                                    \
    static GXRUNTIME_ALWAYS_INLINE void name(CPUState* cpu, u8* base, u32 addr, T value) { \
        u32 off = GX_RB_OFFSET(addr);                                                     \
        if (GX_RB_IN_RAM(off, N)) {                                                       \
            u8* p = base + off;                                                           \
            clear_matching_reservation(cpu, addr);                                        \
            store;                                                                        \
            return;                                                                       \
        }                                                                                 \
        gx_mem_write_slow(cpu, addr, value, N);                                           \
    }
GX_RB_WRITE(gx_rb_write8, u8, 1u, *p = value)
GX_RB_WRITE(gx_rb_write16, u16, 2u, write_be16(p, value))
GX_RB_WRITE(gx_rb_write32, u32, 4u, write_be32(p, value))
GX_RB_WRITE(gx_rb_write64, u64, 8u, write_be64(p, value))
#undef GX_RB_WRITE
#endif

static GXRUNTIME_ALWAYS_INLINE u16 mem_read16(CPUState* cpu, u32 addr) {
    u8* ptr = get_ram_ptr(cpu, addr, 2, NULL);
    if (ptr == NULL)
        return (u16)gx_mem_read_slow(cpu, addr, 2);
    return read_be16(ptr);
}

static GXRUNTIME_ALWAYS_INLINE void mem_write16(CPUState* cpu, u32 addr, u16 value) {
    u32 offset;
    u8* ptr = get_ram_ptr(cpu, addr, 2, &offset);
    if (ptr == NULL) {
        gx_mem_write_slow(cpu, addr, value, 2);
        return;
    }
    clear_matching_reservation(cpu, addr);
    if (GXRUNTIME_MEM_JOURNAL && g_mem_write_journal && offset != (u32)-1) g_mem_write_journal(offset, 2, g_mem_write_journal_user);
    write_be16(ptr, value);
}

static GXRUNTIME_ALWAYS_INLINE u8 mem_read8(CPUState* cpu, u32 addr) {
    u8* ptr = get_ram_ptr(cpu, addr, 1, NULL);
    if (ptr == NULL)
        return (u8)gx_mem_read_slow(cpu, addr, 1);
    return *ptr;
}

static GXRUNTIME_ALWAYS_INLINE void mem_write8(CPUState* cpu, u32 addr, u8 value) {
    u32 offset;
    u8* ptr = get_ram_ptr(cpu, addr, 1, &offset);
    if (ptr == NULL) {
        gx_mem_write_slow(cpu, addr, value, 1);
        return;
    }
    clear_matching_reservation(cpu, addr);
    if (GXRUNTIME_MEM_JOURNAL && g_mem_write_journal && offset != (u32)-1) g_mem_write_journal(offset, 1, g_mem_write_journal_user);
    *ptr = value;
}

#undef GXRUNTIME_ALWAYS_INLINE

bool cpu_init(CPUState* cpu);
void cpu_free(CPUState* cpu);
void cpu_reset(CPUState* cpu);

f64 ppc_approx_reciprocal(f64 value);
f64 ppc_approx_rsqrt(f64 value);
bool ppc_fres(CPUState* cpu, f64 value, f64* result);
bool ppc_frsqrte(CPUState* cpu, f64 value, f64* result);
void ppc_ps_res(CPUState* cpu, f64 a, f64 b, f64* result_a, f64* result_b);
void ppc_ps_rsqrte(CPUState* cpu, f64 a, f64 b, f64* result_a, f64* result_b);
bool ppc_fma(CPUState* cpu, f64 a, f64 c, f64 b, bool single,
             bool subtract, bool negative, f64* output);
bool ppc_fctiw(CPUState* cpu, f64 value, bool toward_zero, u64* result);

/* Instruction-shaped FP unit mirroring Dolphin's interpreter bit-exactly
 * (Interpreter_FloatingPoint/Paired + Interpreter_FPUtils NI_* semantics:
 * PPC NaN propagation, Force25Bit frC rounding, single-precision Fill of
 * both PS lanes, FPSCR FPRF/FI/FR/exception updates, VE/ZE write gating).
 * Generated code calls these; the register indices select CPUState lanes. */
void ppc_fadds(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_fsubs(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_fmuls(CPUState* cpu, u8 d, u8 a, u8 c);
void ppc_fdivs(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_fadd(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_fsub(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_fmul(CPUState* cpu, u8 d, u8 a, u8 c);
void ppc_fdiv(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_fmadd_op(CPUState* cpu, u8 d, u8 a, u8 c, u8 b,
                  bool single, bool subtract, bool negative);
void ppc_frsp(CPUState* cpu, u8 d, u8 b);
void ppc_fres_op(CPUState* cpu, u8 d, u8 b);
void ppc_frsqrte_op(CPUState* cpu, u8 d, u8 b);
void ppc_fctiw_op(CPUState* cpu, u8 d, u8 b, bool toward_zero);
void ppc_fcmp(CPUState* cpu, u8 crfd, f64 a, f64 b, bool ordered);
void ppc_ps_add_op(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_ps_sub_op(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_ps_mul_op(CPUState* cpu, u8 d, u8 a, u8 c);
void ppc_ps_div_op(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_ps_madd_op(CPUState* cpu, u8 d, u8 a, u8 c, u8 b,
                    bool subtract, bool negative);
void ppc_ps_madds0(CPUState* cpu, u8 d, u8 a, u8 c, u8 b);
void ppc_ps_madds1(CPUState* cpu, u8 d, u8 a, u8 c, u8 b);
void ppc_ps_sum0(CPUState* cpu, u8 d, u8 a, u8 c, u8 b);
void ppc_ps_sum1(CPUState* cpu, u8 d, u8 a, u8 c, u8 b);
void ppc_ps_muls0(CPUState* cpu, u8 d, u8 a, u8 c);
void ppc_ps_muls1(CPUState* cpu, u8 d, u8 a, u8 c);
void ppc_ps_res_op(CPUState* cpu, u8 d, u8 b);
void ppc_ps_rsqrte_op(CPUState* cpu, u8 d, u8 b);
/* FP loads/stores with Dolphin's alignment exception + lfs/stfs ConvertTo*
 * bit repack. Return false when an exception was taken (callers skip the
 * update-form RA write-back). */
bool ppc_lfs_op(CPUState* cpu, u8 d, u32 ea, u32 cia);
bool ppc_lfd_op(CPUState* cpu, u8 d, u32 ea, u32 cia);
bool ppc_stfs_op(CPUState* cpu, u8 s, u32 ea, u32 cia);
bool ppc_stfd_op(CPUState* cpu, u8 s, u32 ea, u32 cia);
bool ppc_lwarx_op(CPUState* cpu, u8 d, u32 ea, u32 cia);
void ppc_stwcx_op(CPUState* cpu, u8 s, u32 ea, u32 cia);
/* stswi/stswx store-string: mirrors Dolphin's Helper_StoreString word-based
 * read-modify-write of the head/tail partial words. n = byte count, r =
 * first source GPR (wraps). */
void ppc_stsw(CPUState* cpu, u32 ea, u32 n, u8 r, u32 cia);
/* FPSCR control write (mtfsf/mtfsb/mtfsfi/mcrfs): recompute VX/FEX and re-arm
 * the host FPU rounding/flush mode from RN/NI (Dolphin FPSCRUpdated chain). */
void ppc_fpscr_control_updated(CPUState* cpu);
void ppc_mtfsb0_op(CPUState* cpu, u8 bit);
void ppc_mtfsb1_op(CPUState* cpu, u8 bit);
u32 ppc_mfspr(CPUState* cpu, u16 spr, u32 cia);
void ppc_mtspr(CPUState* cpu, u16 spr, u32 value, u32 cia);
void ppc_lswx(CPUState* cpu, u8 rD, u8 rA, u8 rB, u32 cia);
void ppc_cache_control(CPUState* cpu, u8 operation, u32 ea, u32 cia);

bool ppc_add_overflowed(u32 a, u32 b, u32 result);
bool ppc_trap_condition(u8 to, u32 a, u32 b);
void ppc_set_xer_ov(CPUState* cpu, bool ov);
void ppc_take_exception(CPUState* cpu, u32 exception, u32 vector, u32 srr0, u32 srr1_info);
void ppc_program_exception(CPUState* cpu, u32 cause, u32 cia);

/* Gekko lazy FP: generated FPU instructions call this first. Returns true
 * when MSR[FP] is set; otherwise raises the FP-unavailable exception (srr0 =
 * cia so the instruction retries after the OS restores the FP context) and
 * returns false. ppc_lazy_fp_set_enabled(false) restores the historical
 * execute-regardless behavior for hosts that eagerly restore FP state
 * themselves (StrikersRecomp standalone; see recomp-codegen.md Lazy FPU). */
bool ppc_fp_available(CPUState* cpu, u32 cia);
void ppc_lazy_fp_set_enabled(bool enabled);

/* Same contract, inlined. The emitter puts this in front of every FPU
 * instruction, so on a float-heavy title it is one of the hottest things in the
 * module: a `sample` of a Mario Kart race showed the out-of-line call at ~7% of
 * self time inside StaticRecompCore::Run, second only to the hottest guest loop.
 * Nearly every call takes the fast path -- a running game has MSR[FP] set -- so
 * the work was the call itself, not the test. Only the raise stays out of line.
 *
 * ppc_fp_available() is deliberately kept as a real symbol: the LLVM backend
 * emits calls to it by name (llvm_runtime_lowering.cpp), so it cannot become
 * header-only. */
extern bool g_ppc_lazy_fp_enabled;
bool ppc_fp_raise_unavailable(CPUState* cpu, u32 cia);

static inline bool ppc_fp_available_inline(CPUState* cpu, u32 cia) {
    if (!g_ppc_lazy_fp_enabled || (cpu->msr & PPC_MSR_FP))
        return true;
    return ppc_fp_raise_unavailable(cpu, cia);
}
void ppc_fallback_instruction(CPUState* cpu, u32 raw, u32 cia);
bool ppc_host_call(CPUState* cpu, u32 address);
/* DolRecomp's native-region query: true if [start, end) may run natively
   without going through the chassis (cross-chunk direct calls). */
#define PPC_HOST_CALL_NATIVE_REGION_QUERY 0xFFFFFFFCu
#define PPC_NATIVE_REGION_QUERY_PENDING 0xFEu
#define PPC_NATIVE_REGION_QUERY_HANDLED 0xFFu
bool ppc_native_region_available_slow(CPUState* cpu, u32 start, u32 end);
static inline bool ppc_native_region_available(CPUState* cpu, u32 start, u32 end) {
    const u8* ok = cpu->native_ok;
    if (ok) {
        u32 target = (start - cpu->native_base) >> 14;
        u32 caller = (cpu->lr - cpu->native_base) >> 14;
        if (target < cpu->native_slots && caller < cpu->native_slots && ok[target] && ok[caller] &&
            cpu->lr != cpu->native_idle_pc)
            return true;
    }
    return ppc_native_region_available_slow(cpu, start, end);
}
void ppc_system_call_exception(CPUState* cpu, u32 cia);
void ppc_dsi_exception(CPUState* cpu, u32 ea, u32 cia, u32 dsisr);
void ppc_alignment_exception(CPUState* cpu, u32 ea, u32 cia);
u32 ppc_mftb(CPUState* cpu, u16 tbr, u32 cia);
void ppc_rfi(CPUState* cpu, u32 cia);
void ppc_dcbz_l(CPUState* cpu, u32 ea, u32 cia);
bool ppc_psq_load(CPUState* cpu, u8 frD, u32 ea, bool w, u8 gqr, bool indexed, u32 cia);
bool ppc_psq_store(CPUState* cpu, u8 frS, u32 ea, bool w, u8 gqr, bool indexed, u32 cia);

/* Forced inline: at -O2 the compiler otherwise leaves ~2000 call sites out of line. */
#if defined(__GNUC__) || defined(__clang__)
#define GXRUNTIME_PSQ_INLINE static inline __attribute__((always_inline))
#else
#define GXRUNTIME_PSQ_INLINE static inline
#endif

/* Inlined fast path for the unquantised case. A `sample` of a Mario Kart race
 * put psq_load_value and psq_store_value at ~9% of self time inside
 * StaticRecompCore::Run, on top of ppc_psq_load's own frame.
 *
 * Almost all of that is GQR type 0 -- plain IEEE singles, no quantisation and
 * no scale -- which reduces to two 32-bit accesses and a float conversion.
 * Quantised types (4..7), an invalid type, and the LSQE illegal-instruction
 * check all fall through to the out-of-line version, so behaviour is unchanged.
 *
 * The win is bigger than skipping a call: at every generated site w, gqr_index,
 * indexed and cia are literals, so inlining lets the compiler fold the LSQE
 * test and the w branch away per site, which it cannot do across a call. */
GXRUNTIME_PSQ_INLINE bool ppc_psq_load_inline(CPUState* cpu, u8 frD, u32 ea, bool w,
                                       u8 gqr_index, bool indexed, u32 cia) {
    const u32 gqr = cpu->gqr[gqr_index & 7u];
    if (((gqr >> 16) & 7u) == 0u && (indexed || (cpu->hid2 & PPC_HID2_LSQE) != 0u)) {
        cpu->fpr[frD] = f64_value(convert_to_double(mem_read32(cpu, ea)));
        cpu->ps1[frD] = w ? 1.0 : f64_value(convert_to_double(mem_read32(cpu, ea + 4u)));
        return true;
    }
    return ppc_psq_load(cpu, frD, ea, w, gqr_index, indexed, cia);
}

GXRUNTIME_PSQ_INLINE bool ppc_psq_store_inline(CPUState* cpu, u8 frS, u32 ea, bool w,
                                        u8 gqr_index, bool indexed, u32 cia) {
    const u32 gqr = cpu->gqr[gqr_index & 7u];
    if ((gqr & 7u) == 0u && (indexed || (cpu->hid2 & PPC_HID2_LSQE) != 0u)) {
        mem_write32(cpu, ea, convert_to_single_ftz(f64_bits(cpu->fpr[frS])));
        if (!w)
            mem_write32(cpu, ea + 4u, convert_to_single_ftz(f64_bits(cpu->ps1[frS])));
        return true;
    }
    return ppc_psq_store(cpu, frS, ea, w, gqr_index, indexed, cia);
}
u32 ppc_eciwx(CPUState* cpu, u32 ea, u32 cia);
void ppc_ecowx(CPUState* cpu, u32 ea, u32 value, u32 cia);
void ppc_tlbie(CPUState* cpu, u32 ea, u32 cia);
void ppc_fpscr_updated(CPUState* cpu);
void ppc_memory_fence(void);

#ifdef __cplusplus
}
#endif

/* Header-only fast paths of the FP / paired-single helpers declared above. */
#include "cpu_float_inline.h"

#endif /* DOLRECOMP_CPU_H */
