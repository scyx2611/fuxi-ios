#!/usr/bin/env python3
"""ASan/UBSan host tests for M9 multi-process GPU isolation, scheduling,
guilty-VM fault recovery, U-026 dual-layer blacklist and 0102 admission."""
from pathlib import Path
import importlib.util
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADERS = ROOT / 'xnu/fuxi-adreno'


def build_backend_test():
    metal_spec = importlib.util.spec_from_file_location('tmetal', ROOT / 'tools/test-fuxi-metal.py')
    tmetal = importlib.util.module_from_spec(metal_spec)
    metal_spec.loader.exec_module(tmetal)

    backend = (HEADERS / 'fuxi_gpu_abi_backend.inc').read_text()
    backend = re.sub(r'__asm__ volatile\("dsb sy" ::: "memory"\);', '(void)0;', backend)
    backend = backend.replace('__asm__ volatile("dc ivac, %0" :: "r"(p) : "memory");', 'host_cache(p,1);')
    backend = backend.replace('__asm__ volatile("dc civac, %0" :: "r"(p) : "memory");', 'host_cache(p,0);')

    adapter = (HEADERS / 'fuxi_adreno.c').read_text()
    doorbell = adapter[adapter.index('#define FG_GMU_AHB_FENCE_STATUS'):adapter.index('\n}\n', adapter.index('static int fg_ring_doorbell')) + 3]
    doorbell = re.sub(r'__asm__ volatile\("dsb sy" ::: "memory"\);', '(void)0;', doorbell)

    m9_helpers = tmetal.BACKEND_TEST[tmetal.BACKEND_TEST.index('static void seed(void)'):tmetal.BACKEND_TEST.index('static unsigned wait_us(void)')]
    m9_helpers += tmetal.BACKEND_TEST[tmetal.BACKEND_TEST.index('static void reset(void)'):tmetal.BACKEND_TEST.index('/* Models the CP')]
    m9_helpers += 'static void gpu_model(void) {}\n'

    # Override FG_PT_ALLOC/FG_PT_PUBLISH so BUG-4 can inject PT allocation failures & count L2 publishes.
    pt_hooks = r'''
static unsigned fg_test_pt_publishes, fg_test_pt_alloc_fail_at, fg_test_pt_alloc_calls;
static uint64_t fg_test_next_pa = 0xa00000000ULL;
static int fg_test_pt_alloc(uint64_t *pa_out, volatile uint64_t **va_out, void **handle_out) {
 fg_test_pt_alloc_calls++;
 if (fg_test_pt_alloc_fail_at && fg_test_pt_alloc_calls == fg_test_pt_alloc_fail_at) return 0;
 void *mem = calloc(1, 16384);
 if (!mem) return 0;
 *pa_out = fg_test_next_pa;
 fg_test_next_pa += 16384;
 *va_out = (volatile uint64_t *)mem;
 *handle_out = mem;
 return 1;
}
static void fg_test_pt_free(void *handle) { free(handle); }
#define FG_PT_ALLOC(pa, va, h) fg_test_pt_alloc((pa), (va), (h))
#define FG_PT_FREE(h) fg_test_pt_free((h))
#define FG_PT_PUBLISH(va, bytes) do { (void)(va); (void)(bytes); fg_test_pt_publishes++; } while (0)
'''

    m9_test = m9_helpers + r'''
static void free_all_vms(void) {
 for (unsigned i = 0; i < FG_ABI_MAX_VMS; i++) {
  if (fg_vm_pool[i].pt_slab_count) fg_vm_free_all_pt(&fg_vm_pool[i]);
 }
}

int main(void) {
 const uint64_t va = 0x20000000ULL;
 const uintptr_t src_bb = 0x100000UL, src_maps = 0x200000UL, src_wx = 0x300000UL;
 const uint64_t bytes = 32768ULL;

 /* 1. Multi-VM page-table / ASID / fence slot / inline window isolation */
 seed();
 ncimportArg = 512 | 2097152;
 fg_scanout_double_requested = 1;
 fg_scanout_triple_requested = 0;
 memset(presentL2, 0, sizeof(presentL2));
 presentL2[128 + 3] = 0x41000 | 3;
 assert(fg_abi_init() && fg_abi_queue && fg_abi_inline);

 struct fuxi_gpu_vm *vm_bb = NULL, *vm_maps = NULL, *vm_wx = NULL, *vm_c1_3 = NULL, *vm_c1_4 = NULL, *vm_c1_5 = NULL;
 assert(fuxi_gpu_abi_vm_create(FG_CLASS_COMPOSITOR, 100, "backboardd", &vm_bb) == 0 && vm_bb);
 assert(fuxi_gpu_abi_vm_create(FG_CLASS_APP, 201, "Maps", &vm_maps) == 0 && vm_maps);
 assert(fuxi_gpu_abi_vm_create(FG_CLASS_APP, 202, "Weather", &vm_wx) == 0 && vm_wx);
 assert(fuxi_gpu_abi_vm_create(FG_CLASS_APP, 203, "Notes", &vm_c1_3) == 0 && vm_c1_3);
 assert(fuxi_gpu_abi_vm_create(FG_CLASS_APP, 204, "Mail", &vm_c1_4) == 0 && vm_c1_4);
 /* Max 4 concurrent Class 1 render VMs enforced */
 assert(fuxi_gpu_abi_vm_create(FG_CLASS_APP, 205, "Safari", &vm_c1_5) == -1 && !vm_c1_5);

 /* Distinct ASIDs (excluding 0 and g3_asid), distinct root PAs, distinct fence slots */
 assert(vm_bb->asid != 0 && vm_bb->asid != g3_asid);
 assert(vm_maps->asid != 0 && vm_maps->asid != g3_asid && vm_maps->asid != vm_bb->asid);
 assert(vm_wx->asid != 0 && vm_wx->asid != g3_asid && vm_wx->asid != vm_bb->asid && vm_wx->asid != vm_maps->asid);
 assert(vm_bb->root_pa != vm_maps->root_pa && vm_maps->root_pa != vm_wx->root_pa);
 assert(vm_bb->fence_off == FG_ABI_FENCE_OFF);
 assert(vm_maps->fence_off != vm_bb->fence_off && vm_wx->fence_off != vm_maps->fence_off);

 /* Same VA 0x20000000 mapped in all three VMs without cross-VM PTE collision */
 assert(fuxi_gpu_abi_vm_map(vm_bb, va, src_bb, bytes, 3, 0, 2) == 0);
 assert(fuxi_gpu_abi_vm_map(vm_maps, va, src_maps, bytes, 3, 0, 2) == 0);
 assert(fuxi_gpu_abi_vm_map(vm_wx, va, src_wx, bytes, 3, 0, 2) == 0);
 assert(vm_bb->l3_va[0] && vm_maps->l3_va[0] && vm_wx->l3_va[0]);
 assert(vm_bb->l3_va[0] != vm_maps->l3_va[0] && vm_maps->l3_va[0] != vm_wx->l3_va[0]);
 volatile uint64_t *lf_bb = fg_abi_leaf_vm(vm_bb, va);
 volatile uint64_t *lf_maps = fg_abi_leaf_vm(vm_maps, va);
 volatile uint64_t *lf_wx = fg_abi_leaf_vm(vm_wx, va);
 assert(lf_bb && lf_maps && lf_wx);
 assert(*lf_bb != *lf_maps && *lf_maps != *lf_wx);
 uint64_t expect_maps = ((uint64_t)pmap_find_phys(0, src_maps) << 14) | 3 | (1u << 6) | (2u << 8) | (1u << 10);
 assert(*lf_maps == expect_maps);

 /* Unmapping Maps does not affect backboardd or Weather */
 assert(fuxi_gpu_abi_vm_unmap(vm_maps, va, bytes) == 0);
 assert(!*fg_abi_leaf_vm(vm_maps, va) && *fg_abi_leaf_vm(vm_bb, va) && *fg_abi_leaf_vm(vm_wx, va));
 assert(fuxi_gpu_abi_vm_map(vm_maps, va, src_maps, bytes, 3, 0, 2) == 0);

 /* Inline copy window is exclusive to Class 0 (never exposed to Class 1 VMs) */
 uint64_t ad[1] = {va}, sz[1] = {4096}, f_bb = 0, f_m1 = 0, f_m2 = 0, f_m3 = 0, f_w1 = 0;
 assert(fuxi_gpu_abi_vm_submit(vm_bb, ad, sz, 1, &f_bb) == 0 && f_bb == 1);
 assert(vm_bb->inline_attached == 1 && (vm_bb->root_va[2] == (FG_INL_L2 | 3)));
 assert(!vm_maps->inline_attached && vm_maps->root_va[2] == 0);
 assert(!vm_wx->inline_attached && vm_wx->root_va[2] == 0);
 assert(sregs[(g_cb(g3_bank) + 0x20) / 4] == (uint32_t)vm_bb->root_pa);
 assert((sregs[(g_cb(g3_bank) + 0x2c) / 4] >> 16) == vm_bb->asid);
 complete();
 assert(fuxi_gpu_abi_vm_poll(vm_bb, f_bb) == 0);

 /* 2. Two-class scheduling: Class 1 yields to Class 0/present & capped at inflight <= 2 */
 fuxi_gpu_abi_vm_bb_wait(1);
 assert(fuxi_gpu_abi_vm_submit(vm_maps, ad, sz, 1, &f_m1) == 1);
 fuxi_gpu_abi_vm_bb_wait(-1);
 fg_abi_present_wait = 1;
 assert(fuxi_gpu_abi_vm_submit(vm_maps, ad, sz, 1, &f_m1) == 1);
 fg_abi_present_wait = 0;

 /* Class 1 inflight cap (max 2) */
 assert(fuxi_gpu_abi_vm_submit(vm_maps, ad, sz, 1, &f_m1) == 0 && f_m1 == 1);
 assert(sregs[(g_cb(g3_bank) + 0x20) / 4] == (uint32_t)vm_maps->root_pa);
 assert((sregs[(g_cb(g3_bank) + 0x2c) / 4] >> 16) == vm_maps->asid);
 assert(fuxi_gpu_abi_vm_submit(vm_maps, ad, sz, 1, &f_m2) == 0 && f_m2 == 2);
 assert(fuxi_gpu_abi_vm_submit(vm_maps, ad, sz, 1, &f_m3) == 1);

 /* Class 1 quantum is 40 ms (poll == 1 at 39 ms, timeout -3 at 40 ms + 1 us) */
 ticks = 39000;
 assert(fuxi_gpu_abi_vm_poll(vm_maps, f_m1) == 1);
 ticks = 40001;
 int p_timeout = fuxi_gpu_abi_vm_poll(vm_maps, f_m1);
 assert(p_timeout == -3);
 assert(fg_soft_recover_count == 1 && vm_maps->poisoned == 1 && vm_maps->faults == 1);
 assert(state == 1 && fg_abi_poisoned == 0 && fuxi_gpu_abi_available() == 1);
 assert(!vm_bb->poisoned && !vm_wx->poisoned && fg_active_vm == vm_bb);
 /* Poisoned Class 1 refuses new submits */
 assert(fuxi_gpu_abi_vm_submit(vm_maps, ad, sz, 1, &f_m3) == -1);
 assert(fuxi_gpu_abi_vm_submit(vm_bb, ad, sz, 1, &f_bb) == 0 && f_bb == 2);
 assert(fg_inl_usable());
 complete();
 assert(fuxi_gpu_abi_vm_poll(vm_bb, f_bb) == 0);

 /* 3. Guilty-VM SMMU/HW fault on Class 1 Weather isolates Weather without poisoning backboardd */
 ticks = 0;
 assert(fuxi_gpu_abi_vm_submit(vm_wx, ad, sz, 1, &f_w1) == 0 && f_w1 == 1);
 fault = 1; regs[0x928] = 0xdead0000;
 int p_fault = fuxi_gpu_abi_vm_poll(vm_wx, f_w1);
 assert(p_fault == -1 && fg_soft_recover_count == 2);
 assert(vm_wx->poisoned == 1 && vm_wx->faults == 1 && vm_wx->drained == 1);
 assert(state == 1 && fg_abi_poisoned == 0 && fuxi_gpu_abi_available() == 1);
 assert(!vm_bb->poisoned);
 uint64_t wx_f = 0, wx_s = 0, bb_f = 0, bb_s = 0;
 assert(fuxi_gpu_abi_vm_status(vm_wx, &wx_f, &wx_s) == 0 && wx_f == 1);
 assert(fuxi_gpu_abi_vm_status(vm_bb, &bb_f, &bb_s) == 0 && bb_f == 0);
 assert(fuxi_gpu_abi_vm_submit(vm_bb, ad, sz, 1, &f_bb) == 0 && f_bb == 3);
 complete();
 assert(fuxi_gpu_abi_vm_poll(vm_bb, f_bb) == 0);

 /* 4. Fail-closed paths: soft-recovery hardware failure on Class 1, and any fault on Class 0 */
 fg_soft_recover_fail_model = 1;
 struct fuxi_gpu_vm *vm_notes = vm_c1_3;
 assert(fuxi_gpu_abi_vm_map(vm_notes, va, src_wx, bytes, 3, 0, 2) == 0);
 uint64_t f_n1 = 0;
 assert(fuxi_gpu_abi_vm_submit(vm_notes, ad, sz, 1, &f_n1) == 0);
 fault = 1;
 assert(fuxi_gpu_abi_vm_poll(vm_notes, f_n1) < 0);
 assert(fg_soft_recover_fail_count == 1 && state == 2 && fg_abi_poisoned == 1 && !fuxi_gpu_abi_available());
 fg_soft_recover_fail_model = 0;

 /* Class 0 (backboardd) fault always fails closed to state == 2 even when soft recover works */
 reset();
 assert(fuxi_gpu_abi_vm_create(FG_CLASS_COMPOSITOR, 100, "backboardd", &vm_bb) == 0);
 assert(fuxi_gpu_abi_vm_map(vm_bb, va, src_bb, bytes, 3, 0, 2) == 0);
 assert(fuxi_gpu_abi_vm_submit(vm_bb, ad, sz, 1, &f_bb) == 0);
 fault = 1;
 assert(fuxi_gpu_abi_vm_poll(vm_bb, f_bb) == -1);
 assert(state == 2 && fg_abi_poisoned == 1 && vm_bb->poisoned == 1 && !fuxi_gpu_abi_available());

 /* 5. BUG-2: Cross-VM unmap MUST return busy (1) when another VM or present copy holds GPU */
 reset();
 assert(fuxi_gpu_abi_vm_create(FG_CLASS_COMPOSITOR, 100, "backboardd", &vm_bb) == 0);
 assert(fuxi_gpu_abi_vm_create(FG_CLASS_APP, 201, "Maps", &vm_maps) == 0);
 assert(fuxi_gpu_abi_vm_map(vm_bb, va, src_bb, bytes, 3, 0, 2) == 0);
 assert(fuxi_gpu_abi_vm_map(vm_maps, va, src_maps, bytes, 3, 0, 2) == 0);
 assert(fuxi_gpu_abi_vm_submit(vm_bb, ad, sz, 1, &f_bb) == 0);
 assert(fg_client_busy == 1 && fg_active_vm == vm_bb);
 failTlb = 1;
 assert(fuxi_gpu_abi_vm_unmap(vm_maps, va, bytes) == 1);
 assert(fg_client_busy == 1 && fg_active_vm == vm_bb && !vm_maps->poisoned && !fg_abi_poisoned);
 failTlb = 0;
 complete();
 assert(fuxi_gpu_abi_vm_poll(vm_bb, f_bb) == 0);
 /* Also blocked when fg_busy == 1 (present copy running) */
 fg_busy = 1;
 assert(fuxi_gpu_abi_vm_unmap(vm_maps, va, bytes) == 1);
 fg_busy = 0;
 assert(fuxi_gpu_abi_vm_unmap(vm_maps, va, bytes) == 0);

 /* 6. BUG-4: Cross-chunk fg_abi_map_locked PT alloc failure must still FG_PT_PUBLISH dirty L2 */
 assert(fuxi_gpu_abi_vm_create(FG_CLASS_APP, 202, "Weather", &vm_wx) == 0);
 /* Fill existing L3 slab (slot 0=root, 1=l2[0], 2=l2[1], 3=free -> alloc one L3 for 0x20000000 to exhaust slab) */
 assert(fuxi_gpu_abi_vm_map(vm_wx, 0x20000000ULL, src_wx, 4096, 3, 0, 2) == 0);
 /* Now pt_next_slot == 4 (slab boundary). Cross-chunk 0x203ff000 (chunk 1 & chunk 2):
  * chunk 1 succeeds (allocates new slab), exhausts slots? Wait: 1 slab has 4 slots, so make chunk 1..5 cross 5 chunks and fail on 2nd slab alloc! */
 fg_test_pt_alloc_calls = 0;
 fg_test_pt_alloc_fail_at = 2; /* 1st slab alloc for chunk 1..4 succeeds, 2nd slab alloc for chunk 5 fails */
 unsigned pub_before = fg_test_pt_publishes;
 assert(fuxi_gpu_abi_vm_map(vm_wx, 0x203ff000ULL, src_wx, 0x802000ULL, 3, 0, 2) == -1);
 fg_test_pt_alloc_fail_at = 0;
 assert(fg_test_pt_publishes > pub_before);
 assert(vm_wx->l2_va[0][257] != 0);

 /* 7. BUG-5 Backend + Reviewer 2 Minor #2: per-PID Class 1 VM cap (max 2), shared PID quota, poisoned check, and present_wait clear */
 struct fuxi_gpu_vm *vm_p1 = NULL, *vm_p2 = NULL, *vm_p3 = NULL;
 assert(fuxi_gpu_abi_vm_create(FG_CLASS_APP, 888, "Maps", &vm_p1) == 0 && vm_p1);
 assert(fuxi_gpu_abi_vm_create(FG_CLASS_APP, 888, "Maps", &vm_p2) == 0 && vm_p2);
 assert(fuxi_gpu_abi_vm_create(FG_CLASS_APP, 888, "Maps", &vm_p3) == -1 && !vm_p3);
 assert(fuxi_gpu_abi_vm_reserve_quota(vm_p1, 1, 600ULL << 20, 0) == 0);
 assert(fuxi_gpu_abi_vm_reserve_quota(vm_p2, 1, 424ULL << 20, 0) == 0);
 assert(fuxi_gpu_abi_vm_reserve_quota(vm_p2, 1, 16384, 0) == -1);
 fuxi_gpu_abi_vm_release_quota(vm_p1, 1, 600ULL << 20, 0);
 assert(fuxi_gpu_abi_vm_reserve_quota(vm_p2, 1, 16384, 0) == 0);
 fuxi_gpu_abi_vm_release_quota(vm_p2, 2, (424ULL << 20) + 16384, 0);
 assert(fuxi_gpu_abi_vm_poisoned(vm_p1) == 0);
 /* Reviewer 2 Minor #2: fg_abi_present_wait cleared on guilty-VM soft recovery */
 assert(fuxi_gpu_abi_vm_map(vm_p1, va, src_maps, bytes, 3, 0, 2) == 0);
 uint64_t fp1 = 0;
 assert(fuxi_gpu_abi_vm_submit(vm_p1, ad, sz, 1, &fp1) == 0);
 fg_abi_present_wait = 1;
 fault = 1;
 assert(fuxi_gpu_abi_vm_poll(vm_p1, fp1) == -1);
 assert(fg_abi_present_wait == 0 && vm_p1->poisoned == 1 && fuxi_gpu_abi_vm_poisoned(vm_p1) == 1);
 assert(fuxi_gpu_abi_vm_reserve_quota(vm_p1, 1, 16384, 0) == -1);

 free_all_vms();
 puts("PASS: M9 backend: multi-VM PT/ASID/fence-slot/inline isolation, Class 1 40ms quantum & yield, guilty-VM soft recovery, Class 0 fail-closed, BUG-2/BUG-4/BUG-5/Minor#2");
 return 0;
}
'''
    prefix = tmetal.BACKEND_PREFIX.replace('//FENCED_DOORBELL//', doorbell)
    return prefix + pt_hooks + backend + m9_test


def build_xnu_soft_recover_test():
    backend = (HEADERS / 'fuxi_gpu_abi_backend.inc').read_text()
    start = backend.index('static int fg_soft_recover_hw(struct fuxi_gpu_vm *guilty_vm)')
    end = backend.index('#define FG_SOFT_RECOVER_HW(vm) fg_soft_recover_hw(vm)')
    real_recover = backend[start:end]
    real_recover = re.sub(r'__asm__ volatile\("dsb sy" ::: "memory"\);', '(void)0;', real_recover)

    return r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct fuxi_gpu_vm { uint32_t asid, seq; };
static unsigned g3_bank = 2, g3_wptr = 0, fg_access_failed = 0, fg_irq_quiesced = 0, fg_irq_rearmed = 0;
static uint64_t fg_suspends = 0;
static struct fuxi_gpu_vm g_bb_vm_obj = {3, 1}, *fg_bb_vm = &g_bb_vm_obj;
static uint32_t g_bb_in_use = 1, g_bb_poisoned = 0;
#define GPU_REG_BASE 0x3d00000ULL
#define GPU_SMMU_BASE 0x15000000ULL
#define G3_SQE_CNTL 0x808u
#define G4_RB_CMP_DBG 0x8e00u
#define G4_RB_NC 0x8e08u
#define G4_TPL1_NC 0xb604u
#define G4_SP_NC 0xae02u
#define G3_APERTURE_HOST 0xa00u
#define G4_GRAS_NC 0x8602u
#define G3_PTR_PA 0x1000u
#define G3_FENCE_PA 0x2000u
#define G3_FENCE_VA 0x10029000u

static uint32_t g_fsr = 0x40000002u; /* FSR.SS=1 + translation fault */
static unsigned g_cb_resumed_before_drain = 0, g_drain_polls_while_stalled = 0, g_drain_step = 0;
static unsigned g_switched_to_null = 0, g_switched_to_bb = 0, g_tlb_asid = 0;
static uint32_t g_regs[0x10000];
static uint32_t g_fence_word = 0, g_sentinel = 0;

static unsigned g_cb(unsigned b) { return 0x10000u + b * 0x1000u; }
static void fg_irq_quiesce_soft(void) { fg_irq_quiesced++; }
static int fg_irq_rearm_soft(void) { fg_irq_rearmed++; return 1; }
static void delay(unsigned us) { (void)us; }
static void g_zero(uint64_t pa, unsigned bytes) { (void)pa; (void)bytes; }
static void g_wr(uint64_t pa, unsigned off, uint32_t v) { (void)pa; (void)off; g_fence_word = v; }
static uint32_t g3_read(uint64_t pa) { (void)pa; return g_fence_word; }
static uint32_t g3_pkt7(unsigned op, unsigned n) { return (op << 16) | n; }
static void g3_emit(uint32_t w) {
 g3_wptr = (g3_wptr + 1) & 8191;
 if (w == 0x5a730001u || (w & 0xffff0000u) == 0x5a730000u) g_sentinel = w;
}
static int fg_access(uint64_t addr, uint64_t *val, int write, int smmu) {
 (void)smmu;
 unsigned off = (unsigned)(addr - GPU_REG_BASE);
 if (off == 0x3c45u * 4u && write) { g_drain_step = (unsigned)*val; return 0; }
 if (off == 0x3c46u * 4u && !write) {
  if (g_fsr & (1u << 30)) {
   g_drain_polls_while_stalled++;
   *val = 0; /* Hardware AXI stall keeps GBIF acks blocked until CB_RESUME! */
   return 0;
  }
  *val = (g_drain_step >= 2) ? 3ULL : (g_drain_step == 1 ? 1ULL : 0ULL);
  return 0;
 }
 return 0;
}
static uint32_t g3_sr(unsigned off) {
 if (off == g_cb(g3_bank) + 0x58) return g_fsr;
 return 0;
}
static int g3_sw(unsigned off, uint32_t v) {
 if (off == g_cb(g3_bank) + 0x08 && v == 1) {
  if (g_drain_step == 0) g_cb_resumed_before_drain = 1;
  g_fsr &= ~(1u << 30);
  return 1;
 }
 if (off == g_cb(g3_bank) + 0x58) { g_fsr &= ~v; return 1; }
 return 1;
}
static uint32_t g3_gr(unsigned r) {
 if (r == 0x806) return g3_wptr;
 if (r == 0x210) return 0;
 return g_regs[r];
}
static int g3_gw(unsigned r, uint32_t v) {
 g_regs[r] = v;
 if (r == 0x807 && g_sentinel) g_fence_word = g_sentinel;
 return 1;
}
static int fg_abi_tlb_asid(unsigned asid) { g_tlb_asid = asid; return 0; }
static int fg_abi_switch_vm(struct fuxi_gpu_vm *vm) {
 if (!vm) g_switched_to_null = 1; else g_switched_to_bb = 1;
 return 1;
}
static int g3_hw_init(void) { return 1; }
static int g3_faulted(void) { return 0; }
static int g3_wait(unsigned ms) { (void)ms; return 0; }

struct fg_bb_shim { int in_use, poisoned; };
''' + real_recover.replace('fg_bb_vm->in_use && !fg_bb_vm->poisoned', 'g_bb_in_use && !g_bb_poisoned') + r'''
int main(void) {
 struct fuxi_gpu_vm guilty = {7, 42};
 int ok = fg_soft_recover_hw(&guilty);
 assert(ok == 1);
 assert(g_cb_resumed_before_drain == 1);
 assert(g_drain_polls_while_stalled == 0);
 assert(g_fsr == 0 && g_tlb_asid == 7 && g_switched_to_null == 1 && g_switched_to_bb == 1);
 assert(fg_irq_quiesced == 1 && fg_irq_rearmed == 1);
 puts("PASS: BUG-3 real XNU fg_soft_recover_hw unblocks FSR.SS CB_RESUME before GBIF drain");
 return 0;
}
'''


def build_admission_cpp(patched_uc_text):
    hl_start = patched_uc_text.index('static bool fuxiGpuForbiddenProcess(')
    hl_end = patched_uc_text.index('class IOAcceleratorES')
    kernel_helpers = patched_uc_text[hl_start:hl_end]

    cc_start = patched_uc_text.index('  if (!handler) return kIOReturnBadArgument;')
    cc_end = patched_uc_text.index('  auto *uc=new FuxiAdrenoUserClient;')
    create_body = patched_uc_text[cc_start:cc_end]

    return r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "fuxi_abi_wire.h"
#include "ClientGate.hpp"
#include "BackboardGate.hpp"

using task_t = unsigned;
using proc_t = void *;
using DTEntry = void *;
using IOReturn = int;
using UInt32 = uint32_t;
constexpr int kSuccess = 0, kIOReturnSuccess = 0, kIOReturnBadArgument = 1,
 kIOReturnUnsupported = 2, kIOReturnNotPrivileged = 3, kIOReturnError = -1;
constexpr unsigned kIOClientPrivilegeAdministrator = 1;

static task_t g_cur_task = 7;
static int g_pid = 42;
static char g_proc_name[128] = "";
static char g_exec_path[256] = "";
static char g_cs_ident[256] = "";
static bool g_has_exec_vnode = true;
static bool g_is_root = false;
static bool g_is_platform = false;
static bool g_has_blob = false;
static int g_bb_arg = 0;
static char g_boot_clients[256] = "";
static bool g_has_adt = false;
static char g_adt_clients[512] = "";
static unsigned g_adt_bytes_override = 0;

using vnode_t = void *;
#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif
static task_t current_task() { return g_cur_task; }
[[maybe_unused]] static int proc_selfpid() { return g_pid; }
/* Simulate real XNU truncation: p_name is 32 chars (proc_selfname), p_comm is 16 chars (proc_name). */
static void proc_selfname(char *buf, size_t sz) {
 size_t cap = sz < 33 ? sz : 33;
 snprintf(buf, cap, "%s", g_proc_name);
}
static void proc_name(int, char *buf, size_t sz) {
 size_t cap = sz < 17 ? sz : 17;
 snprintf(buf, cap, "%s", g_proc_name);
}
static proc_t proc_self() { return (proc_t)0x1; }
static proc_t proc_find(int) { return (proc_t)0x1; }
static void *get_bsdtask_info(task_t) { return (void *)0x1; }
static int proc_pid(proc_t) { return g_pid; }
static void proc_rele(proc_t) {}
static int csproc_get_platform_binary(proc_t) { return g_is_platform ? 1 : 0; }
static struct cs_blob *csproc_get_blob(proc_t) { return g_has_blob ? (struct cs_blob *)0x1 : nullptr; }
static const char *csblob_get_identity(struct cs_blob *) { return g_cs_ident[0] ? g_cs_ident : nullptr; }
static vnode_t proc_getexecutablevnode(proc_t) { return (g_has_exec_vnode && g_exec_path[0]) ? (vnode_t)0x2 : nullptr; }
static int vn_getpath(vnode_t, char *buf, int *len) {
 if (!g_exec_path[0] || !buf || !len || *len <= 0) return -1;
 int need = (int)strlen(g_exec_path) + 1;
 if (need > *len) return -1;
 memcpy(buf, g_exec_path, (size_t)need);
 *len = need;
 return 0;
}
static void vnode_put(vnode_t) {}
static int PE_parse_boot_argn(const char *k, void *out, size_t sz) {
 if (!strcmp(k, "fuxibbgpu") && sz == sizeof(int)) {
  if (!g_bb_arg) return 0;
  memcpy(out, &g_bb_arg, sizeof(int));
  return 1;
 }
 if (!strcmp(k, "fuxigpuclients")) {
  if (!g_boot_clients[0]) return 0;
  snprintf(static_cast<char *>(out), sz, "%s", g_boot_clients);
  return 1;
 }
 return 0;
}
static int SecureDTLookupEntry(void *, const char *path, DTEntry *out) {
 if (!g_has_adt || strcmp(path, "/chosen")) return kIOReturnError;
 *out = (DTEntry)0x1;
 return kSuccess;
}
static int SecureDTGetProperty(DTEntry, const char *key, const void **prop, unsigned *bytes) {
 if (!g_has_adt || strcmp(key, "fuxi,gpu-clients") || !g_adt_clients[0]) return kIOReturnError;
 *prop = g_adt_clients;
 *bytes = g_adt_bytes_override ? g_adt_bytes_override : (unsigned)strlen(g_adt_clients) + 1;
 return kSuccess;
}
static inline void IOLog(const char *, ...) {}
struct IOUserClient {
 static IOReturn clientHasPrivilege(void *, unsigned) {
  return g_is_root ? kIOReturnSuccess : kIOReturnNotPrivileged;
 }
};

''' + kernel_helpers + r'''

static IOReturn simulateCreateClient(task_t task, void *securityID, UInt32 type, void **handler, unsigned *outClass) {
''' + create_body + r'''
 *outClass = clientClass;
 *handler = (void *)0x1;
 return kIOReturnSuccess;
}

int main() {
 void *h = nullptr;
 unsigned cls = 99;

 /* 1. U-026 Dual-layer blacklist: denied even when root + platform + ADT + boot-arg */
 g_has_adt = true;
 snprintf(g_adt_clients, sizeof(g_adt_clients), "%s", "SpringBoard,mediaserverd,PosterBoard,CollectionsPoster,WeatherPoster,Maps,Weather");
 snprintf(g_boot_clients, sizeof(g_boot_clients), "%s", "SpringBoard,mediaserverd,PosterBoard,CollectionsPoster,WeatherPoster,Maps,Weather");
 g_is_root = true; g_is_platform = true; g_has_blob = true; g_bb_arg = 1;

 const char *forbidden[] = {
  "SpringBoard", "mediaserverd", "PosterBoard", "CollectionsPoster", "WeatherPoster", "CustomPosterExt",
  "LockScreenAstronomyWallpaperPosterExt", "123456789012345678901234567Poster", "mywallpaperposterapp", "WALLPAPERPOSTER"
 };
 for (const char *name : forbidden) {
  snprintf(g_proc_name, sizeof(g_proc_name), "%s", name);
  snprintf(g_exec_path, sizeof(g_exec_path), "/System/Library/App/%s", name);
  snprintf(g_cs_ident, sizeof(g_cs_ident), "com.apple.%s", name);
  g_has_exec_vnode = true;
  h = nullptr; cls = 99;
  assert(simulateCreateClient(7, nullptr, 0x4147, &h, &cls) == kIOReturnNotPrivileged && !h);
  assert(fuxi::hiddenClient(name));
  assert(fuxi::readClients("/nonexistent/clients", name, g_adt_clients) == fuxi::ClientMode::deny);
 }

 /* 1b. BUG-1 adversarial: 32-byte truncated proc_selfname without full execBase/csIdent MUST fail-closed */
 snprintf(g_proc_name, sizeof(g_proc_name), "%s", "LockScreenAstronomyWallpaperPosterExt");
 g_has_exec_vnode = false; g_exec_path[0] = 0; g_cs_ident[0] = 0; g_has_blob = false; g_is_root = true;
 assert(simulateCreateClient(7, nullptr, 0x4147, &h, &cls) == kIOReturnNotPrivileged);
 /* Class 1 with 32B truncated proc_selfname and no execBase even with safe csIdent MUST fail-closed */
 g_is_root = false; g_has_blob = true;
 snprintf(g_cs_ident, sizeof(g_cs_ident), "%s", "com.example.safe");
 assert(simulateCreateClient(7, nullptr, 0, &h, &cls) == kIOReturnNotPrivileged);
 /* Safe proc_selfname & execBase but csIdent contains "poster" MUST be denied */
 snprintf(g_proc_name, sizeof(g_proc_name), "%s", "Maps");
 snprintf(g_exec_path, sizeof(g_exec_path), "%s", "/Applications/Maps.app/Maps");
 snprintf(g_cs_ident, sizeof(g_cs_ident), "%s", "com.apple.WallpaperposterExtension");
 assert(simulateCreateClient(7, nullptr, 0, &h, &cls) == kIOReturnNotPrivileged);

 /* 2. backboardd with fuxibbgpu=1 + platform binary => FG_CLASS_COMPOSITOR */
 g_is_root = false; g_has_exec_vnode = true;
 snprintf(g_proc_name, sizeof(g_proc_name), "%s", "backboardd");
 snprintf(g_exec_path, sizeof(g_exec_path), "%s", "/usr/libexec/backboardd");
 snprintf(g_cs_ident, sizeof(g_cs_ident), "%s", "com.apple.backboardd");
 assert(simulateCreateClient(7, nullptr, 0x4147, &h, &cls) == kIOReturnSuccess && cls == FG_CLASS_COMPOSITOR);

 /* 3. Foreground Class 1 apps in ADT fuxi,gpu-clients with blob (no root) => FG_CLASS_APP */
 g_is_platform = false; g_has_blob = true;
 for (const char *app : {"Maps", "Weather"}) {
  snprintf(g_proc_name, sizeof(g_proc_name), "%s", app);
  snprintf(g_exec_path, sizeof(g_exec_path), "/Applications/%s.app/%s", app, app);
  snprintf(g_cs_ident, sizeof(g_cs_ident), "com.apple.%s", app);
  h = nullptr; cls = 99;
  assert(simulateCreateClient(7, nullptr, 0, &h, &cls) == kIOReturnSuccess && cls == FG_CLASS_APP);
  assert(fuxi::readClients("/nonexistent/clients", app, g_adt_clients) == fuxi::ClientMode::allow);
 }

 /* 4. Unlisted app or unsigned listed app denied in Kernel; ADT overrides boot-arg */
 snprintf(g_proc_name, sizeof(g_proc_name), "%s", "RogueApp");
 snprintf(g_exec_path, sizeof(g_exec_path), "%s", "/Applications/RogueApp.app/RogueApp");
 snprintf(g_cs_ident, sizeof(g_cs_ident), "%s", "com.example.RogueApp");
 assert(simulateCreateClient(7, nullptr, 0, &h, &cls) == kIOReturnNotPrivileged);
 assert(fuxi::readClients("/nonexistent/clients", "RogueApp", g_adt_clients) == fuxi::ClientMode::deny);

 /* Signed check: Maps without platform or blob denied */
 g_has_blob = false; g_cs_ident[0] = 0;
 snprintf(g_proc_name, sizeof(g_proc_name), "%s", "Maps");
 snprintf(g_exec_path, sizeof(g_exec_path), "%s", "/Applications/Maps.app/Maps");
 assert(simulateCreateClient(7, nullptr, 0, &h, &cls) == kIOReturnNotPrivileged);
 g_has_blob = true;
 snprintf(g_cs_ident, sizeof(g_cs_ident), "%s", "com.apple.Maps");

 /* BUG-6 consistency: >=256 byte ADT fuxi,gpu-clients denied fail-closed in Kernel */
 memset(g_adt_clients, 'a', 250);
 strcpy(g_adt_clients + 250, ",Maps");
 assert(strlen(g_adt_clients) + 1 == 256);
 assert(simulateCreateClient(7, nullptr, 0, &h, &cls) == kIOReturnNotPrivileged);

 /* ADT takes precedence over boot-arg: if ADT has Maps only, Notes in boot-arg is denied; if no ADT, boot-arg admits Notes */
 snprintf(g_adt_clients, sizeof(g_adt_clients), "%s", "Maps");
 snprintf(g_boot_clients, sizeof(g_boot_clients), "%s", "Notes");
 snprintf(g_proc_name, sizeof(g_proc_name), "%s", "Notes");
 snprintf(g_exec_path, sizeof(g_exec_path), "%s", "/Applications/Notes.app/Notes");
 snprintf(g_cs_ident, sizeof(g_cs_ident), "%s", "com.apple.Notes");
 assert(simulateCreateClient(7, nullptr, 0, &h, &cls) == kIOReturnNotPrivileged);
 g_has_adt = false;
 assert(simulateCreateClient(7, nullptr, 0, &h, &cls) == kIOReturnSuccess && cls == FG_CLASS_APP);

 /* 5. BackboardGate vs ForegroundClientGate decoupling */
 fuxi::foregroundClientGate.resetForTest();
 fuxi::foregroundClientGate.admit();
 fuxi::foregroundClientGate.fault();
 assert(fuxi::foregroundClientGate.blocked() && fuxi::foregroundClientGate.failureCount() == 1);
 assert(!fuxi::backboardGate.blocked());
 fuxi::foregroundClientGate.resetForTest();

 puts("PASS: M9 0102 + ClientGate U-026 dual blacklist (*Poster*, SpringBoard, mediaserverd), ADT priority over boot-arg, signed Class 1 admission & Gate decoupling");
 return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp)
        c_src = out / 'm9_backend.c'
        c_src.write_text(build_backend_test())
        c_exe = out / 'm9_backend'
        subprocess.run(['clang', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', '-Wno-unused-variable',
                        '-fsanitize=address,undefined', '-I', str(HEADERS), str(c_src), '-o', str(c_exe)], check=True)
        subprocess.run([str(c_exe)], check=True)

        sr_src = out / 'm9_soft_recover_xnu.c'
        sr_src.write_text(build_xnu_soft_recover_test())
        sr_exe = out / 'm9_soft_recover_xnu'
        subprocess.run(['clang', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', '-Wno-unused-variable',
                        '-fsanitize=address,undefined', str(sr_src), '-o', str(sr_exe)], check=True)
        subprocess.run([str(sr_exe)], check=True)

        uc_tree = out / 'uc_tree/iokit/Kernel/arm'
        uc_tree.mkdir(parents=True, exist_ok=True)
        (uc_tree / 'FuxiAdrenoUserClient.cpp').write_bytes((HEADERS / 'FuxiAdrenoUserClient.cpp').read_bytes())
        subprocess.run(['patch', '-s', '-p1', '--fuzz=0', '-i', str(ROOT / 'xnu/patches/0102-fuxi-gpu-backboardd-open.patch')],
                       cwd=out / 'uc_tree', check=True)
        patched_uc = (uc_tree / 'FuxiAdrenoUserClient.cpp').read_text()

        cpp_src = out / 'm9_admission.cpp'
        cpp_src.write_text(build_admission_cpp(patched_uc))
        cpp_exe = out / 'm9_admission'
        subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-I', str(HEADERS), '-I', str(ROOT / 'tools/fuxi-metal'),
                        str(cpp_src), '-o', str(cpp_exe)], check=True)
        subprocess.run([str(cpp_exe)], check=True)


if __name__ == '__main__':
    main()
