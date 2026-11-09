// RecompCore: StaticRecomp CPU core - Memory and instruction fallback HLE hooks.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <optional>
#include <cstdlib>
#include <unordered_map>
#include <vector>
#include "Core/PowerPC/StaticRecomp/StaticRecompCore.h"
#include "Core/System.h"
#include "Core/PowerPC/MMU.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/PowerPC/JitInterface.h"
#include "Core/PowerPC/Interpreter/Interpreter.h"
#include "Core/PowerPC/StaticRecomp/StaticRecompLockstep.h"
#include "Core/HW/GPFifo.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/SystemTimers.h"
#include "Common/Logging/Log.h"

#include <cstdio>

namespace
{
constexpr u32 LOCKED_CACHE_BASE = 0xE0000000u;
}

// DolRecomp cpu.h's native-region query protocol (GXRuntime's header lacks it).
#ifndef PPC_HOST_CALL_NATIVE_REGION_QUERY
#define PPC_HOST_CALL_NATIVE_REGION_QUERY 0xFFFFFFFCu
#define PPC_NATIVE_REGION_QUERY_PENDING 0xFEu
#define PPC_NATIVE_REGION_QUERY_HANDLED 0xFFu
#endif

bool StaticRecompCore::HookHostCall(CPUState* cpu, u32 address)
{
  auto* core = static_cast<StaticRecompCore*>(cpu->external_user_data);
  // Generated code asks before calling another chunk directly: answer what
  // the dispatch loop would check for that target.
  if (address == PPC_HOST_CALL_NATIVE_REGION_QUERY &&
      cpu->external_rid == PPC_NATIVE_REGION_QUERY_PENDING)
  {
    cpu->external_rid = PPC_NATIVE_REGION_QUERY_HANDLED;
    const u32 target = cpu->external_addr;
    // The call has set lr to its return address: returning to an idle loop
    // must go through the chassis, which only idles when it sees that pc.
    if (core->m_idle_pc != 0 && cpu->lr == core->m_idle_pc)
      return true;
    // Fast path for the common case (no RELs, no forced fallback ranges): two table
    // lookups. The per-chunk host-call state is filled for every chunk at module load,
    // and a chunk without a host call can't hold a host-call target.
    if (!core->m_has_rel_modules && core->m_module_active &&
        core->m_forced_fallback_ranges.empty() && !core->m_chunk_lookup_table.empty())
    {
      const int ti = core->GetAddressLookupIndex(target);
      const int ci = core->GetAddressLookupIndex(cpu->lr);
      const int n = static_cast<int>(core->m_chunk_lookup_table.size());
      if (ti >= 0 && ti < n && ci >= 0 && ci < n)
      {
        const int tc = core->m_chunk_lookup_table[ti];
        const int cc = core->m_chunk_lookup_table[ci];
        if (tc >= 0 && cc >= 0 && core->m_chunk_state[tc] == CHUNK_VERIFIED &&
            core->m_chunk_host_call_state[tc] == 1 && core->m_chunk_host_call_state[cc] == 1)
        {
          // Remember both for the inline check (the caller is running natively: verified).
          if (!core->m_native_ok.empty() && core->m_chunk_state[cc] == CHUNK_VERIFIED &&
              core->m_chunk_slot[tc] >= 0 && core->m_chunk_slot[cc] >= 0)
          {
            core->m_native_ok[core->m_chunk_slot[tc]] = 1;
            core->m_native_ok[core->m_chunk_slot[cc]] = 1;
          }
          return false;
        }
      }
    }
    if (core->m_has_rel_modules || !core->m_module_active || !core->FastDispatchableAt(target) ||
        core->IsHostCallAddress(target))
      return true;
    const int index = core->ChunkIndexOf(target);
    // The caller goes on natively after the call: not if a hook (a mod patch)
    // is in its chunk, as the return would skip it (skip-intro's patch sits
    // right after a bl).
    const int caller = core->ChunkIndexOf(cpu->lr);
    return index < 0 || core->ChunkContainsHostCall(static_cast<u32>(index)) || caller < 0 ||
           core->ChunkContainsHostCall(static_cast<u32>(caller));
  }
  return core->m_module_source.host_call &&
         core->m_module_source.host_call(cpu, address, core->m_module_source.host_call_user);
}

// After a real MMU access to a page-table-mapped address (its PTE's R/C bits now set), cache the
// page's host pointer for the generated code (gx_vtlb_ptr in GXRuntime cpu.h).
static bool MmioStats();

void StaticRecompCore::FillVtlb(StaticRecompCore* core, CPUState* cpu, u32 ea, bool write)
{
  // Lockstep verification needs every access to reach the MMU.
  if (!cpu->vtlb || !(cpu->msr & PPC_MSR_DR) || ea >= 0x80000000u ||
      core->m_lockstep_verifier->IsEnabled())
  {
    if (MmioStats() && ea < 0x80000000u && (ea >> 24) == 0x7E)
    {
      static int n;
      if (n++ < 8)
        std::fprintf(stderr, "[vtlb] skip ea=%08X vtlb=%p msr=%08X lockstep=%d\n", ea,
                     static_cast<void*>(cpu->vtlb), cpu->msr,
                     core->m_lockstep_verifier->IsEnabled() ? 1 : 0);
    }
    return;
  }
  const std::optional<u32> phys = core->m_system.GetMMU().GetTranslatedAddress(ea);
  // Without MMU emulation Dolphin backs 0x7E000000-0x7FFFFFFF with "fake VMEM" (the game's
  // ARAM-backed virtual memory); cache those pages too.
  auto& memory = core->m_system.GetMemory();
  if (phys && memory.GetFakeVMEM() && (*phys & 0xFE000000u) == 0x7E000000u)
  {
    GXVTlbEntry& e = cpu->vtlb[((ea >> 12) & (GX_VTLB_SIZE - 1u)) + (write ? GX_VTLB_SIZE : 0u)];
    e.vpage = ea >> 12;
    e.sr = cpu->sr[ea >> 28];
    e.epoch = cpu->vtlb_epoch;
    e.host = memory.GetFakeVMEM() + ((*phys & memory.GetFakeVMemMask()) & ~0xFFFu);
    return;
  }
  if (!phys || *phys >= cpu->ram_size || cpu->ram_size - (*phys & ~0xFFFu) < 0x1000u)
  {
    if (MmioStats())
    {
      static int n;
      if (n++ < 8)
        std::fprintf(stderr, "[vtlb] no-map ea=%08X phys=%08X ok=%d ram_size=%08X\n", ea,
                     phys ? *phys : 0u, phys ? 1 : 0, cpu->ram_size);
    }
    return;
  }
  if (MmioStats())
  {
    static unsigned n;
    if ((n++ & 0x3FFFFu) == 0)  // every 256K fills: is the epoch moving?
      std::fprintf(stderr, "[vtlb] n=%u fill ea=%08X phys=%08X sr=%08X epoch=%u w=%d\n", n, ea, *phys,
                   cpu->sr[ea >> 28], cpu->vtlb_epoch, write ? 1 : 0);
  }
  GXVTlbEntry& e = cpu->vtlb[((ea >> 12) & (GX_VTLB_SIZE - 1u)) + (write ? GX_VTLB_SIZE : 0u)];
  e.vpage = ea >> 12;
  e.sr = cpu->sr[ea >> 28];
  e.epoch = cpu->vtlb_epoch;
  e.host = cpu->ram + (*phys & ~0xFFFu);
}

// STATICRECOMP_MMIO_STATS=1: count slow-path (non-RAM) guest accesses per 4 KiB page and
// print the top pages every 4M accesses, to find what the native fast path misses.
// Read on first use: iOS sets environment variables from Documents/env.txt after static init.
static bool MmioStats()
{
  static const bool enabled = std::getenv("STATICRECOMP_MMIO_STATS") != nullptr;
  return enabled;
}

static void CountExternalAccess(u32 ea, bool write)
{
  static std::unordered_map<u32, u64> pages;
  static u64 total = 0;
  ++pages[(ea & ~0xFFFu) | (write ? 1u : 0u)];
  if ((++total & ((1u << 22) - 1)) != 0)
    return;
  std::vector<std::pair<u64, u32>> top;
  for (const auto& [page, count] : pages)
    top.emplace_back(count, page);
  std::sort(top.rbegin(), top.rend());
  std::fprintf(stderr, "[mmio] total=%llu", static_cast<unsigned long long>(total));
  for (size_t i = 0; i < top.size() && i < 10; ++i)
    std::fprintf(stderr, " %08X%s:%llu", top[i].second & ~1u, (top[i].second & 1u) ? "w" : "r",
                 static_cast<unsigned long long>(top[i].first));
  std::fprintf(stderr, "\n");
}

u64 StaticRecompCore::HookExternalRead(CPUState* cpu, u32 ea, u8 size)
{
  auto* core = static_cast<StaticRecompCore*>(cpu->external_user_data);
  ea = core->TranslateRelAddress(ea);
  if (MmioStats())
    CountExternalAccess(ea, false);
  if (ea == 0)
    std::fprintf(stderr, "[zero-access] read size=%u guest_pc=%08x ppc_pc=%08x lr=%08x\n", size,
                 cpu->pc, core->m_system.GetPPCState().pc, cpu->lr);
  core->PropagateGuestMSR();
  auto& mmu = core->m_system.GetMMU();
  u64 value;
  switch (size)
  {
  case 1:
    value = mmu.Read<u8>(ea);
    break;
  case 2:
    value = mmu.Read<u16>(ea);
    break;
  case 4:
    value = mmu.Read<u32>(ea);
    break;
  case 8:
    value = mmu.Read<u64>(ea);
    break;
  default:
    ERROR_LOG_FMT(POWERPC, "StaticRecomp: external read of bad size {} at 0x{:08X}", size, ea);
    return 0;
  }
  if (core->m_lockstep_verifier->m_ls_journaling &&
      StaticRecompLockstep::LsHwAccessInScope(mmu, ea))
  {
    core->m_lockstep_verifier->m_journal.native_reads.push_back({ea, static_cast<u32>(value), size});
  }
  FillVtlb(core, cpu, ea, false);
  return value;
}

void StaticRecompCore::HookExternalWrite(CPUState* cpu, u32 ea, u64 value, u8 size)
{
  auto* core = static_cast<StaticRecompCore*>(cpu->external_user_data);
  ea = core->TranslateRelAddress(ea);
  if (MmioStats())
    CountExternalAccess(ea, true);
  if (ea == 0)
    std::fprintf(stderr, "[zero-access] write size=%u guest_pc=%08x ppc_pc=%08x lr=%08x\n", size,
                 cpu->pc, core->m_system.GetPPCState().pc, cpu->lr);

  // Gather-pipe fast path: stores to the write-gather pipe page at effective
  // 0xCC008000 go straight to GPFifo, mirroring the MMU's masked-write
  // special case without an MMU round trip. Keying on the effective page is
  // the same shortcut Dolphin's JITs take (optimizeGatherPipe). GPFifo
  // maintains ppc_state.gather_pipe_ptr internally.
  if ((ea & 0xFFFFF000) == 0xCC008000u)
  {
    if (core->m_lockstep_verifier->m_ls_journaling)
      core->m_lockstep_verifier->m_journal.native_mmio.push_back({ea, static_cast<u32>(value), size});
    auto& gpfifo = core->m_system.GetGPFifo();
    // Not Write*(): its CheckGatherPipe also calls CompileExceptionCheck, which
    // keys on ppc.pc (stale during a native burst) and so only registers junk
    // addresses / invalidates unrelated blocks. FastWrite + FastCheckGatherPipe
    // does the same pipe update without it.
    switch (size)
    {
    case 1:
      gpfifo.FastWrite8(static_cast<u8>(value));
      gpfifo.FastCheckGatherPipe();
      return;
    case 2:
      gpfifo.FastWrite16(static_cast<u16>(value));
      gpfifo.FastCheckGatherPipe();
      return;
    case 4:
      gpfifo.FastWrite32(static_cast<u32>(value));
      gpfifo.FastCheckGatherPipe();
      return;
    default:
      for (u32 i = size * 8u; i > 0;)
      {
        i -= 8;
        gpfifo.Write8(static_cast<u8>(value >> i));
      }
      return;
    }
  }

  core->PropagateGuestMSR();
  auto& mmu = core->m_system.GetMMU();
  if (core->m_lockstep_verifier->m_ls_journaling &&
      StaticRecompLockstep::LsHwAccessInScope(mmu, ea))
  {
    core->m_lockstep_verifier->m_journal.native_mmio.push_back({ea, static_cast<u32>(value), size});
  }
  switch (size)
  {
  case 1:
    mmu.Write<u8>(static_cast<u8>(value), ea);
    break;
  case 2:
    mmu.Write<u16>(static_cast<u16>(value), ea);
    break;
  case 4:
    mmu.Write<u32>(static_cast<u32>(value), ea);
    break;
  case 8:
    mmu.Write<u64>(value, ea);
    break;
  default:
    ERROR_LOG_FMT(POWERPC, "StaticRecomp: external write of bad size {} at 0x{:08X}", size, ea);
    return;
  }
  FillVtlb(core, cpu, ea, true);
}

u32 StaticRecompCore::HookExternalRead32(CPUState* cpu, u32 ea, u8 rid)
{
  // eciwx external-control read. EAR-enable and alignment were checked by the
  // generated helper; Dolphin's interpreter services the access as a plain
  // MMU read (the rid is carried in EAR only).
  auto* core = static_cast<StaticRecompCore*>(cpu->external_user_data);
  ea = core->TranslateRelAddress(ea);
  core->PropagateGuestMSR();
  auto& mmu = core->m_system.GetMMU();
  const u32 value = mmu.Read<u32>(ea);
  if (core->m_lockstep_verifier->m_ls_journaling &&
      StaticRecompLockstep::LsHwAccessInScope(mmu, ea))
  {
    core->m_lockstep_verifier->m_journal.native_reads.push_back({ea, value, 4});
  }
  return value;
}

void StaticRecompCore::HookExternalWrite32(CPUState* cpu, u32 ea, u32 value, u8 rid)
{
  // ecowx external-control write; see HookExternalRead32.
  auto* core = static_cast<StaticRecompCore*>(cpu->external_user_data);
  ea = core->TranslateRelAddress(ea);
  core->PropagateGuestMSR();
  auto& mmu = core->m_system.GetMMU();
  if (core->m_lockstep_verifier->m_ls_journaling &&
      StaticRecompLockstep::LsHwAccessInScope(mmu, ea))
  {
    core->m_lockstep_verifier->m_journal.native_mmio.push_back({ea, value, 4});
  }
  mmu.Write<u32>(value, ea);
}

void* StaticRecompCore::HookExternalPointer(CPUState* cpu, u32 ea, u32 size)
{
  auto* core = static_cast<StaticRecompCore*>(cpu->external_user_data);
  auto& memory = core->m_system.GetMemory();
  if (ea >= LOCKED_CACHE_BASE && size != 0 &&
      (ea - LOCKED_CACHE_BASE) + size <= memory.GetL1CacheSize())
  {
    return memory.GetL1Cache() + (ea - LOCKED_CACHE_BASE);
  }
  // Everything else stays on the per-access MMU hooks: this hook receives
  // *effective* addresses, and whether one maps to RAM depends on live
  // MSR/BAT state that only the MMU can answer. Handing out a raw pointer
  // here would bypass MMIO and translation. (Memory::GetPointerForRange was
  // considered and rejected for exactly that reason.)
  return nullptr;
}

u32 StaticRecompCore::HookSPRRead(CPUState* cpu, u16 spr, u32 cia)
{
  auto* core = static_cast<StaticRecompCore*>(cpu->external_user_data);
  auto& ppc = core->m_system.GetPPCState();
  if (spr >= 1024)
  {
    ppc_program_exception(cpu, PPC_PROGRAM_ILLEGAL, cia);
    return 0;
  }

  switch (spr)
  {
  case SPR_DEC:
    if ((ppc.spr[SPR_DEC] & 0x80000000u) == 0)
      ppc.spr[SPR_DEC] = core->m_system.GetSystemTimers().GetFakeDecrementer();
    break;
  case SPR_WPAR:
    if (core->m_system.GetGPFifo().IsBNE())
      ppc.spr[SPR_WPAR] |= 1;
    else
      ppc.spr[SPR_WPAR] &= ~1u;
    break;
  case SPR_UPMC1:
    return ppc.spr[SPR_PMC1];
  case SPR_UPMC2:
    return ppc.spr[SPR_PMC2];
  case SPR_UPMC3:
    return ppc.spr[SPR_PMC3];
  case SPR_UPMC4:
    return ppc.spr[SPR_PMC4];
  case SPR_IABR:
    return ppc.spr[SPR_IABR] & ~1u;
  default:
    break;
  }
  return ppc.spr[spr];
}

void StaticRecompCore::HookSPRWrite(CPUState* cpu, u16 spr, u32 value, u32 cia)
{
  auto* core = static_cast<StaticRecompCore*>(cpu->external_user_data);
  auto& system = core->m_system;
  auto& ppc = system.GetPPCState();
  if (spr >= 1024)
  {
    ppc_program_exception(cpu, PPC_PROGRAM_ILLEGAL, cia);
    return;
  }

  const u32 old_value = ppc.spr[spr];
  ppc.spr[spr] = value;
  // Page table base and BATs change what a virtual page maps to.
  if (spr == SPR_SDR || (spr >= SPR_IBAT0U && spr <= SPR_DBAT3L) ||
      (spr >= SPR_IBAT4U && spr <= SPR_DBAT7L))
    ++cpu->vtlb_epoch;

  switch (spr)
  {
  case SPR_TL_W:
    TL(ppc) = value;
    system.GetSystemTimers().TimeBaseSet();
    return;
  case SPR_TU_W:
    TU(ppc) = value;
    system.GetSystemTimers().TimeBaseSet();
    return;
  case SPR_PVR:
    ppc.spr[SPR_PVR] = old_value;
    return;
  case SPR_HID0:
  {
    if (HID0(ppc).ICFI)
    {
      HID0(ppc).ICFI = 0;
      ppc.iCache.Reset(system.GetJitInterface());
    }
    return;
  }
  case SPR_HID1:
    ppc.spr[SPR_HID1] &= 0xF8000000u;
    return;
  case SPR_HID4:
    if (old_value != value)
    {
      system.GetMMU().IBATUpdated();
      system.GetMMU().DBATUpdated();
    }
    return;
  case SPR_WPAR:
    system.GetGPFifo().ResetGatherPipe();
    return;
  case SPR_DMAL:
    if (DMAL(ppc).DMA_T)
    {
      const u32 mem_address = DMAU(ppc).MEM_ADDR << 5;
      const u32 cache_address = DMAL(ppc).LC_ADDR << 5;
      u32 length = (DMAU(ppc).DMA_LEN_U << 2) | DMAL(ppc).DMA_LEN_L;
      if (length == 0)
        length = 128;
      if (DMAL(ppc).DMA_LD)
        system.GetMMU().DMA_MemoryToLC(cache_address, mem_address, length);
      else
        system.GetMMU().DMA_LCToMemory(mem_address, cache_address, length);
    }
    DMAL(ppc).DMA_T = 0;
    return;
  case SPR_DEC:
    if ((old_value >> 31) == 0 && (value >> 31) != 0)
      ppc.Exceptions |= EXCEPTION_DECREMENTER;
    system.GetSystemTimers().DecrementerSet();
    return;
  case SPR_SDR:
    system.GetMMU().SDRUpdated();
    return;
  case SPR_MMCR0:
  case SPR_MMCR1:
    PowerPC::MMCRUpdated(ppc);
    return;
  case SPR_THRM1:
  case SPR_THRM2:
  case SPR_THRM3:
  {
    const auto update = [&ppc](UReg_THRM12* reg) {
      if (!THRM3(ppc).E || !reg->V)
      {
        reg->TIV = 0;
      }
      else
      {
        reg->TIV = 1;
        reg->TIN = reg->TID ? 42 < reg->THRESHOLD : 42 > reg->THRESHOLD;
      }
    };
    update(&THRM1(ppc));
    update(&THRM2(ppc));
    return;
  }
  default:
    break;
  }

  const bool ibat = (spr >= SPR_IBAT0U && spr <= SPR_IBAT3L) ||
                    (spr >= SPR_IBAT4U && spr <= SPR_IBAT7L);
  const bool dbat = (spr >= SPR_DBAT0U && spr <= SPR_DBAT3L) ||
                    (spr >= SPR_DBAT4U && spr <= SPR_DBAT7L);
  if (old_value != value && ibat)
    system.GetMMU().IBATUpdated();
  else if (old_value != value && dbat)
    system.GetMMU().DBATUpdated();
}

void StaticRecompCore::HookCacheControl(CPUState* cpu, u8 operation, u32 ea, u32 cia)
{
  auto* core = static_cast<StaticRecompCore*>(cpu->external_user_data);
  ea = core->TranslateRelAddress(ea);
  core->PropagateGuestMSR();
  auto& ppc = core->m_system.GetPPCState();
  auto& mmu = core->m_system.GetMMU();

  if (operation == PPC_CACHE_ICBI)
  {
    ppc.iCache.Invalidate(core->m_system.GetMemory(), core->m_system.GetJitInterface(), ea);
    return;
  }

  if (!ppc.m_enable_dcache)
  {
    core->m_system.GetJitInterface().InvalidateICacheLine(ea);
    return;
  }

  switch (operation)
  {
  case PPC_CACHE_DCBST:
    mmu.StoreDCacheLine(ea);
    break;
  case PPC_CACHE_DCBF:
    mmu.FlushDCacheLine(ea);
    break;
  case PPC_CACHE_DCBI:
    mmu.InvalidateDCacheLine(ea);
    break;
  default:
    ppc_program_exception(cpu, PPC_PROGRAM_ILLEGAL, cia);
    break;
  }
}

void StaticRecompCore::HookInstructionFallback(CPUState* cpu, u32 raw, u32 cia)
{
  auto* core = static_cast<StaticRecompCore*>(cpu->external_user_data);
  cia = core->TranslateRelAddress(cia);
  ++core->m_hook_fallback_instructions;

  // Lockstep: a block that fell back to the interpreter for an unmodeled
  // instruction (DMA mtspr, cache op, ...) performed side effects not captured
  // by the RAM journal / MMIO hooks, so re-running it on the shadow would
  // double-issue them. Mark it unsafe to differentially check.
  if (core->m_lockstep_verifier->m_ls_journaling)
    core->m_lockstep_verifier->m_ls_fallback_seen = true;

  auto& system = core->m_system;
  auto& ppc = system.GetPPCState();

  // Fast path for dcbf/dcbst/dcbi/icbi: streaming code flushes caches in
  // 32-byte loops (thousands per frame), and these ops read two GPRs and
  // change no CPU state, so they run straight off ctx without the full
  // SyncOut/interpreter/SyncIn round trip. This mirrors Dolphin's
  // interpreter with dcache emulation off: every one funnels into
  // InvalidateICacheLine (keeping the SMC guard exact). dcbi's PR!=0
  // privilege trap and dcache-on configs take the slow path.
  if ((raw >> 26) == 31u && !ppc.m_enable_dcache)
  {
    const u32 xo = (raw >> 1) & 0x3FFu;
    if (xo == 86u || xo == 54u || xo == 982u || (xo == 470u && (cpu->msr & 0x4000u) == 0))
    {
      const u32 ra = (raw >> 16) & 31u;
      const u32 rb = (raw >> 11) & 31u;
      const u32 ea = (ra ? cpu->gpr[ra] : 0u) + cpu->gpr[rb];
      if (xo == 982u)
        ppc.iCache.Invalidate(system.GetMemory(), system.GetJitInterface(), ea);
      else
        system.GetJitInterface().InvalidateICacheLine(ea);
      // These bypass SingleStepInner, so charge Dolphin's PPCTables cost
      // here (icbi 4, dcbf/dcbst/dcbi 5); their emitted block cost is zero.
      ppc.downcount -= (xo == 982u) ? 4 : 5;
      cpu->pc = cia + 4u;
      return;
    }
  }

  // The recompiled segment resumes via the dispatcher at the PC this leaves
  // behind, so this must execute exactly the instruction at cia via
  // Dolphin's interpreter and hand the register state back.
  core->SyncOut();
  ppc.pc = cia;
  ppc.npc = cia + 4;
  ppc.downcount -= system.GetInterpreter().SingleStepInner();
  core->SyncIn();
}
