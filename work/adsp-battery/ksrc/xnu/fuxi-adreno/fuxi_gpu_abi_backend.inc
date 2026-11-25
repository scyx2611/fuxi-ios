/* Built into fuxi_adreno.c, with access to its proven SMMU/ring helpers.
 * Multi-client M9 isolation: per-process dynamic fuxi_gpu_vm roots/ASIDs,
 * two-class scheduling (Class 0 compositor/root, Class 1 foreground apps),
 * and guilty-VM fault isolation (Class 1 fault recovers without poisoning
 * backboardd or setting fg_state = 2).
 */
#define FG_ABI_PT (G_MEM+0x1c00000)
#define FG_ABI_PT_BYTES 0x400000u
#define FG_ABI_RUNTIME_L3 (FG_ABI_PT+12288u)
#define FG_ABI_FENCE_OFF 128u
#define FG_ABI_VM_FENCE_STRIDE 16u
#define FG_ABI_VM_FENCE_BASE 256u
_Static_assert(FG_ABI_PT >= G4_DST_PA+G4_BYTES+4096, "ABI table overlap");
_Static_assert(FG_ABI_PT >= G5_META_PA+G5_META_BYTES, "ABI/zap metadata overlap");
_Static_assert(FG_ABI_PT >= FG_FW_BASE+3*FG_FW_SLOT, "ABI/cold handoff or firmware overlap");
_Static_assert(FG_ABI_PT+FG_ABI_PT_BYTES <= G_MEM+G_MEM_SIZE, "ABI tables exceed scratch");
_Static_assert(FG_ABI_VM_FENCE_BASE + (FG_ABI_MAX_VMS - 1u) * FG_ABI_VM_FENCE_STRIDE <= 1024u, "per-VM fence slots overlap job stamps");

#ifndef FG_PT_ALLOC
#ifdef FUXI_GPU_XNU
extern int fuxi_gpu_pt_alloc_kernel(uint64_t *pa_out, volatile uint64_t **va_out, void **handle_out);
extern void fuxi_gpu_pt_free_kernel(void *handle);
#define FG_PT_ALLOC(pa, va, h) fuxi_gpu_pt_alloc_kernel((pa), (va), (h))
#define FG_PT_FREE(h) fuxi_gpu_pt_free_kernel((h))
#define FG_PT_PUBLISH(va, bytes) do { CleanPoC_DcacheRegion_Force((uintptr_t)(va), (bytes)); __asm__ volatile("dsb sy" ::: "memory"); } while (0)
#else
static uint64_t fg_host_pt_next_pa = 0xa00000000ULL;
static int fg_host_pt_alloc(uint64_t *pa_out, volatile uint64_t **va_out, void **handle_out)
{
 void *mem = calloc(1, 16384);
 if (!mem) return 0;
 *pa_out = fg_host_pt_next_pa;
 fg_host_pt_next_pa += 16384;
 *va_out = (volatile uint64_t *)mem;
 *handle_out = mem;
 return 1;
}
static void fg_host_pt_free(void *handle) { free(handle); }
#define FG_PT_ALLOC(pa, va, h) fg_host_pt_alloc((pa), (va), (h))
#define FG_PT_FREE(h) fg_host_pt_free((h))
#define FG_PT_PUBLISH(va, bytes) do { (void)(va); (void)(bytes); } while (0)
#endif
#endif

static unsigned fg_abi_live, fg_abi_seq, fg_abi_completed, fg_abi_poisoned, fg_abi_timed_out;
static int fg_abi_drained = 1;
static int fg_abi_live_for_scanout(void) { return fg_abi_live; }
static uint64_t fg_client_deadline, fg_client_submit_at;
static thread_call_data_t fg_client_watchdog;
static unsigned fg_class1_quantum_ms = 40u;
#define FG_CLASS1_INFLIGHT_MAX 2u

struct fg_abi_import_rec { uint64_t iova, bytes; uintptr_t source; unsigned permissions, gpu_only, dty_first, dty_pages; };
struct fg_abi_map_rec { uint64_t iova, bytes; uintptr_t source; uint32_t seq; };

#define FG_VM_MAX_SLABS 256u
#define FG_VM_CHUNKS 768u
struct fuxi_gpu_vm {
 uint32_t id, asid, fence_off;
 unsigned client_class;
 int pid;
 char name[32];
 uint64_t root_pa, l2_pa[2];
 volatile uint64_t *root_va, *l2_va[2];
 uint64_t l3_pa[FG_VM_CHUNKS];
 volatile uint64_t *l3_va[FG_VM_CHUNKS];
 struct { uint64_t pa; volatile uint64_t *va; void *handle; } pt_slabs[FG_VM_MAX_SLABS];
 unsigned pt_slab_count, pt_next_slot;
 uint64_t used[(FG_ABI_VA_SIZE>>12)/64];
 struct fg_abi_map_rec maps[FG_ABI_MAX_BOS];
 struct fg_abi_import_rec imports[FG_ABI_MAX_IMPORTS];
 uint32_t max_bos, max_imports;
 uint32_t seq, completed;
 uint32_t map_seq, submit_map_seq;
 unsigned in_use, inline_attached, poisoned, drained, timed_out, host_handoff, need_tlbi;
 uint64_t faults;
};

static struct fuxi_gpu_vm fg_vm_pool[FG_ABI_MAX_VMS];
static struct fuxi_gpu_vm *fg_default_vm, *fg_bb_vm, *fg_active_vm;
static uint64_t fg_hw_ttbr;
static uint32_t fg_bb_waiting;
static uint64_t fg_asid_bitmap[4];
static unsigned fg_last_tlb_asid;
static unsigned fg_soft_recover_count, fg_soft_recover_fail_count;
static unsigned fg_bb_recover_limit, fg_bb_recover_count;

#define FG_PID_QUOTA_SLOTS (FG_ABI_MAX_VMS * 4u)
struct fg_pid_quota {
 int in_use;
 int pid;
 uint32_t bos, imports;
 uint64_t wired;
};
static struct fg_pid_quota fg_pid_quotas[FG_PID_QUOTA_SLOTS];

static struct fg_pid_quota *fg_pid_quota_find(int pid, int create)
{
 if (pid <= 0) return NULL;
 struct fg_pid_quota *empty = NULL;
 for (unsigned i = 0; i < FG_PID_QUOTA_SLOTS; i++) {
  if (fg_pid_quotas[i].in_use && fg_pid_quotas[i].pid == pid) return &fg_pid_quotas[i];
  if (!fg_pid_quotas[i].in_use && !empty) empty = &fg_pid_quotas[i];
 }
 if (create && empty) {
  memset(empty, 0, sizeof(*empty));
  empty->in_use = 1;
  empty->pid = pid;
  return empty;
 }
 return NULL;
}

/* Backward-compat aliases into default VM when present (used by host tests & diagnostics). */
static struct fg_abi_import_rec fg_abi_dummy_imports[FG_ABI_MAX_IMPORTS];
static struct fg_abi_map_rec fg_abi_dummy_maps[FG_ABI_MAX_BOS];
static uint64_t fg_abi_dummy_used[(FG_ABI_VA_SIZE>>12)/64];
#define fg_abi_imports (fg_default_vm ? fg_default_vm->imports : fg_abi_dummy_imports)
#define fg_abi_maps (fg_default_vm ? fg_default_vm->maps : fg_abi_dummy_maps)
#define fg_abi_used (fg_default_vm ? fg_default_vm->used : fg_abi_dummy_used)

static inline uint32_t fg_vm_fence_off(const struct fuxi_gpu_vm *vm)
{
 return vm ? vm->fence_off : FG_ABI_FENCE_OFF;
}
#define FG_ABI_VM_FENCE_OFF(vm) fg_vm_fence_off(vm)

static inline void fg_asid_set_used(unsigned asid)
{
 asid &= 255u;
 fg_asid_bitmap[asid >> 6] |= UINT64_C(1) << (asid & 63u);
}
static inline void fg_asid_free_bit(unsigned asid)
{
 asid &= 255u;
 if (asid) fg_asid_bitmap[asid >> 6] &= ~(UINT64_C(1) << (asid & 63u));
}
static uint32_t fg_asid_alloc(void)
{
 for (uint32_t a = 1; a < 256u; a++) {
  if (!(fg_asid_bitmap[a >> 6] & (UINT64_C(1) << (a & 63u)))) {
   fg_asid_set_used(a);
   return a;
  }
 }
 return 0;
}

static unsigned fg_abi_host_handoff, fg_abi_ncimport, fg_abi_unstick, fg_abi_cohtest, fg_abi_tlbpages, fg_abi_mapwait, fg_abi_queue, fg_abi_roimport, fg_abi_pace;
/* Queue mode (bit 512): jobs in flight are seq-completed, at most this many; stats: queued, deepest chain, refused for a present, full. */
#define FG_ABI_QUEUE_MAX 4u
static uint32_t fg_abi_q_stats[6]; /* queued, deepest, refused for a present, full, refused by the hold, hold expired */
static unsigned fg_abi_hold;
static unsigned fg_abi_cf, fg_cf_holding; /* bit 16 copy priority, see fg_abi_copy_first */
static unsigned fg_abi_pk; /* bit 27 park instead of refuse, see fg_pk_park */
#define FG_PK_IDLE_US 1500u
static void fg_cf_reset(void), fg_pk_reset(void);
static int fg_pk_check(void);
/* Hook the userclient installs: wakes submits waiting out a refusal (NULL in host tests, where the macro is defined here). */
void (*fuxi_gpu_abi_wake_hook)(void);
#ifndef FG_ABI_WAKE
#define FG_ABI_WAKE() do { if (fuxi_gpu_abi_wake_hook) fuxi_gpu_abi_wake_hook(); } while (0)
#endif
static unsigned fg_abi_start;
static unsigned fg_abi_probe, fg_abi_spin, fg_abi_mid, fg_abi_ar, fg_abi_hard, fg_abi_dirty;
static uint64_t fg_abi_hold_since;
/* Ticks the last fuxi_gpu_abi_map spent after taking the GPU lock (the caller's total minus this is the wait for that lock). */
static uint64_t fg_abi_map_work;
static unsigned fg_irq_want;
static unsigned fg_abi_coh_wb;
static unsigned fg_abi_coh, fg_abi_coh_tries;
static unsigned fg_abi_cbo, fg_abi_cbo_on;
static const char *fg_abi_cbo_said;
#define FG_JS_OFF 1024u
#define FG_JS_WORDS 12u
#define FG_ABI_JOB_WORDS (FG_ABI_FRAME_WORDS+FG_JS_WORDS)
_Static_assert(FG_ABI_JOB_WORDS+8 < 8192, "one frame must fit the idle ring");
static uint32_t fg_abi_words[FG_ABI_JOB_WORDS];
static uint32_t fg_js_submit[8], fg_js_n, fg_js_bad, fg_js_on;
static uint64_t fg_js_sum[3], fg_js_max[3];
static unsigned fg_js_wrap(uint32_t *words, unsigned n, uint32_t seq)
{
 const uint32_t slot=FG_JS_OFF+(seq&7u)*16u, op=fg_abi_pkt7(0x3e,3), reg=0x980|(2u<<18)|(1u<<30);
 words[0]=words[4]; words[1]=words[5];
 words[2]=op; words[3]=reg; words[4]=G3_FENCE_VA+slot; words[5]=0;
 words[4+n]=fg_abi_pkt7(0x17,1); words[5+n]=1;
 words[6+n]=op; words[7+n]=reg; words[8+n]=G3_FENCE_VA+slot+8; words[9+n]=0;
 words[10+n]=fg_abi_pkt7(0x17,1); words[11+n]=3;
 g_wr(G3_FENCE_PA,slot,0); g_wr(G3_FENCE_PA,slot+8,0);
 fg_js_submit[seq&7u]=g3_gr(0x980);
 return n+FG_JS_WORDS;
}
static void fg_js_retire(uint32_t from, uint32_t to)
{
 for (uint32_t q=from+1; q-from<=to-from && q-from<=8; q++) {
  const uint32_t slot=FG_JS_OFF+(q&7u)*16u, s0=g3_read(G3_FENCE_PA+slot), s1=g3_read(G3_FENCE_PA+slot+8), now=g3_gr(0x980);
  if (fg_access_failed || !s0 || !s1) fg_js_bad++;
  else {
   const uint32_t v[3]={s0-fg_js_submit[q&7u], s1-s0, now-s1};
   for (unsigned i=0;i<3;i++) { fg_js_sum[i]+=v[i]; if (v[i]>fg_js_max[i]) fg_js_max[i]=v[i]; }
  }
  if (!(++fg_js_n % 600)) {
   const uint64_t good=600-fg_js_bad ? 600-fg_js_bad : 1;
   #define FG_JS_US(t) ((unsigned long long)((t)*10/192))
   printf("FUXI GPU ABI: job_stamps per 600 jobs submit_to_start_us avg %llu max %llu exec_us avg %llu max %llu end_to_observed_us avg %llu max %llu no_stamp %u\n",
       FG_JS_US(fg_js_sum[0]/good), FG_JS_US(fg_js_max[0]), FG_JS_US(fg_js_sum[1]/good), FG_JS_US(fg_js_max[1]), FG_JS_US(fg_js_sum[2]/good), FG_JS_US(fg_js_max[2]), fg_js_bad);
   #undef FG_JS_US
   memset(fg_js_sum,0,sizeof(fg_js_sum)); memset(fg_js_max,0,sizeof(fg_js_max)); fg_js_bad=0;
  }
 }
}
static uint32_t fg_abi_ibs[4];
static uint32_t fg_abi_stall_stats[8];
static unsigned fg_abi_stage, fg_abi_stall_lines;
static uint32_t fg_abi_map_seq, fg_abi_submit_map_seq;
static uint64_t fg_abi_cleaned, fg_abi_skipped, fg_abi_invalidated;
static uint64_t fg_abi_cc_bytes[2], fg_abi_cc_ticks[2], fg_abi_cc_bb[4], fg_abi_cc_bt[4], fg_abi_cc_go;
static void fg_abi_cc_note(unsigned permissions, uint64_t bytes, uint64_t ticks)
{
 unsigned c=(permissions&2) ? 0 : 1, b=bytes<(1u<<16) ? 0 : bytes<(1u<<20) ? 1 : bytes<(1u<<24) ? 2 : 3;
 fg_abi_cc_bytes[c]+=bytes; fg_abi_cc_ticks[c]+=ticks; fg_abi_cc_bb[b]+=bytes; fg_abi_cc_bt[b]+=ticks;
}
static void fg_abi_cache_region(uintptr_t source, uint64_t bytes, int complete)
{
 uintptr_t end=source+bytes;
 for (uintptr_t p=source;p<end;p+=64) {
  if (complete) { __asm__ volatile("dc ivac, %0" :: "r"(p) : "memory"); }
  else { __asm__ volatile("dc civac, %0" :: "r"(p) : "memory"); }
 }
 if (complete) fg_abi_invalidated+=bytes;
 else fg_abi_cleaned+=bytes;
}

#define FG_DTY_CHUNK 64u
#define FG_DTY_CHUNKS 512u
#ifndef FG_ABI_DTY_POKE
extern void pmap_clear_modify(ppnum_t pn);
extern boolean_t pmap_is_modified(ppnum_t pn);
extern boolean_t pmap_valid_page(ppnum_t pn);
#define FG_ABI_DTY_POKE(p) do { uint32_t v_; do { v_=*(volatile uint32_t *)(p); } while (!__sync_bool_compare_and_swap((volatile uint32_t *)(p),v_,v_)); } while (0)
#endif
static ppnum_t fg_abi_dty_ppn[FG_DTY_CHUNKS*FG_DTY_CHUNK];
static uint8_t fg_abi_dty_used[FG_DTY_CHUNKS];
static unsigned fg_abi_dty_ok;
static uint64_t fg_abi_dty_st[9];
static int fg_abi_dty_selftest(uintptr_t source, ppnum_t ppn)
{
 pmap_clear_modify(ppn);
 if (pmap_is_modified(ppn)) return 1;
 FG_ABI_DTY_POKE(source);
 if (!pmap_is_modified(ppn)) return 2;
 pmap_clear_modify(ppn);
 return pmap_is_modified(ppn) ? 3 : 0;
}
static void fg_abi_dty_attach_vm(struct fuxi_gpu_vm *vm, unsigned slot, uintptr_t source, uint64_t bytes)
{
 vm->imports[slot].dty_first=vm->imports[slot].dty_pages=0;
 if (!fg_abi_dirty || fg_abi_dty_ok==2 || fg_abi_coh==1) return;
 unsigned pages=(unsigned)(bytes>>PAGE_SHIFT), need=(pages+FG_DTY_CHUNK-1)/FG_DTY_CHUNK, first=FG_DTY_CHUNKS;
 if (!pages || need>FG_DTY_CHUNKS) return;
 for (unsigned c=0,run=0;c<FG_DTY_CHUNKS;c++) {
  run=fg_abi_dty_used[c] ? 0 : run+1;
  if (run==need) { first=c+1-need; break; }
 }
 if (first==FG_DTY_CHUNKS) return;
 ppnum_t *pp=&fg_abi_dty_ppn[first*FG_DTY_CHUNK];
 for (unsigned i=0;i<pages;i++) {
  ppnum_t ppn=pmap_find_phys(kernel_pmap,source+((uintptr_t)i<<PAGE_SHIFT));
  if (!ppn || !pmap_valid_page(ppn)) return;
  pp[i]=ppn;
 }
 if (!fg_abi_dty_ok) {
  int r=fg_abi_dty_selftest(source,pp[0]);
  fg_abi_dty_ok=r ? 2 : 1;
  printf("FUXI GPU ABI: import dirty tracking self-test %s (%d)\n",r ? "refused, full cleans stay" : "passed",r);
  if (r) return;
 }
 for (unsigned i=0;i<pages;i++) pmap_clear_modify(pp[i]);
 for (unsigned c=0;c<need;c++) fg_abi_dty_used[first+c]=1;
 vm->imports[slot].dty_first=first+1; vm->imports[slot].dty_pages=pages;
 fg_abi_dty_st[8]++;
}
static void fg_abi_dty_detach_vm(struct fuxi_gpu_vm *vm, unsigned slot)
{
 if (!vm->imports[slot].dty_first) return;
 unsigned first=vm->imports[slot].dty_first-1, need=(vm->imports[slot].dty_pages+FG_DTY_CHUNK-1)/FG_DTY_CHUNK;
 for (unsigned c=0;c<need;c++) fg_abi_dty_used[first+c]=0;
 vm->imports[slot].dty_first=vm->imports[slot].dty_pages=0;
}
static void fg_abi_dty_clean_vm(struct fuxi_gpu_vm *vm, unsigned slot)
{
 const ppnum_t *pp=&fg_abi_dty_ppn[(vm->imports[slot].dty_first-1)*FG_DTY_CHUNK];
 const uintptr_t source=vm->imports[slot].source;
 const unsigned pages=vm->imports[slot].dty_pages;
 uint64_t t0=rsr("cntpct_el0"), clean=0, dirty=0;
 for (unsigned i=0;i<pages;i++) {
  if (!pmap_is_modified(pp[i])) continue;
  uint64_t c0=rsr("cntpct_el0");
  pmap_clear_modify(pp[i]);
  fg_abi_cache_region(source+((uintptr_t)i<<PAGE_SHIFT),1u<<PAGE_SHIFT,0);
  clean+=rsr("cntpct_el0")-c0; dirty++;
 }
 uint64_t t1=rsr("cntpct_el0");
 fg_abi_dty_st[0]+=pages; fg_abi_dty_st[1]+=dirty; fg_abi_dty_st[2]+=dirty<<PAGE_SHIFT;
 fg_abi_dty_st[4]+=(t1-t0)-clean; fg_abi_dty_st[5]+=clean;
 fg_abi_skipped+=(uint64_t)(pages-dirty)<<PAGE_SHIFT;
 if (dirty) fg_abi_cc_note(vm->imports[slot].permissions,dirty<<PAGE_SHIFT,clean);
}
static void fg_abi_import_cache_vm(struct fuxi_gpu_vm *vm, int complete)
{
 if (!vm) return;
 if (complete && !vm->host_handoff) return;
 __asm__ volatile("dsb sy" ::: "memory");
 if (fg_abi_coh==1) {
  for (unsigned i=0;i<vm->max_imports;i++) if (vm->imports[i].iova) fg_abi_skipped+=vm->imports[i].bytes;
  vm->host_handoff=0;
  if (vm==fg_default_vm) fg_abi_host_handoff=0;
  return;
 }
 for (unsigned i=0;i<vm->max_imports;i++) if (vm->imports[i].iova && (!complete || (vm->imports[i].permissions&2))) {
  if (vm->imports[i].gpu_only) { fg_abi_skipped+=vm->imports[i].bytes; if (!complete) fg_abi_cc_go+=vm->imports[i].bytes; continue; }
  if (!complete && fg_abi_ncimport && (vm->imports[i].permissions&2)) {
   fg_abi_skipped+=vm->imports[i].bytes;
   continue;
  }
  if (!complete && vm->imports[i].dty_first && fg_abi_dty_ok==1) { fg_abi_dty_clean_vm(vm,i); continue; }
  uint64_t c0=rsr("cntpct_el0");
  fg_abi_cache_region(vm->imports[i].source,vm->imports[i].bytes,complete);
  if (!complete) { fg_abi_cc_note(vm->imports[i].permissions,vm->imports[i].bytes,rsr("cntpct_el0")-c0); if (fg_abi_dirty) fg_abi_dty_st[3]+=vm->imports[i].bytes; }
  else fg_abi_dty_st[6]+=rsr("cntpct_el0")-c0;
 }
 __asm__ volatile("dsb sy" ::: "memory");
 if (!complete) fg_abi_dty_st[7]++;
 vm->host_handoff=!complete;
 if (vm==fg_default_vm) fg_abi_host_handoff=vm->host_handoff;
}
void fuxi_gpu_import_stats(uint64_t *cleaned, uint64_t *skipped, uint64_t *invalidated,
    uint64_t *mapped, unsigned *imports)
{
 FG_GUARD;
 *cleaned=fg_abi_cleaned; *skipped=fg_abi_skipped; *invalidated=fg_abi_invalidated;
 *mapped=0; *imports=0;
 for (unsigned v=0;v<FG_ABI_MAX_VMS;v++) {
  if (!fg_vm_pool[v].in_use) continue;
  for (unsigned i=0;i<fg_vm_pool[v].max_imports;i++) if (fg_vm_pool[v].imports[i].iova) {
   *mapped+=fg_vm_pool[v].imports[i].bytes; ++*imports;
  }
 }
}
static void fg_abi_map_note_vm(struct fuxi_gpu_vm *vm, uint64_t iova, uint64_t bytes, uintptr_t source)
{
 ++fg_abi_map_seq;
 ++vm->map_seq;
 for (unsigned i=0;i<vm->max_bos;i++) if (!vm->maps[i].iova) {
  vm->maps[i].iova=iova; vm->maps[i].bytes=bytes; vm->maps[i].source=source; vm->maps[i].seq=vm->map_seq;
  return;
 }
}
static void fg_abi_map_forget_vm(struct fuxi_gpu_vm *vm, uint64_t iova, uint64_t bytes)
{
 for (unsigned i=0;i<vm->max_bos;i++) if (vm->maps[i].iova==iova && vm->maps[i].bytes==bytes) vm->maps[i].iova=0;
}
static const struct fg_abi_map_rec *fg_abi_map_at_vm(const struct fuxi_gpu_vm *vm, uint64_t iova)
{
 if (!vm) return NULL;
 for (unsigned i=0;i<vm->max_bos;i++)
  if (vm->maps[i].iova && iova>=vm->maps[i].iova && iova-vm->maps[i].iova<vm->maps[i].bytes) return &vm->maps[i];
 return NULL;
}
static const struct fg_abi_map_rec *fg_abi_map_at(uint64_t iova)
{
 const struct fuxi_gpu_vm *vm = fg_active_vm ? fg_active_vm : fg_default_vm;
 return fg_abi_map_at_vm(vm, iova);
}
static volatile uint64_t *fg_abi_leaf_vm(const struct fuxi_gpu_vm *vm, uint64_t iova)
{
 if (!vm || iova<FG_ABI_VA_START || iova>=FG_ABI_VA_START+FG_ABI_VA_SIZE) return NULL;
 unsigned chunk=(unsigned)((iova-FG_ABI_VA_START)>>21);
 volatile uint64_t *l3=vm->l3_va[chunk];
 return l3 ? l3+((iova>>12)&511) : NULL;
}
static volatile uint64_t *fg_abi_leaf(uint64_t iova)
{
 return fg_abi_leaf_vm(fg_default_vm, iova);
}

static inline unsigned fg_abi_pg(uint64_t iova) { return (unsigned)((iova-FG_ABI_VA_START)>>12); }
static inline int fg_abi_used_get_vm(const struct fuxi_gpu_vm *vm, unsigned pg) { return (vm->used[pg>>6]>>(pg&63))&1; }
static inline int fg_abi_used_get(unsigned pg) { return fg_default_vm ? fg_abi_used_get_vm(fg_default_vm, pg) : 0; }
static inline uint64_t fg_abi_used_mask(unsigned pg, unsigned n)
{
 unsigned lo=pg&63;
 return (n==64 ? ~UINT64_C(0) : ((UINT64_C(1)<<n)-1))<<lo;
}
static inline int fg_abi_used_any_vm(const struct fuxi_gpu_vm *vm, unsigned pg, unsigned n)
{
 for (unsigned k;n;pg+=k,n-=k) { k=64-(pg&63); if (k>n) k=n; if (vm->used[pg>>6]&fg_abi_used_mask(pg,k)) return 1; }
 return 0;
}
static inline void fg_abi_used_fill_vm(struct fuxi_gpu_vm *vm, unsigned pg, unsigned n, int on)
{
 for (unsigned k;n;pg+=k,n-=k) {
  k=64-(pg&63); if (k>n) k=n;
  if (on) vm->used[pg>>6]|=fg_abi_used_mask(pg,k); else vm->used[pg>>6]&=~fg_abi_used_mask(pg,k);
 }
}

#define FG_ABI_TLB_PAGE_LIMIT 512u
static int g3_sw64(unsigned off, uint64_t v) { return !fg_access_failed && !fg_access(GPU_SMMU_BASE+off,&v,1,1); }
static int fg_abi_tlb_pages_asid(uint64_t iova, uint64_t bytes, unsigned asid)
{
 unsigned cb=g_cb(g3_bank);
 __asm__ volatile("dsb sy" ::: "memory");
 for (uint64_t off=0;off<bytes;off+=4096)
  if (!g3_sw64(cb+0x620,(((iova+off)>>12)&((UINT64_C(1)<<44)-1))|((uint64_t)asid<<48))) return 0xe01;
 if (!g3_sw(cb+0x7f0,0)) return 0xe01;
 for (unsigned i=0;i<1000;i++) {
  uint32_t st=g3_sr(cb+0x7f4);
  if (fg_access_failed) return 0xe01;
  if (!(st&1)) return 0;
  delay(1);
 }
 return 0xe03;
}
static int fg_abi_tlb_asid(unsigned asid)
{
 uint32_t tlb[128] = {0};
 fg_last_tlb_asid = asid;
#ifdef FUXI_GPU_XNU
 if (asid != g3_asid) return g4_tlb_asid(tlb, asid);
#endif
 return g4_tlb(tlb);
}
static int fg_abi_switch_vm(struct fuxi_gpu_vm *vm)
{
 unsigned cb = g_cb(g3_bank);
 uint64_t root_pa = vm ? vm->root_pa : G3_PT;
 uint32_t asid = vm ? vm->asid : g3_asid;
 uint64_t ttbr = root_pa | ((uint64_t)asid << 48);
 int need_switch = (fg_hw_ttbr != ttbr);
 int need_tlb = !vm || vm->need_tlbi;
 __asm__ volatile("dsb sy" ::: "memory");
 if (need_switch) {
  if (!g3_sw(cb+0x20, (uint32_t)ttbr) || !g3_sw(cb+0x24, (uint32_t)(ttbr>>32)) ||
      !g3_sw(cb+0x2c, asid << 16)) return 0;
 }
 if (need_tlb) {
  if (fg_abi_tlb_asid(asid)) return 0;
  if (vm) { vm->need_tlbi = 0; vm->submit_map_seq = vm->map_seq; }
 }
 if (fg_access_failed || g3_sr(cb+0x20)!=(uint32_t)ttbr || g3_sr(cb+0x24)!=(uint32_t)(ttbr>>32)) return 0;
 fg_hw_ttbr = ttbr;
 fg_root_client = (vm && vm == fg_bb_vm && vm->inline_attached) ? 1u : 0u;
 return 1;
}

static int fg_vm_alloc_pt_page(struct fuxi_gpu_vm *vm, uint64_t *pa_out, volatile uint64_t **va_out)
{
 unsigned slab_idx = vm->pt_next_slot >> 2;
 unsigned sub = vm->pt_next_slot & 3u;
 if (slab_idx >= vm->pt_slab_count) {
  if (vm->pt_slab_count >= FG_VM_MAX_SLABS) return 0;
  uint64_t spa = 0;
  volatile uint64_t *sva = NULL;
  void *sh = NULL;
  if (!FG_PT_ALLOC(&spa, &sva, &sh)) return 0;
  if (!spa || (spa & 16383u) || spa + 16384u > (UINT64_C(1) << FG_ABI_PA_BITS) || !sva) {
   if (sh) FG_PT_FREE(sh);
   return 0;
  }
  memset((void *)(uintptr_t)sva, 0, 16384);
  FG_PT_PUBLISH(sva, 16384);
  vm->pt_slabs[vm->pt_slab_count].pa = spa;
  vm->pt_slabs[vm->pt_slab_count].va = sva;
  vm->pt_slabs[vm->pt_slab_count].handle = sh;
  vm->pt_slab_count++;
 }
 *pa_out = vm->pt_slabs[slab_idx].pa + (uint64_t)sub * 4096u;
 *va_out = (volatile uint64_t *)((uintptr_t)vm->pt_slabs[slab_idx].va + (uintptr_t)sub * 4096u);
 vm->pt_next_slot++;
 return 1;
}
static void fg_vm_free_all_pt(struct fuxi_gpu_vm *vm)
{
 for (unsigned i = 0; i < vm->pt_slab_count; i++) {
  if (vm->pt_slabs[i].handle) FG_PT_FREE(vm->pt_slabs[i].handle);
  vm->pt_slabs[i].handle = NULL;
  vm->pt_slabs[i].va = NULL;
  vm->pt_slabs[i].pa = 0;
 }
 vm->pt_slab_count = 0;
 vm->pt_next_slot = 0;
}

#define FG_IBP_OFF 1536u
#define FG_IBP_EVERY 96u
#define FG_IBP_TOP 6u
#define FG_IBP_BUDGET 24576u
#define FG_IBP_IB_BUDGET 4096u
#define FG_IBP_STAMP_WORDS 5u
_Static_assert(FG_IBP_OFF >= FG_JS_OFF+8u*16u+16u, "IB stamps above the job stamps");
_Static_assert(FG_IBP_OFF+16u*(NativeAdrenoAbiMaxBuffers+1u) <= 4096u, "IB stamps fit the fence page");
enum { FG_IBP_K_EMPTY, FG_IBP_K_FULL, FG_IBP_K_PART, FG_IBP_K_BLIT, FG_IBP_K_CS, FG_IBP_K_BAD, FG_IBP_KINDS };
struct fg_ibp_ib {
 uint32_t dw, ib2, ib2dw, draws, blits, blit_kpx, cs, wfi, waits, sds, rps, rp_kpx, rt_w, rt_h, rt_info, rt_pitch, d2_info, s2_info, s2_size,
     tl2, br2, ev, ev_ccu, ev_cache, wfm, bc, clr_kpx, cp_kpx[4], big_kpx, big_s2, big_s2size, big_d2, big_bc,
     flags,
     udraws;
};
static struct fg_ibp_ib fg_ibp_ibs[NativeAdrenoAbiMaxBuffers];
static unsigned fg_ibp_on, fg_ibp_count, fg_ibp_active, fg_ibp_scanned;
static uint32_t fg_ibp_seq, fg_ibp_jobs, fg_ibp_samples, fg_ibp_bad, fg_ibp_stamp[NativeAdrenoAbiMaxBuffers+1u], fg_ibp_d[NativeAdrenoAbiMaxBuffers];
static uint64_t fg_ibp_kind_t[FG_IBP_KINDS], fg_ibp_kind_n[FG_IBP_KINDS], fg_ibp_span_t;
static uint32_t fg_ibp_words[FG_ABI_JOB_WORDS+FG_IBP_STAMP_WORDS*(NativeAdrenoAbiMaxBuffers+1u)];
#define FG_IBP_SEEN 128u
#define FG_IBP_PIDS 6u
extern int proc_selfpid(void);
extern void proc_selfname(char *buf, int size);
static uint64_t fg_ibp_seen[FG_IBP_SEEN], fg_ibp_t0;
static unsigned fg_ibp_nseen;
static int fg_ibp_pid, fg_ibp_rate_pid[FG_IBP_PIDS];
static uint32_t fg_ibp_rate_n[FG_IBP_PIDS], fg_ibp_rate_other;
static char fg_ibp_name[24];
static void fg_ibp_reset(void)
{
 fg_ibp_count=fg_ibp_active=fg_ibp_scanned=0; fg_ibp_seq=fg_ibp_jobs=fg_ibp_samples=fg_ibp_bad=0; fg_ibp_span_t=0;
 memset(fg_ibp_kind_t,0,sizeof(fg_ibp_kind_t)); memset(fg_ibp_kind_n,0,sizeof(fg_ibp_kind_n));
 memset(fg_ibp_rate_pid,0,sizeof(fg_ibp_rate_pid)); memset(fg_ibp_rate_n,0,sizeof(fg_ibp_rate_n)); fg_ibp_rate_other=0; fg_ibp_t0=mach_absolute_time();
}
static int fg_ibp_first(struct fg_ibp_ib *r, uint64_t iova)
{
 for (unsigned i=0;i<fg_ibp_nseen;i++) if (fg_ibp_seen[i]==iova) return 0;
 if (fg_ibp_nseen<FG_IBP_SEEN) fg_ibp_seen[fg_ibp_nseen++]=iova; else r->flags|=64;
 return 1;
}
static void fg_ibp_rate(void)
{
 const int pid=proc_selfpid();
 for (unsigned i=0;i<FG_IBP_PIDS;i++) {
  if (fg_ibp_rate_n[i] && fg_ibp_rate_pid[i]==pid) { fg_ibp_rate_n[i]++; return; }
  if (!fg_ibp_rate_n[i]) { fg_ibp_rate_pid[i]=pid; fg_ibp_rate_n[i]=1; return; }
 }
 fg_ibp_rate_other++;
}
static int fg_ibp_want(const struct fuxi_gpu_vm *vm)
{
 if (!fg_ibp_on || fg_abi_pk) return 0;
 if (fg_ibp_active && (int32_t)(vm->completed-fg_ibp_seq)>=0) { fg_ibp_active=0; fg_ibp_bad++; }
 return !fg_ibp_active && fg_ibp_jobs%FG_IBP_EVERY==FG_IBP_EVERY-1u;
}
static int fg_ibp_rd(const struct fg_abi_map_rec *m, uint64_t iova, uint32_t *w)
{
 uint64_t v=0;
 if (iova<m->iova || iova-m->iova+4>m->bytes || !m->source || fuxi_gpu_access(m->source+(uintptr_t)(iova-m->iova),&v,0,0)) return 0;
 *w=(uint32_t)v;
 return 1;
}
static void fg_ibp_scan(const struct fuxi_gpu_vm *vm, struct fg_ibp_ib *r, uint64_t iova, uint32_t dwords, unsigned depth, unsigned *budget, int uniq)
{
 const struct fg_abi_map_rec *m=fg_abi_map_at_vm(vm,iova);
 if (!m) { r->flags|=1; return; }
 for (uint32_t i=0;i<dwords;) {
  uint32_t h=0;
  if (!*budget) { r->flags|=2; return; }
  if (!fg_ibp_rd(m,iova+4ull*i,&h)) { r->flags|=4; return; }
  --*budget;
  if (h>>28==4u) {
   const uint32_t reg=(h>>8)&0x7ffffu, cnt=h&0x7fu;
   for (uint32_t k=0;k<cnt && i+1+k<dwords;k++) {
    const uint32_t a=reg+k;
    uint32_t v=0;
    if (a!=0x8822 && a!=0x8823 && a!=0x80f1 && a!=0x8c17 && a!=0x8405 && a!=0x8406 && a!=0xb2c0 && a!=0xb2c1 && a!=0x8400) continue;
    if (!*budget) { r->flags|=2; return; }
    if (!fg_ibp_rd(m,iova+4ull*(i+1+k),&v)) { r->flags|=4; return; }
    --*budget;
    switch (a) {
     case 0x8822: r->rt_info=v; break;
     case 0x8823: r->rt_pitch=v; break;
     case 0x80f1: r->rps++; r->rt_w=(v&0x3fffu)+1; r->rt_h=((v>>16)&0x3fffu)+1; r->rp_kpx+=r->rt_w*r->rt_h/1024; break;
     case 0x8c17: r->d2_info=v; break;
     case 0x8405: r->tl2=v; break;
     case 0x8406: r->br2=v; break;
     case 0xb2c0: r->s2_info=v; break;
     case 0xb2c1: r->s2_size=v; break;
     case 0x8400: r->bc=v; break;
    }
   }
   i+=1+cnt;
  } else if (h>>28==7u) {
   const uint32_t op=(h>>16)&0x7fu, cnt=h&0x3fffu;
   switch (op) {
    case 0x38: case 0x28: case 0x29: case 0x2a: case 0x24: r->draws++; if (uniq) r->udraws++; break;
    case 0x2c: {
     const uint32_t x0=r->tl2&0x3fffu, y0=(r->tl2>>16)&0x3fffu, x1=r->br2&0x3fffu, y1=(r->br2>>16)&0x3fffu;
     const uint32_t kpx=x1>=x0 && y1>=y0 ? (x1-x0+1)*(y1-y0+1)/1024 : 0;
     r->blits++; r->blit_kpx+=kpx;
     if (r->bc&0x80u) r->clr_kpx+=kpx;
     else r->cp_kpx[(((r->s2_info>>8)&3u) ? 2u : 0u)+(((r->d2_info>>8)&3u) ? 1u : 0u)]+=kpx;
     if (kpx>r->big_kpx) { r->big_kpx=kpx; r->big_s2=r->bc&0x80u ? 0 : r->s2_info; r->big_s2size=r->bc&0x80u ? 0 : r->s2_size; r->big_d2=r->d2_info; r->big_bc=r->bc; }
     break; }
    case 0x33: case 0x41: r->cs++; break;
    case 0x26: r->wfi++; break;
    case 0x3c: case 0x12: case 0x14: r->waits++; break;
    case 0x13: r->wfm++; break;
    case 0x46: {
     uint32_t ev=0;
     r->ev++;
     if (cnt && i+1<dwords && *budget) {
      if (!fg_ibp_rd(m,iova+4ull*(i+1),&ev)) { r->flags|=4; return; }
      --*budget; ev&=0xffu;
      if (ev==0x18 || ev==0x19 || ev==0x1a || ev==0x1c || ev==0x1d || ev==0x20 || ev==0x21) r->ev_ccu++;
      else if (ev>=0x30 && ev<=0x33) r->ev_cache++;
     }
     break; }
    case 0x43: r->sds++; break;
    case 0x57: r->flags|=16; break;
    case 0x3f:
     if (cnt==3 && i+3<dwords) {
      uint32_t lo=0, hi=0, sz=0;
      if (*budget<3) { r->flags|=2; return; }
      if (!fg_ibp_rd(m,iova+4ull*(i+1),&lo) || !fg_ibp_rd(m,iova+4ull*(i+2),&hi) || !fg_ibp_rd(m,iova+4ull*(i+3),&sz)) { r->flags|=4; return; }
      *budget-=3; sz&=0xfffffu;
      r->ib2++; r->ib2dw+=sz;
      if (!depth) { const uint64_t a2=(uint64_t)hi<<32|lo; const int u2=uniq && fg_ibp_first(r,a2); fg_ibp_scan(vm,r,a2,sz,1,budget,u2); } else r->flags|=32;
     }
     break;
   }
   i+=1+cnt;
  } else { r->flags|=8; return; }
 }
}
static void fg_ibp_decode(const struct fuxi_gpu_vm *vm, const uint64_t *iovas, const uint64_t *sizes, unsigned count)
{
 unsigned left=FG_IBP_BUDGET;
 fg_ibp_nseen=0;
 fg_ibp_pid=proc_selfpid(); proc_selfname(fg_ibp_name,(int)sizeof(fg_ibp_name));
 for (unsigned k=0;k<count;k++) {
  struct fg_ibp_ib *r=&fg_ibp_ibs[k];
  unsigned b=left<FG_IBP_IB_BUDGET ? left : FG_IBP_IB_BUDGET, b0=b;
  memset(r,0,sizeof(*r)); r->dw=(uint32_t)(sizes[k]/4);
  fg_ibp_scan(vm,r,iovas[k],r->dw,0,&b,1);
  left-=b0-b;
 }
 fg_ibp_scanned=FG_IBP_BUDGET-left;
}
static unsigned fg_ibp_insert(const uint32_t *in, unsigned n, const uint64_t *iovas, const uint64_t *sizes, unsigned count)
{
 const uint32_t ib=fg_abi_pkt7(0x3f,3), wfi=fg_abi_pkt7(0x26,0), r2m=fg_abi_pkt7(0x3e,3), reg=0x980|(2u<<18)|(1u<<30);
 unsigned o=0, k=0;
#define FG_IBP_STAMP(j) do { fg_ibp_words[o++]=wfi; fg_ibp_words[o++]=r2m; fg_ibp_words[o++]=reg; fg_ibp_words[o++]=G3_FENCE_VA+FG_IBP_OFF+16u*(j); fg_ibp_words[o++]=0; } while (0)
 for (unsigned i=0;i<n;i++) {
  if (k<count && i+3<n && in[i]==ib && in[i+1]==(uint32_t)iovas[k] && !in[i+2] && in[i+3]==(uint32_t)(sizes[k]/4)) {
   FG_IBP_STAMP(k);
   for (unsigned j=0;j<4;j++) fg_ibp_words[o++]=in[i+j];
   i+=3;
   if (++k==count) FG_IBP_STAMP(count);
   continue;
  }
  fg_ibp_words[o++]=in[i];
 }
#undef FG_IBP_STAMP
 if (k!=count) return 0;
 for (unsigned j=0;j<=count;j++) { g_wr(G3_FENCE_PA,FG_IBP_OFF+16u*j,0); g_wr(G3_FENCE_PA,FG_IBP_OFF+16u*j+4,0); }
 return o;
}
static unsigned fg_ibp_kind(const struct fg_ibp_ib *r)
{
 if (r->flags&7u) return FG_IBP_K_BAD;
 if (r->draws) return r->rt_w*r->rt_h>=2000000u ? FG_IBP_K_FULL : FG_IBP_K_PART;
 if (r->blits) return FG_IBP_K_BLIT;
 if (r->cs) return FG_IBP_K_CS;
 return FG_IBP_K_EMPTY;
}
#define FG_IBP_US(t) ((unsigned long long)((t)*10/192))
static void fg_ibp_retire(uint32_t from, uint32_t to)
{
 if (!fg_ibp_active || fg_ibp_seq-from-1u>=to-from) return;
 fg_ibp_active=0;
 const unsigned n=fg_ibp_count;
 {
  uint32_t dr=0, udr=0, rps=0, bl=0, cs=0, ib2=0, fl=0;
  for (unsigned i=0;i<n;i++) { const struct fg_ibp_ib *r=&fg_ibp_ibs[i]; dr+=r->draws; udr+=r->udraws; rps+=r->rps; bl+=r->blits; cs+=r->cs; ib2+=r->ib2; fl|=r->flags; }
  printf("FUXI IBPROF: job %u totals pid %d %s ibs %u dr %u udr %u ib2 %u seen %u rp %u bl %u cs %u fl %x\n",
      fg_ibp_seq,fg_ibp_pid,fg_ibp_name,n,dr,udr,ib2,fg_ibp_nseen,rps,bl,cs,fl);
 }
 unsigned bad=0;
 for (unsigned i=0;i<=n;i++) { fg_ibp_stamp[i]=g3_read(G3_FENCE_PA+FG_IBP_OFF+16u*i); if (fg_access_failed || !fg_ibp_stamp[i]) bad++; }
 for (unsigned i=0;i<n && !bad;i++) { fg_ibp_d[i]=fg_ibp_stamp[i+1]-fg_ibp_stamp[i]; if ((int32_t)fg_ibp_d[i]<0) bad++; }
 if (bad) { fg_ibp_bad++; printf("FUXI IBPROF: job %u ibs %u unusable stamps %u\n",fg_ibp_seq,n,bad); return; }
 uint64_t kt[FG_IBP_KINDS]={0};
 const uint64_t span=fg_ibp_stamp[n]-fg_ibp_stamp[0];
 for (unsigned i=0;i<n;i++) { const unsigned k=fg_ibp_kind(&fg_ibp_ibs[i]); kt[k]+=fg_ibp_d[i]; fg_ibp_kind_t[k]+=fg_ibp_d[i]; fg_ibp_kind_n[k]++; }
 fg_ibp_samples++; fg_ibp_span_t+=span;
 printf("FUXI IBPROF: job %u ibs %u span_us %llu scanned_dw %u us empty/full_rt/part_rt/blit/compute/unread %llu/%llu/%llu/%llu/%llu/%llu\n",
     fg_ibp_seq,n,FG_IBP_US(span),fg_ibp_scanned,FG_IBP_US(kt[0]),FG_IBP_US(kt[1]),FG_IBP_US(kt[2]),FG_IBP_US(kt[3]),FG_IBP_US(kt[4]),FG_IBP_US(kt[5]));
 uint8_t used[NativeAdrenoAbiMaxBuffers]={0};
 for (unsigned t=0;t<FG_IBP_TOP && t<n;t++) {
  unsigned best=n;
  for (unsigned i=0;i<n;i++) if (!used[i] && (best==n || fg_ibp_d[i]>fg_ibp_d[best])) best=i;
  used[best]=1;
  const struct fg_ibp_ib *r=&fg_ibp_ibs[best];
  printf("FUXI IBPROF: job %u ib %u/%u %lluus dw %u ib2 %u/%u dr %u rp %u/%uK rt %ux%u info %x pitch %u bl %u/%uK d2 %x s2 %x/%x cs %u wfi %u wt %u wfm %u ev %u/%u/%u sds %u fl %x\n",
      fg_ibp_seq,best,n,FG_IBP_US(fg_ibp_d[best]),r->dw,r->ib2,r->ib2dw,r->draws,r->rps,r->rp_kpx,r->rt_w,r->rt_h,r->rt_info,r->rt_pitch,
      r->blits,r->blit_kpx,r->d2_info,r->s2_info,r->s2_size,r->cs,r->wfi,r->waits,r->wfm,r->ev,r->ev_ccu,r->ev_cache,r->sds,r->flags);
  if (r->blits)
   printf("FUXI IBPROF: job %u ib %u blits clr %uK cp ll/lt/tl/tt %u/%u/%u/%uK big %uK s2 %x/%x d2 %x bc %x\n",
       fg_ibp_seq,best,r->clr_kpx,r->cp_kpx[0],r->cp_kpx[1],r->cp_kpx[2],r->cp_kpx[3],r->big_kpx,r->big_s2,r->big_s2size,r->big_d2,r->big_bc);
 }
 if (!(fg_ibp_samples%16u)) {
  printf("FUXI IBPROF: per 16 samples span_us avg %llu; us/job (IBs/job) empty %llu (%llu) full_rt %llu (%llu) part_rt %llu (%llu) blit %llu (%llu) compute %llu (%llu) unread %llu (%llu); unusable %u\n",
      FG_IBP_US(fg_ibp_span_t/16),FG_IBP_US(fg_ibp_kind_t[0]/16),(unsigned long long)(fg_ibp_kind_n[0]/16),FG_IBP_US(fg_ibp_kind_t[1]/16),(unsigned long long)(fg_ibp_kind_n[1]/16),
      FG_IBP_US(fg_ibp_kind_t[2]/16),(unsigned long long)(fg_ibp_kind_n[2]/16),FG_IBP_US(fg_ibp_kind_t[3]/16),(unsigned long long)(fg_ibp_kind_n[3]/16),FG_IBP_US(fg_ibp_kind_t[4]/16),(unsigned long long)(fg_ibp_kind_n[4]/16),
      FG_IBP_US(fg_ibp_kind_t[5]/16),(unsigned long long)(fg_ibp_kind_n[5]/16),fg_ibp_bad);
  fg_ibp_span_t=0; fg_ibp_bad=0; memset(fg_ibp_kind_t,0,sizeof(fg_ibp_kind_t)); memset(fg_ibp_kind_n,0,sizeof(fg_ibp_kind_n));
  uint64_t now=mach_absolute_time(), ns=0;
  absolutetime_to_nanoseconds(now-fg_ibp_t0,&ns);
  printf("FUXI IBPROF: rate %llu ms jobs pid=n %d=%u %d=%u %d=%u %d=%u %d=%u %d=%u other %u\n",(unsigned long long)(ns/1000000),
      fg_ibp_rate_pid[0],fg_ibp_rate_n[0],fg_ibp_rate_pid[1],fg_ibp_rate_n[1],fg_ibp_rate_pid[2],fg_ibp_rate_n[2],
      fg_ibp_rate_pid[3],fg_ibp_rate_n[3],fg_ibp_rate_pid[4],fg_ibp_rate_n[4],fg_ibp_rate_pid[5],fg_ibp_rate_n[5],fg_ibp_rate_other);
  memset(fg_ibp_rate_pid,0,sizeof(fg_ibp_rate_pid)); memset(fg_ibp_rate_n,0,sizeof(fg_ibp_rate_n)); fg_ibp_rate_other=0; fg_ibp_t0=now;
 }
}
#undef FG_IBP_US

static void fg_abi_peek(const char *tag, uint64_t at, uint64_t start)
{
 const struct fg_abi_map_rec *m=fg_abi_map_at(at);
 if (!m) { printf("FUXI GPU STALL: %s IB %#llx not in a mapped range\n",tag,(unsigned long long)at); return; }
 if (start<m->iova || start>at) start=m->iova;
 uint64_t room=(m->iova+m->bytes-start)/4;
 uint32_t w[16]; unsigned bad=0;
 for (unsigned i=0;i<16;i++) {
  uint64_t v=0;
  if (i>=room || fuxi_gpu_access(m->source+(start-m->iova)+4*i,&v,0,0)) { w[i]=0xdeadbeef; bad++; } else w[i]=(uint32_t)v;
 }
 printf("FUXI GPU STALL: %s at %#llx from %#llx map %#llx seq %u/%u unread %u: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
     tag,(unsigned long long)at,(unsigned long long)start,(unsigned long long)m->iova,m->seq,fg_abi_submit_map_seq,bad,
     w[0],w[1],w[2],w[3],w[4],w[5],w[6],w[7],w[8],w[9],w[10],w[11],w[12],w[13],w[14],w[15]);
}
static void fg_abi_snapshot(const char *why)
{
 static const unsigned regs[]={0x210,0x211,0x212,0x213,0x201,0x821,0x825,0x824,0x806,0x807,0x928,0x929,0x92a,0x943,0x92b,0x92c,0x92d,
     0xa66,0xa6d,0xa70,0xa74,0xa71,0xa72};
 uint32_t v[sizeof(regs)/sizeof(regs[0])];
 for (unsigned i=0;i<sizeof(regs)/sizeof(regs[0]);i++) v[i]=g3_gr(regs[i]);
 unsigned cb=g_cb(g3_bank);
 const uint32_t seq_v = fg_active_vm ? fg_active_vm->seq : fg_abi_seq;
 printf("FUXI GPU STALL: %s seq %u rbbm %08x %08x %08x %08x int %08x hwfault %08x cpst1 %08x prot %08x rptr %x wptr %x\n",why,seq_v,
     v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9]);
 printf("FUXI GPU STALL: ib1 %08x:%08x rem %x init %x ib2 %08x:%08x rem %x bv rptr %x ib1 lo %08x rem %x ib2 %08x:%08x rem %x\n",
     v[11],v[10],v[12],v[13],v[15],v[14],v[16],v[17],v[18],v[19],v[22],v[21],v[20]);
 printf("FUXI GPU STALL: smmu fsr %08x far %08x:%08x fsynr0 %08x\n",g3_sr(cb+0x58),g3_sr(cb+0x64),g3_sr(cb+0x60),g3_sr(cb+0x68));
 const uint64_t ib1=(uint64_t)v[11]<<32|v[10], bv1=(uint64_t)v[11]<<32|v[18], bv2=(uint64_t)v[22]<<32|v[21];
 const uint32_t reached=v[13]>=v[12] ? v[13]-v[12] : 0;
 const uint64_t br=ib1+4ull*(reached ? reached-1 : 0);
 fg_abi_peek("BR-IB1",br,br-48);
 fg_abi_peek("BV-IB1",bv1,bv1-48);
 fg_abi_peek("BV-IB2",bv2,bv2-48);
}
static unsigned fg_abi_hang_dumped;
static void fg_abi_hang_dump_for(const char *why, const struct fuxi_gpu_vm *vm)
{
 const uint32_t seq_v = vm ? vm->seq : fg_abi_seq;
 const uint32_t comp_v = vm ? vm->completed : fg_abi_completed;
 if (vm && vm->client_class == FG_CLASS_APP) {
  if (fg_soft_recover_count >= 8) return;
  printf("FUXI GPU FAULT (Class 1 VM id=%u pid=%d name=%s asid=%u): %s seq %u completed %u busy %u expected-wptr 0x%x\n",
      vm->id, vm->pid, vm->name, vm->asid, why, seq_v, comp_v, fg_client_busy, g3_wptr);
  fg_abi_snapshot("class1-fault");
  return;
 }
 if (vm && vm->client_class == FG_CLASS_COMPOSITOR &&
     fg_bb_recover_limit > 0 && fg_bb_recover_count < fg_bb_recover_limit &&
     fg_abi_live && fuxi_gpu_ready()) {
  printf("FUXI GPU FAULT (Class 0 VM id=%u pid=%d name=%s asid=%u attempt=%u/%u): %s seq %u completed %u busy %u expected-wptr 0x%x\n",
      vm->id, vm->pid, vm->name, vm->asid, fg_bb_recover_count + 1, fg_bb_recover_limit,
      why, seq_v, comp_v, fg_client_busy, g3_wptr);
  fg_abi_snapshot("class0-fault");
  return;
 }
 if (fg_abi_hang_dumped) return;
 fg_abi_hang_dumped=1;
 printf("FUXI GPU HANG: %s seq %u completed %u busy %u expected-wptr 0x%x\n",
     why,seq_v,comp_v,fg_client_busy,g3_wptr);
 static const struct { const char *name; unsigned reg; } regs[] = {
  {"CP_RB_RPTR",0x806},{"CP_RB_WPTR",0x807},
  {"CP_IB1_BASE_LO",0x928},{"CP_IB1_BASE_HI",0x929},
  {"CP_IB1_REM_SIZE",0x92a},{"CP_IB1_INIT_SIZE",0x943},
  {"CP_STATUS_1",0x825},{"CP_HW_FAULT",0x821},
  {"CP_INTERRUPT_STATUS",0x823},{"CP_PROTECT_STATUS",0x824},
  {"RBBM_STATUS",0x210},{"RBBM_STATUS3",0x213},{"RBBM_INT_0_STATUS",0x201},
 };
 for (unsigned i=0;i<sizeof(regs)/sizeof(regs[0]);i++)
  printf("FUXI GPU HANG: %s [0x%x]=0x%x\n",regs[i].name,regs[i].reg,g3_gr(regs[i].reg));
 unsigned cb=g_cb(g3_bank);
 printf("FUXI GPU HANG: SMMU GPU CB %u TTBR 0x%x:%08x FSR 0x%x FAR 0x%x:%08x FSYNR0 0x%x FSYNR1 0x%x GFSR 0x%x\n",
     g3_bank,g3_sr(cb+0x24),g3_sr(cb+0x20),g3_sr(cb+0x58),
     g3_sr(cb+0x64),g3_sr(cb+0x60),g3_sr(cb+0x68),g3_sr(cb+0x6c),g3_sr(0x48));
 printf("FUXI GPU HANG: MMIO-access-failed %u; isolating\n",fg_access_failed);
 fg_abi_snapshot("hang");
}

#ifndef FG_SOFT_RECOVER_HW
#ifdef FUXI_GPU_XNU
static int fg_soft_recover_hw(struct fuxi_gpu_vm *guilty_vm)
{
 fg_irq_quiesce_soft();
 unsigned cb = g_cb(g3_bank);

 /* Unblock any stalled SMMU context bank transaction (FSR.SS) BEFORE GBIF bus drain,
  * otherwise AXI stall keeps 0x3c46 drain acks blocked until timeout. */
 uint32_t fsr = g3_sr(cb + 0x58);
 if (fg_access_failed) return 0;
 if (fsr & (1u << 30)) {
  if (!g3_sw(cb + 0x08, 1)) return 0; /* CB_RESUME terminate */
 }
 if (fsr && !g3_sw(cb + 0x58, fsr)) return 0;
 if (guilty_vm && fg_abi_tlb_asid(guilty_vm->asid)) return 0;

 uint64_t v = 1;
 int good = !fg_access(GPU_REG_BASE + 0x3c45*4, &v, 1, 0);
 for (unsigned step = 1; step <= 2 && good; step++) {
  unsigned i;
  for (i = 0; i < 1000; i++) {
   v = 0;
   if (fg_access(GPU_REG_BASE + 0x3c46*4, &v, 0, 0)) { good = 0; break; }
   if (v & step) break;
   delay(1);
  }
  if (i == 1000) good = 0;
  if (step == 1 && good) {
   v = 2;
   if (fg_access(GPU_REG_BASE + 0x3c45*4, &v, 1, 0)) good = 0;
  }
 }
 v = 0;
 if (!good || fg_access(GPU_REG_BASE + 0x3c45*4, &v, 1, 0)) return 0;

 /* Switch CB2 to safe G3_PT before pulsing GPU SW reset. */
 if (!fg_abi_switch_vm(NULL)) return 0;

 /* Pulse RBBM_SW_RESET (never leave S2CR faulted). */
 v = 1;
 if (fg_access(GPU_REG_BASE + 0x43*4, &v, 1, 0)) return 0;
 delay(100);
 v = 0;
 if (fg_access(GPU_REG_BASE + 0x43*4, &v, 1, 0)) return 0;
 delay(20);
 if (fg_suspends != UINT64_MAX) ++fg_suspends;

 /* Re-program CP/UCHE/RB registers and restart SQE on G3_PT. */
 if (!g3_hw_init()) return 0;
 g3_wptr = 0;
 g_zero(G3_PTR_PA, 16);
 if (!g3_gw(0x807, 0) || !g3_gw(G3_SQE_CNTL, 1)) return 0;
 delay(50);
 if (fg_access_failed || g3_faulted()) return 0;

 g3_emit(g3_pkt7(0x17, 1)); g3_emit(1u << 27);
 g3_emit(g3_pkt7(0x48, 4)); g3_emit(0x4b); g3_emit(3); g3_emit(0x20000000); g3_emit(2);
 __asm__ volatile("dsb sy" ::: "memory");
 if (!g3_gw(0x807, g3_wptr)) return 0;
 if (g3_wait(0)) return 0;

 /* Re-apply SM8550 UBWC NC settings and CMP_DBG bit 11. */
 uint32_t cmp = g3_gr(G4_RB_CMP_DBG);
 if (fg_access_failed || !g3_gw(G4_RB_CMP_DBG, cmp | (1u<<11))) return 0;
 static const struct { unsigned reg; uint32_t val; } nc_writes[] = {
  {G4_RB_NC, 0x816}, {G4_TPL1_NC, 6}, {G4_SP_NC, 0x26},
  {G3_APERTURE_HOST, 1u<<12}, {G4_GRAS_NC, 0x60},
  {G3_APERTURE_HOST, 2u<<12}, {G4_GRAS_NC, 0x60},
  {G3_APERTURE_HOST, 0},
 };
 for (unsigned i = 0; i < sizeof(nc_writes)/sizeof(nc_writes[0]); i++)
  if (!g3_gw(nc_writes[i].reg, nc_writes[i].val)) return 0;

 /* Self-test CP_MEM_WRITE to unused scratch slot G3_FENCE_PA + 8. */
 uint32_t sentinel = 0x5a730001u ^ (guilty_vm ? guilty_vm->seq : 0u);
 g_wr(G3_FENCE_PA, 8, 0);
 g3_emit(g3_pkt7(0x3d, 3)); g3_emit(G3_FENCE_VA + 8); g3_emit(0); g3_emit(sentinel);
 g3_emit(g3_pkt7(0x26, 0));
 g3_wptr &= 8191;
 __asm__ volatile("dsb sy" ::: "memory");
 if (!g3_gw(0x807, g3_wptr)) return 0;
 unsigned ok = 0;
 for (unsigned i = 0; i < 2000; i++) {
  if (g3_read(G3_FENCE_PA + 8) == sentinel && g3_gr(0x806) == g3_wptr && !(g3_gr(0x210) & ~1u)) { ok = 1; break; }
  if (fg_access_failed || g3_faulted()) return 0;
  delay(5);
 }
 if (!ok) return 0;

 /* Restore backboardd VM (if live) and fence IRQ if previously armed. */
 if (fg_bb_vm && fg_bb_vm->in_use && !fg_bb_vm->poisoned) {
  if (!fg_abi_switch_vm(fg_bb_vm)) return 0;
 }
 if (!fg_irq_rearm_soft()) return 0;
 return 1;
}
#define FG_SOFT_RECOVER_HW(vm) fg_soft_recover_hw(vm)
#else
static int fg_soft_recover_fail_model;
static unsigned fg_soft_recover_calls;
static int fg_soft_recover_hw(struct fuxi_gpu_vm *guilty_vm)
{
 (void)guilty_vm;
 fg_soft_recover_calls++;
 if (fg_soft_recover_fail_model) return 0;
 fault = 0;
 fg_access_failed = 0;
 g3_wptr = 0;
 (void)g3_gw(0x806, 0);
 (void)g3_gw(0x807, 0);
 (void)g3_gw(0x210, 0);
 if (fg_suspends != UINT64_MAX) ++fg_suspends;
 return 1;
}
#define FG_SOFT_RECOVER_HW(vm) fg_soft_recover_hw(vm)
#endif
#endif

static int fg_abi_guilty_fault(struct fuxi_gpu_vm *guilty_vm, const char *why)
{
 if (guilty_vm && fg_abi_queue && fg_client_busy && !fg_access_failed) {
  uint32_t f=g3_read(G3_FENCE_PA+guilty_vm->fence_off+4);
  if (!fg_access_failed && f>guilty_vm->completed && f<=guilty_vm->seq) {
   guilty_vm->completed=f;
   if (guilty_vm==fg_default_vm) fg_abi_completed=f;
  }
 }
 fg_abi_hang_dump_for(why, guilty_vm);
 if (guilty_vm && guilty_vm->client_class == FG_CLASS_APP && fg_abi_live && fuxi_gpu_ready()) {
  fg_soft_recover_count++;
  if (FG_SOFT_RECOVER_HW(guilty_vm)) {
   guilty_vm->poisoned = 1;
   guilty_vm->drained = 1;
   if (guilty_vm->faults != UINT64_MAX) guilty_vm->faults++;
   fg_abi_import_cache_vm(guilty_vm, 1);
   fg_client_busy = 0;
   fg_abi_present_wait = 0;
   fg_abi_hold_since = 0;
   fg_ibp_active = 0;
   fg_abi_stage = 0;
   fg_active_vm = (fg_bb_vm && fg_bb_vm->in_use && !fg_bb_vm->poisoned) ? fg_bb_vm : NULL;
   fg_root_client = (fg_active_vm && fg_active_vm == fg_bb_vm && fg_active_vm->inline_attached) ? 1u : 0u;
   FG_ABI_WAKE();
   printf("FUXI GPU RECOVER: isolated Class 1 VM id=%u pid=%d name=%s faults=%llu; backboardd preserved (fg_state=1)\n",
       guilty_vm->id, guilty_vm->pid, guilty_vm->name, (unsigned long long)guilty_vm->faults);
   return guilty_vm->timed_out ? -3 : -1;
  }
  fg_soft_recover_fail_count++;
  fg_abi_hang_dump_for(why, NULL);
 } else if (guilty_vm && guilty_vm->client_class == FG_CLASS_COMPOSITOR && fg_bb_recover_limit > 0 &&
            fg_bb_recover_count < fg_bb_recover_limit && fg_abi_live && fuxi_gpu_ready()) {
  guilty_vm->poisoned = 1;
  fg_bb_recover_count++;
  fg_soft_recover_count++;
  if (FG_SOFT_RECOVER_HW(guilty_vm)) {
   guilty_vm->drained = 1;
   if (guilty_vm->faults != UINT64_MAX) guilty_vm->faults++;
   fg_abi_import_cache_vm(guilty_vm, 1);
   if (fg_bb_vm == guilty_vm) fg_bb_vm = NULL;
   if (fg_default_vm == guilty_vm) fg_default_vm = NULL;
   fg_active_vm = NULL;
   fg_root_client = 0;
   fg_inl_busy = 0;
   fg_pk_n = 0;
   fg_client_busy = 0;
   fg_abi_present_wait = 0;
   fg_abi_hold_since = 0;
   fg_ibp_active = 0;
   fg_abi_stage = 0;
   fg_abi_timed_out = 0;
   FG_ABI_WAKE();
   printf("FUXI GPU RECOVER: isolated Class 0 VM id=%u pid=%d name=%s faults=%llu attempt=%u/%u; GPU kept alive (fg_state=1) for reconnect\n",
       guilty_vm->id, guilty_vm->pid, guilty_vm->name, (unsigned long long)guilty_vm->faults,
       fg_bb_recover_count, fg_bb_recover_limit);
   return guilty_vm->timed_out ? -3 : -1;
  }
  fg_soft_recover_fail_count++;
  fg_abi_hang_dump_for(why, NULL);
 }
 if (guilty_vm) guilty_vm->poisoned = 1;
 fg_abi_poisoned = 1; fg_pk_n = 0;
 fg_fail(why, 0x701);
 fg_abi_drained = fg_isolate();
 if (guilty_vm) guilty_vm->drained = fg_abi_drained;
 if (!fg_abi_drained && fg_scanout_owned) (void)fg_scanout_stop("GPU DMA drain failed");
 if (fg_abi_drained && guilty_vm) fg_abi_import_cache_vm(guilty_vm, 1);
 fg_client_busy = 0;
 FG_ABI_WAKE();
 return (fg_abi_timed_out || (guilty_vm && guilty_vm->timed_out)) ? (fg_abi_drained ? -3 : -4) : (fg_abi_drained ? -1 : -2);
}
static int fg_abi_fault(const char *why)
{
 struct fuxi_gpu_vm *guilty = fg_active_vm ? fg_active_vm : fg_default_vm;
 if (guilty) guilty->timed_out = fg_abi_timed_out;
 return fg_abi_guilty_fault(guilty, why);
}

#define FG_INL_L2 (FG_ABI_PT+0x304000)
#define FG_INL_L3 (FG_ABI_PT+0x305000)
#define FG_INL_TABLES 15u
_Static_assert(FG_INL_L2 >= FG_ABI_PT+(4u+768u)*4096 && FG_INL_L3+FG_INL_TABLES*4096 <= FG_ABI_PT+0x3f0000, "inline window tables overlap");
_Static_assert(FG_INL_FB_VA >= FG_ABI_VA_START+FG_ABI_VA_SIZE && FG_INL_SRC_VA >= FG_ABI_VA_START+FG_ABI_VA_SIZE && (FG_INL_SRC_VA>>30) == 2 && (FG_INL_FB_VA>>30) == 2, "inline window inside client VA");
static void fg_inl_mirror(unsigned idx, uint64_t v)
{
 if (fg_inl_ready != 1) return;
 ((volatile uint64_t *)G_PTR_RANGE(FG_INL_L2,4096))[((FG_INL_SRC_VA>>21)&511)+idx] = v;
}
static int fg_inl_usable(void)
{
 return fg_abi_inline && fg_inl_ready == 1 && fg_root_client && fg_abi_live && !fg_abi_poisoned &&
     (!fg_active_vm || fg_active_vm == fg_bb_vm);
}
static int fg_inl_setup_vm(struct fuxi_gpu_vm *vm)
{
 if (!vm || vm->client_class != FG_CLASS_COMPOSITOR) return 0;
 if (vm->inline_attached) return 1;
 if (fg_inl_ready == 2) return 0;
 if (vm->root_va[2]) { fg_inl_ready = 2; return 0; }
 if (!fg_abi_inline || !fg_abi_queue) { fg_inl_ready = 2; return 0; }
 if (!fg_inl_ready) {
  volatile uint64_t *l2 = (volatile uint64_t *)G_PTR_RANGE(FG_INL_L2,4096);
  if (!fg_l2()) { fg_inl_ready = 2; return 0; }
  g_zero(FG_INL_L2, (1+FG_INL_TABLES)*4096);
  static const uint64_t pa[3] = {G4_FB_PA, 0xb8a00000ULL, 0xb9400000ULL};
  unsigned want = 1u | (fg_scanout_double_requested ? 2u : 0) | (fg_scanout_triple_requested ? 4u : 0);
  _Static_assert(G4_FB_MAP <= 0xa00000u, "framebuffer exceeds its 10 MiB window");
  for (unsigned b = 0; b < 3; b++) {
   if (!(want & (1u<<b))) continue;
   for (unsigned pg = 0; pg < G4_FB_MAP/4096; pg++) {
    unsigned t = b*5+(pg>>9);
    volatile uint64_t *l3 = (volatile uint64_t *)G_PTR_RANGE(FG_INL_L3+(uint64_t)t*4096,4096);
    l2[((FG_INL_FB_VA>>21)&511)+t] = (FG_INL_L3+(uint64_t)t*4096) | 3;
    l3[pg&511] = (pa[b]+(uint64_t)pg*4096) | 3 | (3u<<8) | (1u<<10) | (1u<<6);
   }
  }
  volatile uint64_t *present = fg_l2();
  for (unsigned i = 0; i < FG_CACHE_SLOTS*FG_SOURCE_L3; i++)
   l2[((FG_INL_SRC_VA>>21)&511)+i] = present[((FG_SOURCE_VA>>21)&511)+i];
  __asm__ volatile("dsb sy" ::: "memory");
  fg_inl_fbmask = want; fg_inl_ready = 1;
  printf("FUXI GPU: inline copy window ready (sources %#x, carveout %#x, buffers %#x)\n", FG_INL_SRC_VA, FG_INL_FB_VA, want);
 }
 if (!vm->inline_attached && !vm->root_va[2]) {
  vm->root_va[2] = FG_INL_L2 | 3;
  FG_PT_PUBLISH(vm->root_va, 4096);
  vm->inline_attached = 1;
 }
 return vm->inline_attached;
}
static int fg_inl_to_present(void)
{
 if (!fg_root_client) return 1;
 if (fg_pk_n) { (void)fg_pk_flush(FG_PK_OTHER); return 0; }
 if (fg_client_busy || fg_busy) return 0;
 if (g3_faulted() || g3_gr(0x806) != g3_wptr || g3_gr(0x807) != g3_wptr || (g3_gr(0x210) & ~1u)) return 0;
 if (!fg_abi_switch_vm(NULL)) { (void)fg_abi_fault("inline restore present TTBR"); return 0; }
 fg_active_vm = NULL;
 return 1;
}

static void fg_abi_stall_log(const char *what)
{
 const uint32_t seq_v = fg_active_vm ? fg_active_vm->seq : fg_abi_seq;
 if (fg_abi_stall_lines<24) { fg_abi_stall_lines++; printf("FUXI GPU STALL: %s seq %u\n",what,seq_v); }
}
static void fg_abi_stall_done(void)
{
 fg_abi_stall_stats[fg_abi_stage==1 ? 5 : 6]++;
 fg_abi_stall_log(fg_abi_stage==1 ? "completed after WPTR kick" : "completed after bus drain");
 fg_abi_stage=0;
}
static int fg_abi_unstick_step(void)
{
 if (fg_active_vm && fg_active_vm->client_class == FG_CLASS_APP) return 0;
 const uint64_t freq=rsr("cntfrq_el0"), pending=rsr("cntpct_el0")-fg_client_submit_at;
 if (fg_abi_stage==0 && pending>=freq*30/1000) {
  fg_abi_stage=1; fg_abi_stall_stats[0]++; fg_abi_stall_stats[1]=fg_active_vm ? fg_active_vm->seq : fg_abi_seq;
  fg_abi_stall_log("pending 30 ms: snapshot, TLB drop, WPTR kick");
  if (fg_abi_stall_stats[7]<8) { fg_abi_stall_stats[7]++; fg_abi_snapshot("30ms"); }
  if (fg_abi_tlb_asid(fg_active_vm ? fg_active_vm->asid : g3_asid) || fg_access_failed) return 1;
  if (!fg_ring_doorbell() || fg_access_failed) return 1;
 }
 if (fg_abi_stage==1 && pending>=freq*60/1000) {
  uint64_t ack[2]={0,0};
  fg_abi_stage=2; fg_abi_stall_stats[2]++;
  fg_abi_stall_log("pending 60 ms: bus drain");
  if (!g3_gw(0x3c45,1)) return 1;
  for (unsigned i=0;i<1000 && !(ack[0]&1);i++) { ack[0]=g3_gr(0x3c46); if (!(ack[0]&1)) delay(1); }
  if (!g3_gw(0x3c45,3)) return 1;
  for (unsigned i=0;i<1000 && !(ack[1]&2);i++) { ack[1]=g3_gr(0x3c46); if (!(ack[1]&2)) delay(1); }
  if (!g3_gw(0x3c45,0) || fg_access_failed) return 1;
  fg_abi_stall_stats[3]=(uint32_t)ack[0]&1; fg_abi_stall_stats[4]=((uint32_t)ack[1]>>1)&1;
 }
 return 0;
}

static struct fuxi_gpu_vm *fg_vm_by_hw_asid(void)
{
 unsigned cb = g_cb(g3_bank);
 uint32_t asid = (g3_sr(cb + 0x24) >> 16) & 0xffu;
 for (unsigned i = 0; i < FG_ABI_MAX_VMS; i++) {
  if (fg_vm_pool[i].in_use && fg_vm_pool[i].asid == asid) return &fg_vm_pool[i];
 }
 return NULL;
}

static int fg_abi_observe_vm(struct fuxi_gpu_vm *vm)
{
 if (fg_abi_poisoned) return fg_abi_timed_out ? (fg_abi_drained ? -3 : -4) : (fg_abi_drained ? -1 : -2);
 if (vm && vm->poisoned) return vm->timed_out ? (vm->drained ? -3 : -4) : (vm->drained ? -1 : -2);
 if (!fuxi_gpu_ready()) return -1;
 if (fg_access_failed || g3_faulted()) {
  struct fuxi_gpu_vm *guilty = fg_active_vm ? fg_active_vm : fg_vm_by_hw_asid();
  int rc = fg_abi_guilty_fault(guilty, "client GPU fault");
  if (vm && vm != guilty && !vm->poisoned && fuxi_gpu_ready() && !fg_abi_poisoned) return 0;
  return rc;
 }
 if (fg_pk_n) { int r=fg_pk_check(); if (r) return r; }
 if (!fg_client_busy) return 0;
 struct fuxi_gpu_vm *run_vm = fg_active_vm ? fg_active_vm : fg_default_vm;
 if (!run_vm) return 0;
 const uint32_t foff = run_vm->fence_off;
 if (g3_read(G3_FENCE_PA+foff+4)==run_vm->seq &&
     ((fg_root_client && run_vm == fg_bb_vm) || (g3_gr(0x806)==g3_wptr && g3_gr(0x807)==g3_wptr && !(g3_gr(0x210)&~1u)))) {
  if (run_vm->client_class == FG_CLASS_COMPOSITOR) {
   if (!fg_root_client && !fg_abi_switch_vm(NULL)) return fg_abi_guilty_fault(run_vm, "client restore TTBR");
  } else {
   if (fg_bb_vm && fg_bb_vm->in_use && !fg_bb_vm->poisoned) {
    if (!fg_abi_switch_vm(fg_bb_vm)) return fg_abi_guilty_fault(run_vm, "client restore backboardd TTBR");
   } else {
    if (!fg_abi_switch_vm(NULL)) return fg_abi_guilty_fault(run_vm, "client restore present TTBR");
   }
  }
  fg_abi_import_cache_vm(run_vm, 1);
  if (fg_abi_stage) fg_abi_stall_done();
  if (fg_js_on) fg_js_retire(run_vm->completed, run_vm->seq);
  if (fg_ibp_on) fg_ibp_retire(run_vm->completed, run_vm->seq);
  run_vm->completed = run_vm->seq;
  if (run_vm == fg_default_vm) fg_abi_completed = run_vm->completed;
  fg_client_busy = 0;
  if (!fg_abi_hold) fg_abi_present_wait = 0;
  if (run_vm->client_class == FG_CLASS_APP) {
   fg_active_vm = (fg_bb_vm && fg_bb_vm->in_use && !fg_bb_vm->poisoned) ? fg_bb_vm : NULL;
  } else if (!fg_root_client) {
   fg_active_vm = NULL;
  }
  FG_ABI_WAKE();
  return 0;
 }
 {
  uint32_t f;
  if (fg_abi_queue && (f=g3_read(G3_FENCE_PA+foff+4))>run_vm->completed && f<run_vm->seq) {
   fg_abi_import_cache_vm(run_vm, 1); run_vm->host_handoff=1;
   if (run_vm==fg_default_vm) fg_abi_host_handoff=1;
   if (fg_abi_stage) fg_abi_stall_done();
   if (fg_js_on) fg_js_retire(run_vm->completed, f);
   if (fg_ibp_on) fg_ibp_retire(run_vm->completed, f);
   run_vm->completed=f;
   if (run_vm==fg_default_vm) fg_abi_completed=f;
   {
    uint64_t freq=rsr("cntfrq_el0");
    fg_client_submit_at = rsr("cntpct_el0");
    fg_client_deadline = fg_client_submit_at + (run_vm->client_class==FG_CLASS_APP ? freq*fg_class1_quantum_ms/1000 : freq/10);
   }
   FG_ABI_WAKE();
   return 1;
  }
 }
 if (rsr("cntpct_el0") >= fg_client_deadline) {
  run_vm->timed_out = 1;
  if (run_vm == fg_default_vm) fg_abi_timed_out = 1;
  int rc = fg_abi_guilty_fault(run_vm, run_vm->client_class == FG_CLASS_APP ? "client fence deadline (40 ms)" : "client fence deadline (100 ms)");
  if (vm && vm != run_vm && !vm->poisoned && fuxi_gpu_ready() && !fg_abi_poisoned) return 0;
  return rc;
 }
 if (fg_abi_unstick && fg_abi_unstick_step()) {
  return fg_abi_guilty_fault(run_vm, "client stall recovery access");
 }
 return 1;
}
static int fg_abi_observe(void)
{
 return fg_abi_observe_vm(fg_active_vm ? fg_active_vm : fg_default_vm);
}

static void fg_abi_watch(thread_call_param_t arg, thread_call_param_t unused)
{
 (void)arg; (void)unused;
 FG_GUARD;
 if (fg_abi_observe()==1 || fg_pk_n) {
  uint64_t deadline;
  clock_interval_to_deadline(2,NSEC_PER_MSEC,&deadline);
  (void)thread_call_enter_delayed(&fg_client_watchdog,deadline);
 }
}

static int fg_abi_vm_create_locked(unsigned client_class, int pid, const char *name, struct fuxi_gpu_vm **vm_out)
{
 if (!fg_abi_live || !fuxi_gpu_ready() || fg_abi_poisoned) return -1;
 if (client_class != FG_CLASS_COMPOSITOR && client_class != FG_CLASS_APP) return -1;
 if (client_class == FG_CLASS_APP) {
  unsigned app_cnt = 0, pid_cnt = 0;
  for (unsigned i = 0; i < FG_ABI_MAX_VMS; i++) {
   if (fg_vm_pool[i].in_use && fg_vm_pool[i].client_class == FG_CLASS_APP) {
    app_cnt++;
    if (pid > 0 && fg_vm_pool[i].pid == pid) pid_cnt++;
   }
  }
  if (app_cnt >= FG_ABI_MAX_CLASS1_CLIENTS) return -1;
  if (pid > 0 && pid_cnt >= FG_ABI_MAX_CLASS1_VMS_PER_PID) return -1;
 }
 int slot = -1;
 for (unsigned i = 0; i < FG_ABI_MAX_VMS; i++) {
  if (!fg_vm_pool[i].in_use) { slot = (int)i; break; }
 }
 if (slot < 0) return -1;
 uint32_t asid = fg_asid_alloc();
 if (!asid) return -1;
 struct fuxi_gpu_vm *vm = &fg_vm_pool[slot];
 memset(vm, 0, sizeof(*vm));
 vm->id = (uint32_t)slot;
 vm->asid = asid;
 vm->fence_off = (slot == 0) ? FG_ABI_FENCE_OFF : (FG_ABI_VM_FENCE_BASE + ((uint32_t)slot - 1u) * FG_ABI_VM_FENCE_STRIDE);
 vm->client_class = client_class;
 vm->pid = pid;
 if (name) {
  size_t len = strlen(name);
  if (len >= sizeof(vm->name)) len = sizeof(vm->name) - 1;
  memcpy(vm->name, name, len);
  vm->name[len] = 0;
 }
 vm->max_bos = (client_class == FG_CLASS_COMPOSITOR) ? FG_ABI_MAX_BOS : FG_ABI_CLASS1_MAX_BOS;
 vm->max_imports = (client_class == FG_CLASS_COMPOSITOR) ? FG_ABI_MAX_IMPORTS : FG_ABI_CLASS1_MAX_IMPORTS;
 if (!fg_vm_alloc_pt_page(vm, &vm->root_pa, &vm->root_va) ||
     !fg_vm_alloc_pt_page(vm, &vm->l2_pa[0], &vm->l2_va[0]) ||
     !fg_vm_alloc_pt_page(vm, &vm->l2_pa[1], &vm->l2_va[1])) {
  fg_vm_free_all_pt(vm);
  fg_asid_free_bit(asid);
  memset(vm, 0, sizeof(*vm));
  return -1;
 }
 vm->root_va[0] = vm->l2_pa[0] | 3u;
 vm->root_va[1] = vm->l2_pa[1] | 3u;
 vm->root_va[2] = 0;
 vm->l2_va[0][G3_SQE_VA >> 21] = FG_ABI_RUNTIME_L3 | 3u;
 FG_PT_PUBLISH(vm->l2_va[0], 4096);
 FG_PT_PUBLISH(vm->l2_va[1], 4096);
 FG_PT_PUBLISH(vm->root_va, 4096);
 g_wr(G3_FENCE_PA, vm->fence_off + 4, 0);
 g_wr(G3_FENCE_PA, vm->fence_off + 12, 0);
 vm->in_use = 1;
 vm->drained = 1;
 vm->need_tlbi = 1;
 if (client_class == FG_CLASS_COMPOSITOR && (!fg_bb_vm || !fg_bb_vm->in_use)) {
  fg_bb_vm = vm;
 }
 *vm_out = vm;
 return 0;
}

static void fg_abi_vm_destroy_locked(struct fuxi_gpu_vm *vm)
{
 if (!vm || !vm->in_use) return;
 if (fg_active_vm == vm) {
  if (fuxi_gpu_ready() && !fg_access_failed && !fg_client_busy && !fg_busy) {
   (void)fg_abi_switch_vm(fg_bb_vm && fg_bb_vm != vm && fg_bb_vm->in_use && !fg_bb_vm->poisoned ? fg_bb_vm : NULL);
  }
  fg_active_vm = (fg_bb_vm && fg_bb_vm != vm && fg_bb_vm->in_use && !fg_bb_vm->poisoned && fg_root_client) ? fg_bb_vm : NULL;
 }
 if (fuxi_gpu_ready() && !fg_access_failed) (void)fg_abi_tlb_asid(vm->asid);
 for (unsigned i = 0; i < vm->max_imports; i++) {
  if (vm->imports[i].dty_first) fg_abi_dty_detach_vm(vm, i);
 }
 if (fg_default_vm == vm) fg_default_vm = NULL;
 if (fg_bb_vm == vm) fg_bb_vm = NULL;
 uint32_t asid = vm->asid;
 fg_vm_free_all_pt(vm);
 fg_asid_free_bit(asid);
 memset(vm, 0, sizeof(*vm));
}

int fuxi_gpu_abi_vm_create(unsigned client_class, int pid, const char *name, struct fuxi_gpu_vm **vm_out)
{
 FG_GUARD;
 if (!vm_out) return -1;
 *vm_out = NULL;
 return fg_abi_vm_create_locked(client_class, pid, name, vm_out);
}
void fuxi_gpu_abi_vm_destroy(struct fuxi_gpu_vm *vm)
{
 FG_GUARD;
 fg_abi_vm_destroy_locked(vm);
}
int fuxi_gpu_abi_vm_poisoned(struct fuxi_gpu_vm *vm)
{
 FG_GUARD;
 return (!vm || !vm->in_use || fg_abi_poisoned || vm->poisoned) ? 1 : 0;
}
int fuxi_gpu_abi_vm_reserve_quota(struct fuxi_gpu_vm *vm, uint32_t bos, uint64_t wired, uint32_t imports)
{
 FG_GUARD;
 if (!vm || !vm->in_use || fg_abi_poisoned || vm->poisoned) return -1;
 if (vm->client_class != FG_CLASS_APP || vm->pid <= 0) return 0;
 struct fg_pid_quota *q = fg_pid_quota_find(vm->pid, 1);
 if (!q) return -1;
 if (bos > FG_ABI_CLASS1_MAX_BOS - q->bos ||
     imports > FG_ABI_CLASS1_MAX_IMPORTS - q->imports ||
     wired > FG_ABI_CLASS1_MAX_WIRED_BYTES - q->wired) {
  return -1;
 }
 q->bos += bos;
 q->wired += wired;
 q->imports += imports;
 return 0;
}
void fuxi_gpu_abi_vm_release_quota(struct fuxi_gpu_vm *vm, uint32_t bos, uint64_t wired, uint32_t imports)
{
 FG_GUARD;
 if (!vm || vm->client_class != FG_CLASS_APP || vm->pid <= 0) return;
 struct fg_pid_quota *q = fg_pid_quota_find(vm->pid, 0);
 if (!q) return;
 q->bos = (q->bos >= bos) ? (q->bos - bos) : 0;
 q->wired = (q->wired >= wired) ? (q->wired - wired) : 0;
 q->imports = (q->imports >= imports) ? (q->imports - imports) : 0;
 if (!q->bos && !q->wired && !q->imports) memset(q, 0, sizeof(*q));
}

static int fg_abi_init(void)
{
 for (unsigned i = 0; i < FG_ABI_MAX_VMS; i++) {
  if (fg_vm_pool[i].pt_slab_count) fg_vm_free_all_pt(&fg_vm_pool[i]);
 }
 memset(fg_vm_pool, 0, sizeof(fg_vm_pool));
 memset(fg_pid_quotas, 0, sizeof(fg_pid_quotas));
 fg_default_vm = NULL;
 fg_bb_vm = NULL;
 fg_active_vm = NULL;
 fg_hw_ttbr = 0;
 __atomic_store_n(&fg_bb_waiting, 0, __ATOMIC_RELAXED);
 fg_soft_recover_count = fg_soft_recover_fail_count = 0;
 fg_bb_recover_limit = fg_bb_recover_count = 0;

 unsigned bits=0;
 (void)PE_parse_boot_argn("fuxigpuncimport",&bits,sizeof(bits));
 fg_abi_ncimport=bits&1; fg_wptr_fence=(bits>>1)&1; fg_abi_unstick=(bits>>2)&1; fg_abi_cohtest=(bits>>6)&1; fg_abi_tlbpages=(bits>>7)&1; fg_abi_mapwait=(bits>>8)&1; fg_abi_queue=(bits>>9)&1; fg_abi_roimport=(bits>>10)&1; fg_abi_pace=(bits>>11)&1; fg_abi_hold=(bits>>12)&1; fg_abi_start=(bits>>13)&1; fg_abi_probe=(bits>>14)&1; fg_abi_spin=(bits>>15)&1; fg_abi_mid=(bits>>16)&1; fg_abi_ar=(bits>>17)&1; fg_abi_hard=(bits>>18)&1; fg_abi_dirty=(bits>>19)&1; fg_abi_cbo=(bits>>29)&1; fg_abi_cbo_on=0; fg_abi_cbo_said=NULL; fg_abi_inline=((bits>>21)&1) && ((bits>>9)&1); fg_abi_cf=fg_abi_inline && ((bits>>4)&1); fg_abi_pk=fg_abi_cf && ((bits>>27)&1); fg_irq_want=(bits>>26)&1; fg_cf_reset(); fg_pk_reset(); fg_inl_ready=fg_root_client=fg_inl_busy=fg_inl_fbmask=0; fg_abi_hold_since=0; fg_abi_present_wait=0; memset(fg_abi_q_stats,0,sizeof(fg_abi_q_stats));
 if (fg_abi_pk) printf("FUXI GPU: copy_first_park=1 idle_us=%u cap=2 periods (fuxigpuncimport 134217728 with 16, inline mode)\n", FG_PK_IDLE_US);
 { unsigned early=0; fg_js_on=PE_parse_boot_argn("fuxiearly",&early,sizeof(early)) && (early&8u); fg_js_n=fg_js_bad=0; memset(fg_js_sum,0,sizeof(fg_js_sum)); memset(fg_js_max,0,sizeof(fg_js_max));
   if (fg_js_on) printf("FUXI GPU ABI: job_stamps on (fuxiearly bit 8): 12 ring words per job\n");
   fg_ibp_on=(early&16u)!=0; fg_ibp_reset();
   if (fg_ibp_on) printf("FUXI GPU ABI: ib_profile on (fuxiearly bit 16): one job in %u, %u ring words per IB\n",FG_IBP_EVERY,FG_IBP_STAMP_WORDS); }
#ifdef FUXI_GPU_XNU
 {
  unsigned qms = 0;
  if (PE_parse_boot_argn("fuxigpuquantum", &qms, sizeof(qms)) && qms >= 10 && qms <= 100) fg_class1_quantum_ms = qms;
  DTEntry chosen;
  void const *prop = NULL;
  unsigned int bytes = 0;
  uint32_t rlim = 0;
  if (SecureDTLookupEntry(NULL, "/chosen", &chosen) == kSuccess &&
      SecureDTGetProperty(chosen, "fuxi,gpu-bb-recover", &prop, &bytes) == kSuccess &&
      prop && bytes == sizeof(uint32_t)) {
   memcpy(&rlim, prop, sizeof(rlim));
   if (rlim > 8u) rlim = 8u;
   fg_bb_recover_limit = rlim;
   if (fg_bb_recover_limit)
    printf("FUXI GPU ABI: Class 0 bounded soft recovery enabled (limit=%u)\n", fg_bb_recover_limit);
  }
 }
#endif
 fg_abi_coh=fg_abi_coh_tries=0;

 static const struct { const char *name; unsigned reg; uint32_t want, mask; } checks[] = {
  {"UCHE_WRITE_THRU_BASE_LO",0xe07,0xfffff000u,0xfffff000u},
  {"UCHE_WRITE_THRU_BASE_HI",0xe08,0x1ffff,UINT32_MAX},
  {"UCHE_TRAP_BASE_LO",0xe09,0xfffff000u,0xfffff000u},
  {"UCHE_TRAP_BASE_HI",0xe0a,0x1ffff,UINT32_MAX},
  {"UCHE_GMEM_RANGE_MIN_LO",0xe0b,0x1000000,0xfffff000u},
  {"UCHE_GMEM_RANGE_MIN_HI",0xe0c,0,UINT32_MAX},
  {"UCHE_GMEM_RANGE_MAX_LO",0xe0d,0x12fffff,0xfffff000u},
  {"UCHE_GMEM_RANGE_MAX_HI",0xe0e,0,UINT32_MAX},
  {"UCHE_MODE_CNTL_HBB",0xe01,3,UINT32_MAX},
  {"RBBM_NC_MODE_CNTL",0x534,1,UINT32_MAX},
 };
 uint32_t values[sizeof(checks)/sizeof(checks[0])];
 for (unsigned i=0;i<sizeof(checks)/sizeof(checks[0]);i++) values[i]=g3_gr(checks[i].reg);
 uint32_t id2=g3_sr(SMMU_ID2);
 static unsigned dumped;
 if (!dumped) {
  dumped=1;
  for (unsigned i=0;i<sizeof(checks)/sizeof(checks[0]);i++)
   printf("FUXI GPU ABI: init regs %s [0x%x]=0x%x\n",checks[i].name,checks[i].reg,values[i]);
  printf("FUXI GPU ABI: init regs SMMU_ID2 [0x%x]=0x%x\n",SMMU_ID2,id2);
 }
 if (fg_access_failed) {
  printf("FUXI GPU ABI: init check register_access got 0x%x want 0x0\n",fg_access_failed);
  return 0;
 }
 for (unsigned i=0;i<sizeof(checks)/sizeof(checks[0]);i++) {
  uint32_t got=checks[i].reg==0xe01 ? (values[i]>>21)&3 : values[i];
  if ((got&checks[i].mask)!=(checks[i].want&checks[i].mask)) {
   printf("FUXI GPU ABI: init check %s got 0x%x want 0x%x\n",checks[i].name,got,checks[i].want);
   return 0;
  }
 }
 if (((id2>>4)&15)<1) {
  printf("FUXI GPU ABI: init check SMMU_ID2_OAS_min got 0x%x want 0x1\n",(id2>>4)&15);
  return 0;
 }
 static const struct { const char *name; unsigned reg; uint32_t value; } writes[] = {
  {"G4_RB_NC",G4_RB_NC,0x816},
  {"G4_TPL1_NC",G4_TPL1_NC,6},
  {"G4_SP_NC",G4_SP_NC,0x26},
  {"G3_APERTURE_HOST_BR",G3_APERTURE_HOST,1u<<12},
  {"G4_GRAS_NC_BR",G4_GRAS_NC,0x60},
  {"G3_APERTURE_HOST_BV",G3_APERTURE_HOST,2u<<12},
  {"G4_GRAS_NC_BV",G4_GRAS_NC,0x60},
  {"G3_APERTURE_HOST_DEFAULT",G3_APERTURE_HOST,0},
 };
 for (unsigned i=0;i<sizeof(writes)/sizeof(writes[0]);i++) {
  if (!g3_gw(writes[i].reg,writes[i].value)) {
   printf("FUXI GPU ABI: init write %s [0x%x]=0x%x failed\n",writes[i].name,writes[i].reg,writes[i].value);
   return 0;
  }
 }

 /* Initialize 8-bit ASID bitmap, excluding 0, g3_asid, g_asid, and any live SMMU CB ASIDs. */
 memset(fg_asid_bitmap, 0, sizeof(fg_asid_bitmap));
 fg_asid_set_used(0);
 fg_asid_set_used(g3_asid);
#ifdef FUXI_GPU_XNU
 fg_asid_set_used(g_asid);
 for (unsigned b = 0; b < g_ncb; b++) {
  if (gpu_extra[1][52] & (1u << b)) continue;
  unsigned cb = g_cb(b);
  if (g3_sr(cb) & 1) {
   fg_asid_set_used(g3_sr(cb + 0x24) >> 16);
   fg_asid_set_used(g3_sr(cb + 0x2c) >> 16);
  }
 }
#endif

 g_zero(FG_ABI_PT,FG_ABI_PT_BYTES);
 volatile uint64_t *runtime = (volatile uint64_t *)G_PTR_RANGE(FG_ABI_RUNTIME_L3,4096);
 for (unsigned i=0;i<0x2a000/4096;i++) {
  uint64_t pa = G3_SQE_PA+(uint64_t)i*4096;
  unsigned ap = i<0x20 ? 3u<<6 : i==0x28 ? 0 : 1u<<6;
  runtime[i]=pa|3|(3u<<8)|(1u<<10)|ap;
 }
 g_wr(G3_FENCE_PA,FG_ABI_FENCE_OFF+4,0);
 g_wr(G3_FENCE_PA,FG_ABI_FENCE_OFF+12,0);
 __asm__ volatile("dsb sy" ::: "memory");
 thread_call_setup_with_options(&fg_client_watchdog,fg_abi_watch,NULL,
     THREAD_CALL_PRIORITY_KERNEL,THREAD_CALL_OPTIONS_ONCE);
 fg_abi_live=1;
 return 1;
}
int fuxi_gpu_abi_available(void)
{
 FG_GUARD;
 return fg_abi_live && fuxi_gpu_ready() && !fg_abi_poisoned;
}
int fuxi_gpu_abi_timestamp(uint64_t *ticks)
{
 FG_GUARD;
 if (!ticks) return -2;
 *ticks=0;
 if (!fg_abi_live || !fuxi_gpu_ready() || fg_abi_poisoned) return -1;
 for (unsigned attempt=0;attempt<8;attempt++) {
  uint32_t high=g3_gr(0x981), low=g3_gr(0x980), again=g3_gr(0x981);
  if (fg_access_failed) { (void)fg_abi_fault("client timestamp MMIO"); return -2; }
  if (high==again) { *ticks=((uint64_t)high<<32)|low; return 0; }
 }
 return -3;
}
static int fg_abi_vm_status_locked(struct fuxi_gpu_vm *vm, uint64_t *faults, uint64_t *suspends)
{
 if (!faults || !suspends) return -1;
 *faults=*suspends=0;
 if (!fg_abi_live) return -1;
 if (fuxi_gpu_ready()) (void)fg_abi_observe_vm(vm);
 uint64_t vm_faults = vm ? vm->faults : 0;
 *faults = (vm_faults > UINT64_MAX - fg_faults) ? UINT64_MAX : (fg_faults + vm_faults);
 *suspends = fg_suspends;
 return 0;
}
int fuxi_gpu_abi_vm_status(struct fuxi_gpu_vm *vm, uint64_t *faults, uint64_t *suspends)
{
 FG_GUARD;
 return fg_abi_vm_status_locked(vm, faults, suspends);
}
int fuxi_gpu_abi_status(uint64_t *faults, uint64_t *suspends)
{
 FG_GUARD;
 return fg_abi_vm_status_locked(fg_default_vm, faults, suspends);
}

static ppnum_t fg_abi_ppns[(FG_ABI_MAX_BYTES>>PAGE_SHIFT)+2];
static int fg_abi_wait_idle(void)
{
 if (!fg_client_busy) return 0;
 int s=fg_abi_observe();
 return s<0 ? -1 : s>0 ? 1 : 0;
}
static int fg_abi_map_locked(struct fuxi_gpu_vm *vm, uint64_t iova, uintptr_t source, uint64_t bytes,
    unsigned permissions, unsigned wb, unsigned sh)
{
 if (!vm || !vm->in_use || !fg_abi_live || !fuxi_gpu_ready() || fg_abi_poisoned || vm->poisoned) return -1;
 if (fg_abi_mapwait) { int idle=fg_abi_wait_idle(); if (idle) return idle; }
 else if (fg_client_busy && fg_abi_observe_vm(vm)<0) return -1;
 if (!fg_abi_map_valid(iova,bytes,permissions,wb,sh,0) || !source || (source&4095) || bytes>FG_ABI_MAX_BYTES) return -1;
 uintptr_t base=source>>PAGE_SHIFT;
 for (uint64_t off=0;off<bytes;off+=4096) {
  if (!off || !((source+off)&PAGE_MASK)) {
   ppnum_t ppn=pmap_find_phys(kernel_pmap,source+off);
   if (!ppn || ((uint64_t)ppn<<PAGE_SHIFT)>=(1ULL<<FG_ABI_PA_BITS)) return -1;
   fg_abi_ppns[((source+off)>>PAGE_SHIFT)-base]=ppn;
  }
 }
 if (fg_abi_used_any_vm(vm,fg_abi_pg(iova),(unsigned)(bytes>>12))) return -1;

 /* Ensure all needed L3 tables exist before publishing leaves (fail-closed if PT alloc fails). */
 unsigned l2_dirty_mask = 0;
 uint64_t first_chunk = (iova - FG_ABI_VA_START) >> 21;
 uint64_t last_chunk = (iova + bytes - 4096u - FG_ABI_VA_START) >> 21;
 for (uint64_t c = first_chunk; c <= last_chunk; c++) {
  if (!vm->l3_va[c]) {
   uint64_t l3_pa = 0;
   volatile uint64_t *l3_va = NULL;
   if (!fg_vm_alloc_pt_page(vm, &l3_pa, &l3_va)) {
    if (l2_dirty_mask & 1u) FG_PT_PUBLISH(vm->l2_va[0], 4096);
    if (l2_dirty_mask & 2u) FG_PT_PUBLISH(vm->l2_va[1], 4096);
    return -1;
   }
   vm->l3_pa[c] = l3_pa;
   vm->l3_va[c] = l3_va;
   uint64_t va_base = FG_ABI_VA_START + c * 0x200000ULL;
   unsigned l2_idx = (unsigned)(va_base >> 30);
   vm->l2_va[l2_idx][(va_base >> 21) & 511u] = l3_pa | 3u;
   l2_dirty_mask |= 1u << l2_idx;
  }
 }
 if (l2_dirty_mask & 1u) FG_PT_PUBLISH(vm->l2_va[0], 4096);
 if (l2_dirty_mask & 2u) FG_PT_PUBLISH(vm->l2_va[1], 4096);

 volatile uint64_t *leaf=NULL;
 unsigned prev_chunk = FG_VM_CHUNKS;
 for (uint64_t off=0;off<bytes;off+=4096) {
  unsigned chunk = (unsigned)((iova + off - FG_ABI_VA_START) >> 21);
  if (chunk != prev_chunk) {
   if (prev_chunk < FG_VM_CHUNKS && vm->l3_va[prev_chunk]) FG_PT_PUBLISH(vm->l3_va[prev_chunk], 4096);
   leaf = fg_abi_leaf_vm(vm, iova + off);
   prev_chunk = chunk;
  } else {
   leaf++;
  }
  uint64_t pa=((uint64_t)fg_abi_ppns[((source+off)>>PAGE_SHIFT)-base]<<PAGE_SHIFT)|((source+off)&PAGE_MASK);
  *leaf=pa|3|(permissions==3 ? 1u<<6 : 3u<<6)|(wb ? 1u<<2 : 0)|sh<<8|1u<<10;
 }
 if (prev_chunk < FG_VM_CHUNKS && vm->l3_va[prev_chunk]) FG_PT_PUBLISH(vm->l3_va[prev_chunk], 4096);
 fg_abi_used_fill_vm(vm,fg_abi_pg(iova),(unsigned)(bytes>>12),1);
 __asm__ volatile("dsb sy" ::: "memory");
 fg_abi_map_note_vm(vm,iova,bytes,source);
 vm->need_tlbi = 1;
 return 0;
}

static struct fuxi_gpu_vm *fg_ensure_default_vm(void)
{
 if (!fg_default_vm) {
  (void)fg_abi_vm_create_locked(FG_CLASS_COMPOSITOR, 0, "default", &fg_default_vm);
 }
 return fg_default_vm;
}
int fuxi_gpu_abi_vm_map(struct fuxi_gpu_vm *vm, uint64_t iova, uintptr_t source, uint64_t bytes,
    unsigned permissions, unsigned writeback, unsigned shareability)
{
 FG_GUARD;
 uint64_t t=mach_absolute_time();
 int s=fg_abi_map_locked(vm,iova,source,bytes,permissions,writeback,shareability);
 fg_abi_map_work=mach_absolute_time()-t;
 return s;
}
int fuxi_gpu_abi_map(uint64_t iova, uintptr_t source, uint64_t bytes,
    unsigned permissions, unsigned wb, unsigned sh)
{
 FG_GUARD;
 struct fuxi_gpu_vm *vm = fg_ensure_default_vm();
 uint64_t t=mach_absolute_time();
 int s=fg_abi_map_locked(vm,iova,source,bytes,permissions,wb,sh);
 fg_abi_map_work=mach_absolute_time()-t;
 return s;
}
uint64_t fuxi_gpu_abi_map_work(void) { return fg_abi_map_work; }

static void fg_abi_coh_probe(struct fuxi_gpu_vm *vm);
static int fg_abi_map_import_locked(struct fuxi_gpu_vm *vm, uint64_t iova, uintptr_t source, uint64_t bytes, unsigned permissions)
{
 if (!vm || !vm->in_use || fg_abi_poisoned || vm->poisoned) return -1;
 unsigned gpu_only=permissions&FG_ABI_IMPORT_GPU_ONLY;
 permissions&=~FG_ABI_IMPORT_GPU_ONLY;
 if (!source || !bytes || ((source|bytes)&(NativeAdrenoAbiHostPageBytes-1)) ||
     bytes>FG_ABI_MAX_BYTES || source+bytes<source) return -1;
 if (fg_abi_cohtest && !fg_abi_coh) fg_abi_coh_probe(vm);
 unsigned slot;
 for (slot=0;slot<vm->max_imports && vm->imports[slot].iova;slot++) {}
 if (slot==vm->max_imports) return -1;
 int s=fg_abi_map_locked(vm,iova,source,bytes,permissions,fg_abi_coh_wb,fg_abi_coh_wb ? 3 : 2);
 if (!s) {
  vm->imports[slot].iova=0;
  fg_abi_dty_attach_vm(vm,slot,source,bytes);
  if ((gpu_only || vm->imports[slot].dty_first || (fg_abi_ncimport && (permissions&2))) && fg_abi_coh!=1) {
   __asm__ volatile("dsb sy" ::: "memory");
   fg_abi_cache_region(source,bytes,0);
   __asm__ volatile("dsb sy" ::: "memory");
  }
  vm->imports[slot].permissions=permissions; vm->imports[slot].gpu_only=gpu_only;
  vm->imports[slot].source=source; vm->imports[slot].bytes=bytes; vm->imports[slot].iova=iova;
 }
 return s;
}
int fuxi_gpu_abi_vm_map_import(struct fuxi_gpu_vm *vm, uint64_t iova, uintptr_t source, uint64_t bytes, unsigned permissions)
{
 FG_GUARD;
 return fg_abi_map_import_locked(vm,iova,source,bytes,permissions);
}
int fuxi_gpu_abi_map_import(uint64_t iova, uintptr_t source, uint64_t bytes, unsigned permissions)
{
 FG_GUARD;
 struct fuxi_gpu_vm *vm = fg_ensure_default_vm();
 return fg_abi_map_import_locked(vm,iova,source,bytes,permissions);
}
static void fg_abi_import_remove_vm(struct fuxi_gpu_vm *vm, uint64_t iova, uint64_t bytes)
{
 for (unsigned i=0;i<vm->max_imports;i++)
  if (vm->imports[i].iova==iova && vm->imports[i].bytes==bytes) {
   if (vm->imports[i].gpu_only && fg_abi_coh!=1) {
    __asm__ volatile("dsb sy" ::: "memory");
    fg_abi_cache_region(vm->imports[i].source,bytes,1);
    __asm__ volatile("dsb sy" ::: "memory");
   }
   fg_abi_dty_detach_vm(vm,i);
   vm->imports[i].iova=0; vm->imports[i].gpu_only=0;
  }
}
static int fg_abi_unmap_locked(struct fuxi_gpu_vm *vm, uint64_t iova, uint64_t bytes)
{
 if (!vm || !vm->in_use) return -1;
 int idle = 0;
 if (fg_abi_live && !fg_abi_poisoned && !vm->poisoned && fg_abi_range(iova,bytes)) idle = fg_abi_wait_idle();
 if (fg_abi_poisoned || vm->poisoned) {
  if ((fg_abi_poisoned && !fg_abi_drained) || !vm->drained) return -2;
  fg_abi_import_remove_vm(vm,iova,bytes); fg_abi_map_forget_vm(vm,iova,bytes); return 0;
 }
 if (idle < 0) return idle;
 if (!fg_abi_live || !fg_abi_range(iova,bytes)) return -1;
 if (idle > 0 || fg_client_busy || fg_busy) return 1;
 volatile uint64_t *leaf=NULL;
 unsigned prev_chunk = FG_VM_CHUNKS;
 for (uint64_t off=0;off<bytes;off+=4096) {
  unsigned chunk = (unsigned)((iova + off - FG_ABI_VA_START) >> 21);
  if (chunk != prev_chunk) {
   if (prev_chunk < FG_VM_CHUNKS && vm->l3_va[prev_chunk]) FG_PT_PUBLISH(vm->l3_va[prev_chunk], 4096);
   leaf = fg_abi_leaf_vm(vm, iova + off);
   prev_chunk = chunk;
  } else if (leaf) {
   leaf++;
  }
  if (leaf) *leaf=0;
 }
 if (prev_chunk < FG_VM_CHUNKS && vm->l3_va[prev_chunk]) FG_PT_PUBLISH(vm->l3_va[prev_chunk], 4096);
 fg_abi_used_fill_vm(vm,fg_abi_pg(iova),(unsigned)(bytes>>12),0);
 __asm__ volatile("dsb sy" ::: "memory");
 int terr=(vm->client_class == FG_CLASS_COMPOSITOR && fg_abi_tlbpages && bytes/4096<=FG_ABI_TLB_PAGE_LIMIT) ?
     fg_abi_tlb_pages_asid(iova,bytes,vm->asid) : fg_abi_tlb_asid(vm->asid);
 if (terr || fg_access_failed) return fg_abi_guilty_fault(vm, "client unmap TLB sync");
 vm->need_tlbi = 0;
 fg_abi_import_remove_vm(vm,iova,bytes);
 fg_abi_map_forget_vm(vm,iova,bytes);
 return 0;
}

static int fg_abi_present_held(void)
{
 if (!fg_abi_hold || !fg_abi_present_wait) { fg_abi_hold_since=0; return 0; }
 const uint64_t now=rsr("cntpct_el0");
 if (!fg_abi_hold_since) fg_abi_hold_since=now;
 if (now-fg_abi_hold_since>rsr("cntfrq_el0")*4/1000) { fg_abi_present_wait=0; fg_abi_hold_since=0; fg_abi_q_stats[5]++; return 0; }
 return 1;
}

#define FG_CF_DUE 4u
#define FG_CF_WRITERS 8u
static uint32_t fg_cf_due[FG_CF_DUE], fg_cf_due_n, fg_cf_gen, fg_cf_held_gen, fg_cf_expired_gen, fg_cf_w_next;
static struct { uint32_t id, seq; } fg_cf_w[FG_CF_WRITERS];
static uint64_t fg_cf_since;
static uint32_t fg_cf_stats[6];
#define FG_CF_LD(x) __atomic_load_n(&(x), __ATOMIC_RELAXED)
#define FG_CF_ST(x, v) __atomic_store_n(&(x), (v), __ATOMIC_RELAXED)
static void fg_cf_reset(void) { fg_cf_holding=fg_cf_due_n=fg_cf_gen=fg_cf_held_gen=fg_cf_expired_gen=fg_cf_w_next=0; fg_cf_since=0; memset(fg_cf_w,0,sizeof(fg_cf_w)); memset(fg_cf_stats,0,sizeof(fg_cf_stats)); }
void fuxi_gpu_present_writer(uint32_t id, int active)
{
 if (!fg_abi_cf || !id) return;
 uint32_t cur_seq = fg_bb_vm ? FG_CF_LD(fg_bb_vm->seq) : FG_CF_LD(fg_abi_seq);
 uint32_t seq = active ? cur_seq : UINT32_MAX, slot = FG_CF_WRITERS;
 for (unsigned i = 0; i < FG_CF_WRITERS; i++) if (FG_CF_LD(fg_cf_w[i].id) == id) slot = i;
 if (slot == FG_CF_WRITERS) { slot = __atomic_fetch_add(&fg_cf_w_next, 1, __ATOMIC_RELAXED) % FG_CF_WRITERS; FG_CF_ST(fg_cf_w[slot].id, id); }
 FG_CF_ST(fg_cf_w[slot].seq, seq);
}
void fuxi_gpu_present_due(const uint32_t *ids, unsigned count)
{
 if (!fg_abi_cf) return;
 if (count > FG_CF_DUE) count = FG_CF_DUE;
 unsigned old = FG_CF_LD(fg_cf_due_n), added = 0, removed = 0;
 for (unsigned i = 0; i < count; i++) { unsigned f = 0; for (unsigned j = 0; j < old; j++) f |= FG_CF_LD(fg_cf_due[j]) == ids[i]; added |= !f; }
 for (unsigned j = 0; j < old; j++) { unsigned f = 0; for (unsigned i = 0; i < count; i++) f |= FG_CF_LD(fg_cf_due[j]) == ids[i]; removed |= !f; }
 FG_CF_ST(fg_cf_due_n, 0);
 for (unsigned i = 0; i < count; i++) FG_CF_ST(fg_cf_due[i], ids[i]);
 FG_CF_ST(fg_cf_due_n, count);
 if (added) { uint32_t g = FG_CF_LD(fg_cf_gen)+1; FG_CF_ST(fg_cf_gen, g ? g : 1); }
 fg_cf_stats[4]++;
 if (removed && FG_CF_LD(fg_cf_holding)) { fg_cf_stats[2]++; FG_CF_ST(fg_cf_holding, 0); FG_ABI_WAKE(); }
}
static uint64_t fg_cf_bound(void) { return rsr("cntfrq_el0")*(fg_scanout_period_us ? fg_scanout_period_us : 16667u)/1000000; }
static int fg_cf_due_writer(uint32_t before, uint32_t *wfence, int count)
{
 const uint32_t n = FG_CF_LD(fg_cf_due_n);
 const uint32_t cur_seq = fg_bb_vm ? fg_bb_vm->seq : fg_abi_seq;
 unsigned due = 0;
 for (unsigned i = 0; i < n && i < FG_CF_DUE; i++) {
  uint32_t id = FG_CF_LD(fg_cf_due[i]), seq = UINT32_MAX, known = 0;
  for (unsigned k = 0; k < FG_CF_WRITERS; k++) if (FG_CF_LD(fg_cf_w[k].id) == id) { seq = FG_CF_LD(fg_cf_w[k].seq); known = 1; }
  if (!known && count) fg_cf_stats[5]++;
  if (seq == UINT32_MAX || seq == cur_seq || (before && seq+1 >= before)) continue;
  if (!due || seq+1 < *wfence) *wfence = seq+1;
  due = 1;
 }
 return due;
}
static int fg_abi_copy_first(void)
{
 if (!fg_abi_cf) return 0;
 const uint32_t gen = FG_CF_LD(fg_cf_gen);
 uint32_t wfence = 0;
 const unsigned due = fg_cf_due_writer(0, &wfence, 1);
 if (!due || gen == fg_cf_expired_gen) { FG_CF_ST(fg_cf_holding, 0); return 0; }
 const uint64_t now = rsr("cntpct_el0");
 if (fg_cf_held_gen != gen) { fg_cf_held_gen = gen; fg_cf_since = now; fg_cf_stats[1]++; }
 if (now-fg_cf_since > fg_cf_bound()) { fg_cf_expired_gen = gen; fg_cf_stats[3]++; FG_CF_ST(fg_cf_holding, 0); return 0; }
 fg_cf_stats[0]++; FG_CF_ST(fg_cf_holding, 1);
 return 1;
}
static unsigned fg_abi_inflight(void)
{
 const struct fuxi_gpu_vm *vm = fg_active_vm ? fg_active_vm : fg_default_vm;
 return vm ? (vm->seq - vm->completed) : (fg_abi_seq - fg_abi_completed);
}

static uint32_t fg_pk_words[FG_ABI_JOB_WORDS];
static uint32_t fg_pk_fence, fg_pk_wfence, fg_pk_gen, fg_pk_stats[FG_PK_REASONS+3], fg_pk_idle_n;
static uint64_t fg_pk_since, fg_pk_retired, fg_pk_sum, fg_pk_max, fg_pk_idle_sum, fg_pk_idle_max;
static void fg_pk_stats_reset(void) { fg_pk_idle_n=0; fg_pk_sum=fg_pk_max=fg_pk_idle_sum=fg_pk_idle_max=0; memset(fg_pk_stats,0,sizeof(fg_pk_stats)); }
static void fg_pk_reset(void) { fg_pk_n=fg_pk_fence=fg_pk_wfence=fg_pk_gen=0; fg_pk_since=fg_pk_retired=0; fg_pk_stats_reset(); }
static int fg_pk_written(uint32_t fence)
{
 const uint32_t foff = fg_vm_fence_off(fg_bb_vm ? fg_bb_vm : fg_default_vm);
 uint32_t f=g3_read(G3_FENCE_PA+foff+4);
 return !fg_access_failed && (int32_t)(f-fence)>=0;
}
uint32_t fuxi_gpu_abi_seq(void)
{
 const struct fuxi_gpu_vm *vm = fg_bb_vm ? fg_bb_vm : fg_default_vm;
 return vm ? FG_CF_LD(vm->seq) : FG_CF_LD(fg_abi_seq);
}
int fuxi_gpu_abi_fence_passed(uint32_t fence) { return !fence || (fuxi_gpu_ready() && fg_pk_written(fence)); }
static int fg_pk_flush(unsigned why)
{
 if (!fg_pk_n) return 0;
 const unsigned n=fg_pk_n; fg_pk_n=0;
 const uint64_t now=rsr("cntpct_el0"), d=now-fg_pk_since;
 fg_pk_stats[why<FG_PK_REASONS ? why : FG_PK_OTHER]++; fg_pk_sum+=d; if (d>fg_pk_max) fg_pk_max=d;
 if (fg_pk_retired) { uint64_t i=now-fg_pk_retired; fg_pk_idle_sum+=i; fg_pk_idle_n++; if (i>fg_pk_idle_max) fg_pk_idle_max=i; }
 if (!fg_inl_usable()) { fg_abi_poisoned=1; return -1; }
 if (g3_faulted()) return fg_abi_fault("client parked pre-ring fault");
 const unsigned queued=fg_client_busy;
 for (unsigned i=0;i<n;i++) g3_emit(fg_pk_words[i]);
 g3_wptr &= 8191;
 __asm__ volatile("dsb sy" ::: "memory");
 if (!queued) { fg_client_submit_at=now; fg_client_deadline=now+rsr("cntfrq_el0")/10; fg_abi_stage=0; }
 fg_client_busy=1;
 fg_dcvs_kick();
 if (!fg_ring_doorbell() || fg_access_failed) return fg_abi_fault("client parked doorbell");
 if (!queued) { uint64_t deadline; clock_interval_to_deadline(2,NSEC_PER_MSEC,&deadline); (void)thread_call_enter_delayed(&fg_client_watchdog,deadline); }
 else {
  fg_abi_q_stats[0]++;
  unsigned fl = fg_abi_inflight();
  if (fl > fg_abi_q_stats[1]) fg_abi_q_stats[1] = fl;
 }
 return 0;
}
static int fg_pk_check(void)
{
 const uint64_t now=rsr("cntpct_el0");
 uint32_t wf=0;
 if (!fg_pk_retired && fg_pk_written(fg_pk_wfence)) fg_pk_retired=now;
 unsigned why=!fg_cf_due_writer(fg_pk_fence,&wf,0) ? FG_PK_NOTDUE :
     fg_pk_retired && now-fg_pk_retired>=rsr("cntfrq_el0")*FG_PK_IDLE_US/1000000 ? FG_PK_IDLE : now-fg_pk_since>=2*fg_cf_bound() ? FG_PK_CAP : FG_PK_REASONS;
 if (why==FG_PK_REASONS) return 0;
 if (why!=FG_PK_NOTDUE) fg_cf_expired_gen=fg_pk_gen;
 return fg_pk_flush(why);
}
static int fg_pk_want(const struct fuxi_gpu_vm *vm, unsigned n, uint32_t *wfence)
{
 if (!fg_abi_pk || fg_pk_n || !vm || vm->client_class != FG_CLASS_COMPOSITOR) return 0;
 const uint32_t gen=FG_CF_LD(fg_cf_gen);
 if (gen==fg_cf_expired_gen || !fg_cf_due_writer(0,wfence,1)) return 0;
 uint32_t used=(g3_wptr-g3_gr(0x806))&8191;
 if (fg_access_failed || used+n+1024+64>8191) { fg_pk_stats[FG_PK_REASONS+1]++; return 0; }
 return 1;
}
static void fg_pk_park(struct fuxi_gpu_vm *vm, const uint32_t *words, unsigned n, uint32_t wfence)
{
 memcpy(fg_pk_words,words,n*sizeof(words[0]));
 fg_pk_n=n; fg_pk_fence=vm->seq+1; fg_pk_wfence=wfence; fg_pk_gen=FG_CF_LD(fg_cf_gen);
 fg_pk_since=rsr("cntpct_el0"); fg_pk_retired=fg_pk_written(wfence) ? fg_pk_since : 0;
 fg_pk_stats[FG_PK_REASONS]++; if (fg_pk_retired) fg_pk_stats[FG_PK_REASONS+2]++;
 if (!fg_client_busy) { uint64_t deadline; clock_interval_to_deadline(2,NSEC_PER_MSEC,&deadline); (void)thread_call_enter_delayed(&fg_client_watchdog,deadline); }
}
static void fg_cf_log(void)
{
 if (!fg_abi_cf) return;
 printf("FUXI GPU: copy_first refused=%u episodes=%u released=%u expired=%u due_publishes=%u no_writer=%u bound_us=%u\n",
     fg_cf_stats[0], fg_cf_stats[1], fg_cf_stats[2], fg_cf_stats[3], fg_cf_stats[4], fg_cf_stats[5], fg_scanout_period_us);
 memset(fg_cf_stats, 0, sizeof(fg_cf_stats));
 if (!fg_abi_pk) return;
 const uint64_t f=rsr("cntfrq_el0") ? rsr("cntfrq_el0") : 1, r=fg_pk_stats[FG_PK_COPY]+fg_pk_stats[FG_PK_IDLE]+fg_pk_stats[FG_PK_CAP]+fg_pk_stats[FG_PK_SUBMIT]+fg_pk_stats[FG_PK_NOTDUE]+fg_pk_stats[FG_PK_OTHER];
 printf("FUXI GPU: copy_first_park parked=%u released_by copy=%u idle=%u cap=%u submit=%u not_due=%u other=%u no_room=%u writer_done_at_park=%u"
     " parked_us avg=%llu max=%llu writer_done_to_release_us n=%u avg=%llu max=%llu idle_us=%u\n",
     fg_pk_stats[FG_PK_REASONS], fg_pk_stats[FG_PK_COPY], fg_pk_stats[FG_PK_IDLE], fg_pk_stats[FG_PK_CAP], fg_pk_stats[FG_PK_SUBMIT], fg_pk_stats[FG_PK_NOTDUE],
     fg_pk_stats[FG_PK_OTHER], fg_pk_stats[FG_PK_REASONS+1], fg_pk_stats[FG_PK_REASONS+2],
     (unsigned long long)(r ? fg_pk_sum*1000000/f/r : 0), (unsigned long long)(fg_pk_max*1000000/f), fg_pk_idle_n,
     (unsigned long long)(fg_pk_idle_n ? fg_pk_idle_sum*1000000/f/fg_pk_idle_n : 0), (unsigned long long)(fg_pk_idle_max*1000000/f), FG_PK_IDLE_US);
 fg_pk_stats_reset();
}

enum { FG_SS_IDLE, FG_SS_FRAME, FG_SS_TLB, FG_SS_CACHE, FG_SS_RING, FG_SS_BELL, FG_SS_LOCK, FG_SS_REFUSED, FG_SS_N };
static uint64_t fg_abi_ss_t[FG_SS_N], fg_abi_ss_cur[FG_SS_N], fg_abi_ss_lockmax, fg_abi_ss_clean, fg_abi_ss_skip, fg_abi_ss_cur_clean, fg_abi_ss_cur_skip;
static uint32_t fg_abi_ss_att, fg_abi_ss_ok, fg_abi_ss_why[5];
#define FG_SS(i) do { uint64_t n_=rsr("cntpct_el0"); fg_abi_ss_cur[i]+=n_-m; m=n_; } while (0)
#define FG_REFUSE(k) do { fg_abi_ss_why[k]++; return 1; } while (0)

static int fg_abi_submit_locked(struct fuxi_gpu_vm *vm, const uint64_t *iovas, const uint64_t *sizes,
    unsigned count, uint64_t *fence)
{
 uint64_t m=rsr("cntpct_el0");
 if (!vm || !vm->in_use || !fg_abi_live || !fuxi_gpu_ready() || fg_abi_poisoned || vm->poisoned || vm->seq==UINT32_MAX) return -1;

 const unsigned is_app = (vm->client_class == FG_CLASS_APP);
 const unsigned inl = (!is_app && fg_inl_usable());

 if (is_app) {
  /* Class 1 yields immediately if Class 0 is waiting or present needs GPU. */
  if (fg_busy || fg_scanout_reserved) FG_REFUSE(0);
  if (FG_CF_LD(fg_bb_waiting) > 0 || fg_abi_present_wait || FG_CF_LD(fg_cf_holding)) FG_REFUSE(1);
  if (fg_pk_n) FG_REFUSE(1);
  if (fg_client_busy && fg_active_vm != vm) FG_REFUSE(0);
  if (fg_abi_present_held()) { fg_abi_q_stats[4]++; FG_REFUSE(1); }
 } else {
  if (!inl && (fg_busy || fg_scanout_reserved)) FG_REFUSE(0);
  if (!inl && fg_abi_present_held()) { fg_abi_q_stats[4]++; FG_REFUSE(1); }
  if (fg_pk_n) { int r=fg_pk_flush(FG_PK_SUBMIT); if (r) return r; }
  if (inl && !fg_abi_pk && fg_abi_copy_first()) FG_REFUSE(1);
  if (fg_client_busy && fg_active_vm && fg_active_vm != vm) FG_REFUSE(0);
 }

 const int ibp=fg_ibp_want(vm);
 int queued=0, idle=fg_abi_wait_idle();
 if (idle<0) return -1;
 if (fg_active_vm && fg_active_vm != vm && idle) FG_REFUSE(0);
 if (idle || (inl && fg_active_vm == vm)) {
  if (!fg_abi_queue && idle) FG_REFUSE(2);
  if (!inl && fg_abi_present_wait) { fg_abi_q_stats[2]++; FG_REFUSE(3); }
  uint32_t used=(g3_wptr-g3_gr(0x806))&8191;
  if (fg_access_failed) return fg_abi_guilty_fault(vm, "client queue ring read");
  uint32_t inflight_cap = is_app ? FG_CLASS1_INFLIGHT_MAX : FG_ABI_QUEUE_MAX;
  if ((vm->seq - vm->completed) >= inflight_cap ||
      used+(fg_js_on ? FG_ABI_JOB_WORDS : FG_ABI_FRAME_WORDS)+(ibp ? FG_IBP_STAMP_WORDS*(count+1) : 0)+64>8191) {
   fg_abi_q_stats[3]++; FG_REFUSE(4);
  }
  if (g3_faulted()) return fg_abi_guilty_fault(vm, "client pre-queue fault");
  queued=idle>0;
 } else if (g3_faulted() || g3_gr(0x806)!=g3_wptr || g3_gr(0x807)!=g3_wptr || (g3_gr(0x210)&~1u)) {
  return fg_abi_guilty_fault(vm, "client pre-submit idle/fault");
 }
 FG_SS(FG_SS_IDLE);

 uint32_t *words=fg_abi_words;
 unsigned n=fg_abi_frame(words+(fg_js_on ? 4 : 0),iovas,sizes,count,G3_FENCE_VA+vm->fence_off,vm->seq,vm->seq+1,1);
 if (!n) return -1;
 if (fg_js_on) n=fg_js_wrap(words,n,vm->seq+1);
 unsigned ibp_on=0;
 if (ibp) { const unsigned m2=fg_ibp_insert(words,n,iovas,sizes,count); if (m2) { fg_ibp_decode(vm,iovas,sizes,count); words=fg_ibp_words; n=m2; ibp_on=1; } }
 FG_SS(FG_SS_FRAME);
 fg_abi_ibs[0]++; fg_abi_ibs[1]+=count; if (count>fg_abi_ibs[2]) fg_abi_ibs[2]=count; if (count>16) fg_abi_ibs[3]++;

 uint32_t pk_writer=0;
 const unsigned park=inl && fg_pk_want(vm,n,&pk_writer);
 if (queued || (inl && fg_active_vm == vm)) {
  if (vm->map_seq != vm->submit_map_seq && (fg_abi_tlb_asid(vm->asid) || fg_access_failed))
   return fg_abi_guilty_fault(vm, "client queue TLB");
 } else {
  if (fg_abi_inline && !is_app) (void)fg_inl_setup_vm(vm);
  if (!fg_abi_switch_vm(vm)) return fg_abi_guilty_fault(vm, "client install TTBR");
 }
 fg_active_vm = vm;
 FG_SS(FG_SS_TLB);

 fg_abi_ss_cur_clean=fg_abi_cleaned; fg_abi_ss_cur_skip=fg_abi_skipped;
 fg_abi_import_cache_vm(vm, 0);
 fg_abi_ss_cur_clean=fg_abi_cleaned-fg_abi_ss_cur_clean; fg_abi_ss_cur_skip=fg_abi_skipped-fg_abi_ss_cur_skip;
 __asm__ volatile("dsb sy" ::: "memory");
 FG_SS(FG_SS_CACHE);
 if (park) {
  vm->submit_map_seq=vm->map_seq;
  if (vm==fg_default_vm) fg_abi_submit_map_seq=vm->map_seq;
  fg_pk_park(vm,words,n,pk_writer);
  *fence=++vm->seq;
  if (vm==fg_default_vm) fg_abi_seq=vm->seq;
  FG_SS(FG_SS_BELL);
  return 0;
 }
 for (unsigned i=0;i<n;i++) g3_emit(words[i]);
 g3_wptr &= 8191;
 __asm__ volatile("dsb sy" ::: "memory");
 FG_SS(FG_SS_RING);
 if (!queued) {
  uint64_t freq=rsr("cntfrq_el0");
  fg_client_submit_at = rsr("cntpct_el0");
  fg_client_deadline = fg_client_submit_at + (is_app ? freq*fg_class1_quantum_ms/1000 : freq/10);
  fg_abi_stage=0;
 }
 fg_client_busy=1;
 fg_dcvs_kick();
 vm->submit_map_seq=vm->map_seq;
 if (vm==fg_default_vm) fg_abi_submit_map_seq=vm->map_seq;
 if (!fg_ring_doorbell() || fg_access_failed) return fg_abi_guilty_fault(vm, "client doorbell");
 *fence=++vm->seq;
 if (vm==fg_default_vm) fg_abi_seq=vm->seq;
 if (ibp_on) { fg_ibp_active=1; fg_ibp_seq=vm->seq; fg_ibp_count=count; }
 if (fg_ibp_on) { fg_ibp_jobs++; fg_ibp_rate(); }
 FG_SS(FG_SS_BELL);
 if (queued) {
  fg_abi_q_stats[0]++;
  if (vm->seq-vm->completed>fg_abi_q_stats[1]) fg_abi_q_stats[1]=vm->seq-vm->completed;
  return 0;
 }
 uint64_t deadline;
 clock_interval_to_deadline(2,NSEC_PER_MSEC,&deadline);
 (void)thread_call_enter_delayed(&fg_client_watchdog,deadline);
 return 0;
}

int fuxi_gpu_abi_vm_unmap(struct fuxi_gpu_vm *vm, uint64_t iova, uint64_t bytes)
{
 FG_GUARD;
 return fg_abi_unmap_locked(vm, iova, bytes);
}
int fuxi_gpu_abi_unmap(uint64_t iova, uint64_t bytes)
{
 FG_GUARD;
 struct fuxi_gpu_vm *vm = fg_ensure_default_vm();
 return fg_abi_unmap_locked(vm, iova, bytes);
}
int fuxi_gpu_abi_vm_submit(struct fuxi_gpu_vm *vm, const uint64_t *iovas, const uint64_t *sizes,
    unsigned count, uint64_t *fence)
{
 uint64_t t0=rsr("cntpct_el0");
 FG_GUARD;
 uint64_t t1=rsr("cntpct_el0");
 memset(fg_abi_ss_cur,0,sizeof(fg_abi_ss_cur));
 int s=fg_abi_submit_locked(vm,iovas,sizes,count,fence);
 fg_abi_ss_att++; fg_abi_ss_t[FG_SS_LOCK]+=t1-t0; if (t1-t0>fg_abi_ss_lockmax) fg_abi_ss_lockmax=t1-t0;
 if (!s) {
  fg_abi_ss_ok++; fg_abi_ss_clean+=fg_abi_ss_cur_clean; fg_abi_ss_skip+=fg_abi_ss_cur_skip;
  for (unsigned i=0;i<FG_SS_BELL+1;i++) fg_abi_ss_t[i]+=fg_abi_ss_cur[i];
 } else fg_abi_ss_t[FG_SS_REFUSED]+=rsr("cntpct_el0")-t1;
 return s;
}
int fuxi_gpu_abi_submit(const uint64_t *iovas, const uint64_t *sizes,
    unsigned count, uint64_t *fence)
{
 uint64_t t0=rsr("cntpct_el0");
 FG_GUARD;
 struct fuxi_gpu_vm *vm = fg_ensure_default_vm();
 uint64_t t1=rsr("cntpct_el0");
 memset(fg_abi_ss_cur,0,sizeof(fg_abi_ss_cur));
 int s=fg_abi_submit_locked(vm,iovas,sizes,count,fence);
 fg_abi_ss_att++; fg_abi_ss_t[FG_SS_LOCK]+=t1-t0; if (t1-t0>fg_abi_ss_lockmax) fg_abi_ss_lockmax=t1-t0;
 if (!s) {
  fg_abi_ss_ok++; fg_abi_ss_clean+=fg_abi_ss_cur_clean; fg_abi_ss_skip+=fg_abi_ss_cur_skip;
  for (unsigned i=0;i<FG_SS_BELL+1;i++) fg_abi_ss_t[i]+=fg_abi_ss_cur[i];
 } else fg_abi_ss_t[FG_SS_REFUSED]+=rsr("cntpct_el0")-t1;
 return s;
}
void fuxi_gpu_abi_vm_bb_wait(int delta)
{
 if (delta > 0) __atomic_add_fetch(&fg_bb_waiting, (uint32_t)delta, __ATOMIC_RELAXED);
 else if (delta < 0) __atomic_sub_fetch(&fg_bb_waiting, (uint32_t)(-delta), __ATOMIC_RELAXED);
}

unsigned fuxi_gpu_abi_submit_wait_us(unsigned *kind)
{
 FG_GUARD;
 *kind=(fg_abi_hold && fg_abi_present_wait) || fg_cf_holding ? 0 : fg_busy ? 1 : 2;
 const uint64_t now=rsr("cntpct_el0"), f=rsr("cntfrq_el0");
 if (fg_cf_holding) { uint64_t end=fg_cf_since+fg_cf_bound(); if (now>=end || !f) return 1; uint64_t us=(end-now)*1000000/f+1; return us>16667 ? 16667 : (unsigned)us; }
 if (fg_abi_hold && fg_abi_present_wait && fg_abi_hold_since && f) {
  uint64_t end=fg_abi_hold_since+f*4/1000;
  if (now>=end) return 1;
  uint64_t us=(end-now)*1000000/f+1;
  return us>4000 ? 4000 : (unsigned)us;
 }
 return fg_busy ? 1000 : 100;
}
void fuxi_gpu_abi_clean_split(struct fuxi_gpu_clean_split *o)
{
 FG_GUARD;
 uint64_t f=rsr("cntfrq_el0"); if (!f) f=1;
 memset(o,0,sizeof(*o));
 for (unsigned i=0;i<2;i++) { o->kib[i]=fg_abi_cc_bytes[i]>>10; o->ns[i]=fg_abi_cc_ticks[i]*1000000000ull/f; }
 for (unsigned i=0;i<4;i++) { o->bkt_kib[i]=fg_abi_cc_bb[i]>>10; o->bkt_ns[i]=fg_abi_cc_bt[i]*1000000000ull/f; }
 o->go_kib=fg_abi_cc_go>>10;
 o->dty_state=fg_abi_dirty ? fg_abi_dty_ok+1 : 0; o->dty_checked=fg_abi_dty_st[0]; o->dty_dirty=fg_abi_dty_st[1]; o->dty_kib=fg_abi_dty_st[2]>>10; o->dty_full_kib=fg_abi_dty_st[3]>>10;
 o->dty_check_ns=fg_abi_dty_st[4]*1000000000ull/f; o->dty_clean_ns=fg_abi_dty_st[5]*1000000000ull/f; o->ivac_ns=fg_abi_dty_st[6]*1000000000ull/f; o->submits=(uint32_t)fg_abi_dty_st[7];
 o->dty_imports=0;
 for (unsigned v=0;v<FG_ABI_MAX_VMS;v++) {
  if (!fg_vm_pool[v].in_use) continue;
  for (unsigned i=0;i<fg_vm_pool[v].max_imports;i++) if (fg_vm_pool[v].imports[i].iova && fg_vm_pool[v].imports[i].dty_first) o->dty_imports++;
 }
 memset(fg_abi_dty_st,0,sizeof(fg_abi_dty_st)-sizeof(fg_abi_dty_st[0]));
 for (unsigned v=0;v<FG_ABI_MAX_VMS;v++) {
  if (!fg_vm_pool[v].in_use) continue;
  for (unsigned i=0;i<fg_vm_pool[v].max_imports;i++) if (fg_vm_pool[v].imports[i].iova) {
   o->imports++; o->mapped_kib+=fg_vm_pool[v].imports[i].bytes>>10; if (fg_vm_pool[v].imports[i].gpu_only) o->go_imports++;
   for (unsigned k=0;k<3;k++) if (fg_vm_pool[v].imports[i].bytes>o->top_kib[k]<<10 || !o->top_kib[k]) {
    for (unsigned j=2;j>k;j--) { o->top_kib[j]=o->top_kib[j-1]; o->top_perm[j]=o->top_perm[j-1]; }
    o->top_kib[k]=fg_vm_pool[v].imports[i].bytes>>10; o->top_perm[k]=fg_vm_pool[v].imports[i].permissions|(fg_vm_pool[v].imports[i].gpu_only ? 16 : 0);
    break;
   }
  }
 }
 memset(fg_abi_cc_bytes,0,sizeof(fg_abi_cc_bytes)); memset(fg_abi_cc_ticks,0,sizeof(fg_abi_cc_ticks));
 memset(fg_abi_cc_bb,0,sizeof(fg_abi_cc_bb)); memset(fg_abi_cc_bt,0,sizeof(fg_abi_cc_bt)); fg_abi_cc_go=0;
}
void fuxi_gpu_abi_submit_split(uint64_t ns[8], uint32_t why[5], uint32_t n[2], uint64_t *lockmax_ns, uint64_t kib[2])
{
 FG_GUARD;
 uint64_t f=rsr("cntfrq_el0"); if (!f) f=1;
 for (unsigned i=0;i<FG_SS_N;i++) ns[i]=fg_abi_ss_t[i]*1000000000ull/f;
 *lockmax_ns=fg_abi_ss_lockmax*1000000000ull/f;
 memcpy(why,fg_abi_ss_why,sizeof(fg_abi_ss_why)); n[0]=fg_abi_ss_att; n[1]=fg_abi_ss_ok;
 kib[0]=fg_abi_ss_clean>>10; kib[1]=fg_abi_ss_skip>>10;
 memset(fg_abi_ss_t,0,sizeof(fg_abi_ss_t)); memset(fg_abi_ss_why,0,sizeof(fg_abi_ss_why));
 fg_abi_ss_att=fg_abi_ss_ok=0; fg_abi_ss_lockmax=fg_abi_ss_clean=fg_abi_ss_skip=0;
}

#define FG_ABI_COH_IOVA (FG_ABI_VA_START+FG_ABI_VA_SIZE-0x10000)
#define FG_ABI_COH_IB (FG_ABI_PT+0x3f0000)
#define FG_ABI_COH_RES (FG_ABI_PT+0x3f1000)
_Static_assert(FG_ABI_COH_IB >= FG_ABI_PT+(4u+768u)*4096 && FG_ABI_COH_RES+4096 <= FG_ABI_PT+FG_ABI_PT_BYTES, "coherency test pages overlap tables");
static uint8_t fg_abi_coh_page[16384] __attribute__((aligned(16384)));
static const char *coh_why; static unsigned coh_up, coh_down, coh_def;
static void fg_abi_coh_try(struct fuxi_gpu_vm *vm, unsigned wbmode)
{
 coh_why="not idle"; coh_up=coh_down=coh_def=0;
 if (!vm || !vm->in_use || !fg_abi_live || !fuxi_gpu_ready() || fg_abi_poisoned || fg_abi_wait_idle()) return;
 if (fg_busy) { coh_why="busy"; return; }
 const uint64_t ib=FG_ABI_COH_IOVA, res=ib+0x1000, data=ib+0x4000;
 const char *why="inconclusive";
 unsigned up=0, down=0, mapped=0, definitive=0;
 volatile uint32_t *page=(volatile uint32_t *)fg_abi_coh_page;
 volatile uint32_t *rp=(volatile uint32_t *)G_PTR_RANGE(FG_ABI_COH_RES,4096);
 uint32_t *ibw=(uint32_t *)G_PTR_RANGE(FG_ABI_COH_IB,4096);
 uint64_t fence=0, ibytes=0, cleaned=fg_abi_cleaned;
 unsigned n=0;
 if (fg_abi_map_locked(vm,ib,(uintptr_t)ibw,4096,3,0,2)) { why="map ib"; goto out; }
 mapped|=1;
 if (fg_abi_map_locked(vm,res,(uintptr_t)rp,4096,3,0,2)) { why="map result"; goto out; }
 mapped|=2;
 if (fg_abi_map_locked(vm,data,(uintptr_t)fg_abi_coh_page,sizeof(fg_abi_coh_page),3,wbmode,wbmode ? 3 : 2)) { why="map data"; goto out; }
 mapped|=4;
 for (unsigned k=0;k<16;k++) rp[k]=0;
 for (unsigned k=0;k<8;k++) { page[k*16]=0xC0DE0000u|k; page[(8+k)*16]=0x51A1E000u|k; (void)page[(8+k)*16]; }
 for (unsigned k=0;k<8;k++) {
  ibw[n++]=fg_abi_pkt7(0x73,5); ibw[n++]=0; ibw[n++]=(uint32_t)(res+4*k); ibw[n++]=0; ibw[n++]=(uint32_t)(data+64*k); ibw[n++]=0;
 }
 for (unsigned k=0;k<8;k++) {
  ibw[n++]=fg_abi_pkt7(0x3d,3); ibw[n++]=(uint32_t)(data+(8+k)*64); ibw[n++]=0; ibw[n++]=0x6E57A000u|k;
 }
 ibw[n++]=fg_abi_pkt7(0x12,0); ibw[n++]=fg_abi_pkt7(0x26,0);
 ibytes=4u*n;
 __asm__ volatile("dsb sy" ::: "memory");
 { int s=fg_abi_submit_locked(vm,&ib,&ibytes,1,&fence); if (s) { why=s>0 ? "busy" : "submit"; goto out; } }
 { int o; while ((o=fg_abi_observe_vm(vm))==1) delay(20);
   if (o) { why="fault"; definitive=1; goto out; } }
 for (unsigned k=0;k<8;k++) { up+=rp[k]==(0xC0DE0000u|k); down+=page[(8+k)*16]==(0x6E57A000u|k); }
 definitive=1;
 why=up==8 && down==8 ? "pass" : "stale";
out:
 if (mapped&4) fg_abi_cache_region((uintptr_t)fg_abi_coh_page,sizeof(fg_abi_coh_page),0);
 fg_abi_cleaned=cleaned;
 if (mapped&4) (void)fg_abi_unmap_locked(vm,data,sizeof(fg_abi_coh_page));
 if (mapped&2) (void)fg_abi_unmap_locked(vm,res,4096);
 if (mapped&1) (void)fg_abi_unmap_locked(vm,ib,4096);
 coh_why=why; coh_up=up; coh_down=down; coh_def=definitive;
}
static void fg_abi_coh_probe(struct fuxi_gpu_vm *vm)
{
 struct fuxi_gpu_vm *temp_vm = NULL;
 if (!vm) {
  if (fg_bb_vm && fg_bb_vm->in_use && !fg_bb_vm->poisoned) vm = fg_bb_vm;
  else if (fg_abi_vm_create_locked(FG_CLASS_COMPOSITOR, 0, "coh-probe", &temp_vm) == 0) vm = temp_vm;
 }
 if (!vm) return;
 for (unsigned wbmode=0;wbmode<2;wbmode++) {
  fg_abi_coh_try(vm, wbmode);
  int pass=coh_up==8 && coh_down==8 && coh_def && !fg_abi_poisoned;
  if (pass && wbmode) {
   for (unsigned v=0;v<FG_ABI_MAX_VMS;v++) {
    if (!fg_vm_pool[v].in_use) continue;
    for (unsigned i=0;i<fg_vm_pool[v].max_imports;i++) if (fg_vm_pool[v].imports[i].iova) { pass=0; coh_why="imports already mapped NC"; }
   }
  }
  const char *tag=wbmode ? "(wb-ish)" : "";
  if (pass) {
   fg_abi_coh=1; fg_abi_coh_wb=wbmode;
   printf("FUXI GPU ABI: coherent-import%s pass cpu_to_gpu %u/8 gpu_to_cpu %u/8 -> skip import clean/invalidate%s\n",tag,coh_up,coh_down,wbmode ? ", imports mapped write-back inner-shareable" : "");
   if (temp_vm) fg_abi_vm_destroy_locked(temp_vm);
   return;
  }
  if (!coh_def && ++fg_abi_coh_tries<3) {
   printf("FUXI GPU ABI: coherent-import%s self-test %s cpu_to_gpu %u/8 gpu_to_cpu %u/8 -> keep import cache maintenance (retry)\n",tag,coh_why,coh_up,coh_down);
   if (temp_vm) fg_abi_vm_destroy_locked(temp_vm);
   return;
  }
  printf("FUXI GPU ABI: coherent-import%s self-test %s cpu_to_gpu %u/8 gpu_to_cpu %u/8 -> %s\n",tag,coh_why,coh_up,coh_down,
      wbmode || !coh_def ? "keep import cache maintenance (final)" : "trying write-back inner-shareable");
  if (!coh_def || fg_abi_poisoned) break;
 }
 fg_abi_coh=2;
 if (temp_vm) fg_abi_vm_destroy_locked(temp_vm);
}
int fuxi_gpu_abi_cached_coherent(int probe)
{
 FG_GUARD;
 if (fg_abi_cbo_on) return 1;
 const char *why=!fg_abi_cbo ? "bit29-off" : !fg_abi_cohtest ? "no-selftest-bit64" : NULL;
 if (!why && probe && !fg_abi_coh) fg_abi_coh_probe(NULL);
 if (!why) why=fg_abi_poisoned ? "gpu-poisoned" : !fg_abi_coh ? "selftest-pending" : fg_abi_coh==2 ? "selftest-refused" : !fg_abi_coh_wb ? "selftest-not-wb" : NULL;
 if (!why) { fg_abi_cbo_on=1; printf("FUXI GPU ABI: cached-coherent BOs enabled reason=selftest-wb-ish-pass\n"); return 1; }
 if (why!=fg_abi_cbo_said) { fg_abi_cbo_said=why; printf("FUXI GPU ABI: cached-coherent BOs disabled reason=%s\n",why); }
 return fg_abi_cbo && fg_abi_cohtest && !fg_abi_coh && !fg_abi_poisoned ? -1 : 0;
}
int fuxi_gpu_abi_vm_poll(struct fuxi_gpu_vm *vm, uint64_t fence)
{
 FG_GUARD;
 if (!vm || !vm->in_use || !fg_abi_live || fence>vm->seq) return -1;
 if (fg_abi_queue && fence && fence<=vm->completed) return 0;
 int err=fg_abi_observe_vm(vm);
 if (err<0) return err;
 return fence<=vm->completed ? 0 : 1;
}
int fuxi_gpu_abi_poll(uint64_t fence)
{
 FG_GUARD;
 struct fuxi_gpu_vm *vm = fg_default_vm;
 if (!vm || !vm->in_use || !fg_abi_live || fence>vm->seq) return -1;
 if (fg_abi_queue && fence && fence<=vm->completed) return 0;
 int err=fg_abi_observe_vm(vm);
 if (err<0) return err;
 return fence<=vm->completed ? 0 : 1;
}
int fuxi_gpu_abi_queue_enabled(void) { FG_GUARD; return fg_abi_queue; }
int fuxi_gpu_abi_roimport(void) { FG_GUARD; return fg_abi_roimport; }
int fuxi_gpu_abi_pace(void) { FG_GUARD; return fg_abi_pace; }
int fuxi_gpu_abi_start(void) { FG_GUARD; return fg_abi_start; }
void fuxi_gpu_abi_queue_stats(uint32_t q[6]) { FG_GUARD; memcpy(q,fg_abi_q_stats,sizeof(fg_abi_q_stats)); }
void fuxi_gpu_abi_sched_stats(uint32_t ibs[4], uint32_t wptr[4], uint32_t stall[8])
{
 FG_GUARD;
 memcpy(ibs,fg_abi_ibs,sizeof(fg_abi_ibs)); memcpy(wptr,fg_wptr_stats,sizeof(fg_wptr_stats)); memcpy(stall,fg_abi_stall_stats,sizeof(fg_abi_stall_stats));
}
