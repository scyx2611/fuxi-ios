#!/usr/bin/env python3
"""Wave 2 GPU host ASan/UBSan/LSan adversarial & simulation test suite.

Covers:
1. FuxiAdrenoUserClient lock split (fuxigpulocksplit / fuxi,gpu-locksplit):
   - Concurrent close vs unlocked BoAlloc/BoImport/BoFree rollback
   - BoFree waitUnlocked + Submit vs clientClose UAF barrier (inflightUnlocked_)
   - b->dropping busy refusal on Submit and BoCpuMap
   - Concurrent clientClose reentrancy serialization (closing_ flag)
   - Two-segment wrap-around freeSlot() scan when free_hint_ > 0
   - Regression test: BoAlloc valid==true && unlocked && !live_ single complete() only
   - A/B lock_ contention p95/p99/max benchmark
2. SweetFramebuffer TE observer & idle poll suppression (fuxinopoll / fuxi,nopoll):
   - Mode clamping (2..7 -> 8, 8..16 preserved, >16 -> 0)
   - Faithful fuxi_gpu_scanout_te() + teVsyncTick() + sweet_fb_rearm() simulation
     under normal and extreme 420..500 us jitter + rapid toggle (0 TE losses, 0 past deadlines)
3. WaitFence IRQ slice clamping (fuxigpuirqslice / fuxi,gpu-irqslice):
   - Short deadline (2000 us) never oversleeps when irqSliceUs=8000/16000
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

CPP_SOURCE = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

uint64_t now_us() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          Clock::now().time_since_epoch())
          .count());
}

void busy_or_sleep_us(uint64_t us) {
  if (us == 0) return;
  std::this_thread::sleep_for(std::chrono::microseconds(us));
}

struct SimVm {
  std::atomic<uint64_t> retired_fence{0};
  std::atomic<uint32_t> magic{0x564D4F4Bu};
  std::atomic<uint64_t> irq_sleep_calls{0};

  int poll(uint64_t fence) const {
    assert(magic.load(std::memory_order_acquire) == 0x564D4F4Bu);
    return retired_fence.load(std::memory_order_acquire) >= fence ? 0 : 1;
  }
};

struct SimBackingMemory {
  std::vector<uint8_t> pages;
  std::atomic<uint32_t> refs{1};
  std::atomic<bool> prepared{false};
  std::atomic<bool> wired{false};
  std::atomic<unsigned> complete_calls{0};
  bool fail_unwire = false;

  explicit SimBackingMemory(size_t bytes, bool fail_unwire_ = false)
      : pages(bytes, 0xA5), fail_unwire(fail_unwire_) {}
  void retain() { refs.fetch_add(1, std::memory_order_relaxed); }
  void release() {
    uint32_t prev = refs.fetch_sub(1, std::memory_order_acq_rel);
    assert(prev > 0);
    if (prev == 1) {
      assert(!prepared.load(std::memory_order_acquire));
      delete this;
    }
  }
  void prepare() {
    assert(!prepared.exchange(true, std::memory_order_acq_rel));
  }
  void zero_pages() { std::memset(pages.data(), 0, pages.size()); }
  void wire() { wired.store(true, std::memory_order_release); }
  bool unwire() {
    if (fail_unwire) return false;
    wired.store(false, std::memory_order_release);
    return true;
  }
  void complete() {
    bool was_prepared = prepared.exchange(false, std::memory_order_acq_rel);
    assert(was_prepared && "double complete() detected!");
    complete_calls.fetch_add(1, std::memory_order_relaxed);
  }
};

struct SimBoView {
  SimBackingMemory *backing = nullptr;
  bool bind(SimBackingMemory *m, bool fail_bind = false) {
    if (fail_bind) return false;
    backing = m;
    backing->retain();
    return true;
  }
  void release() {
    if (backing) {
      backing->complete();
      backing->release();
    }
    delete this;
  }
};

struct SimBo {
  uint64_t handle = 0;
  uint64_t bytes = 0;
  uint64_t allocated = 0;
  uint64_t iova = 0;
  bool is_import = false;
  bool pending_alloc = false;
  bool dropping = false;
  uint32_t leases = 0;
  SimBackingMemory *mem = nullptr;
  SimBoView *view = nullptr;
};

class SimUserClient {
 public:
  static constexpr size_t kMaxBos = 1024;
  static constexpr uint32_t kMaxImports = 256;
  static constexpr uint64_t kMaxWired = 256ULL * 1024 * 1024;

  static constexpr int kOk = 0;
  static constexpr int kNotOpen = -1;
  static constexpr int kBadArg = -2;
  static constexpr int kNoRes = -3;
  static constexpr int kBusy = -4;
  static constexpr int kTimeout = -5;

  explicit SimUserClient(bool lock_split, uint32_t irq_slice_us = 1000)
      : lock_split_(lock_split),
        irq_slice_us_(std::clamp(irq_slice_us, 1000u, 16000u)),
        bo_table_(new SimBo[kMaxBos]()),
        vm_(new SimVm()) {}

  ~SimUserClient() {
    client_close();
    delete[] bo_table_;
    bo_table_ = nullptr;
  }

  void advance_fence(uint64_t f) {
    std::lock_guard<std::mutex> lk(lock_);
    if (vm_) vm_->retired_fence.store(f, std::memory_order_release);
    ev_cv_.notify_all();
  }

  int wait_fence(uint64_t fence, uint64_t timeout_us, uint64_t *slept_us_out = nullptr) {
    std::unique_lock<std::mutex> lk(lock_);
    if (!live_) return kNotOpen;
    return wait_unlocked_locked(fence, timeout_us, lk, slept_us_out);
  }

  int bo_cpu_map(uint64_t handle) {
    std::unique_lock<std::mutex> lk(lock_);
    if (!live_) return kNotOpen;
    SimBo *b = find_locked(handle);
    if (!b || b->is_import || b->leases == UINT32_MAX) return kBadArg;
    if (b->dropping) return kBusy;
    ++b->leases;
    return kOk;
  }

  int bo_cpu_unmap(uint64_t handle) {
    std::unique_lock<std::mutex> lk(lock_);
    if (!live_) return kNotOpen;
    SimBo *b = find_locked(handle);
    if (!b || !b->leases) return kBadArg;
    --b->leases;
    return kOk;
  }

  int submit(uint64_t handle, uint64_t *lock_wait_us, uint64_t *total_us,
             bool simulate_ring_wait = false, uint64_t ring_wait_us = 0) {
    uint64_t t0 = now_us();
    std::unique_lock<std::mutex> lk(lock_);
    uint64_t t1 = now_us();
    if (lock_wait_us) *lock_wait_us = t1 - t0;

    if (!live_) {
      if (total_us) *total_us = now_us() - t0;
      return kNotOpen;
    }

    bool dropping = false;
    auto lookup = [&]() -> SimBo * {
      dropping = false;
      SimBo *b = find_locked(handle);
      if (!b || b->dropping || !b->iova) {
        if (b && b->dropping) dropping = true;
        return nullptr;
      }
      return b;
    };

    if (!lookup()) {
      if (total_us) *total_us = now_us() - t0;
      return dropping ? kBusy : kBadArg;
    }

    if (simulate_ring_wait && ring_wait_us > 0) {
      enter_unlocked();
      lk.unlock();
      busy_or_sleep_us(ring_wait_us);
      lk.lock();
      leave_unlocked();
      if (!live_) {
        if (total_us) *total_us = now_us() - t0;
        return kNotOpen;
      }
      if (!lookup()) {
        if (total_us) *total_us = now_us() - t0;
        return dropping ? kBusy : kBadArg;
      }
    }

    busy_or_sleep_us(40);
    if (!first_fence_) first_fence_ = 1;
    ++last_fence_;
    if (vm_) vm_->retired_fence.store(last_fence_, std::memory_order_release);
    if (total_us) *total_us = now_us() - t0;
    return kOk;
  }

  int submit_pending_fence(uint64_t handle, uint64_t *out_fence) {
    std::unique_lock<std::mutex> lk(lock_);
    if (!live_) return kNotOpen;
    SimBo *b = find_locked(handle);
    if (!b || b->dropping || !b->iova) return (b && b->dropping) ? kBusy : kBadArg;
    if (!first_fence_) first_fence_ = 1;
    ++last_fence_;
    if (out_fence) *out_fence = last_fence_;
    return kOk;
  }

  // Models allocate() including SimBoView bind + exact FuxiAdrenoUserClient::allocate() cleanup order
  int bo_alloc(uint64_t bytes, uint64_t sim_work_us, uint64_t *out_handle,
               bool map_immediately = true, bool fail_bind = false,
               std::function<void()> after_bind_hook = nullptr,
               unsigned *observed_complete_calls = nullptr) {
    uint64_t allocated = (bytes + 16383ULL) & ~16383ULL;
    std::unique_lock<std::mutex> lk(lock_);
    if (!live_) return kNotOpen;
    if (wired_ > kMaxWired || allocated > kMaxWired - wired_) return kNoRes;
    SimBo *b = free_slot_locked();
    if (!b) return kNoRes;

    const bool unlocked = lock_split_;
    if (unlocked) {
      b->pending_alloc = true;
      wired_ += allocated;
      enter_unlocked();
      lk.unlock();
    }

    auto *mem = new SimBackingMemory(static_cast<size_t>(allocated));
    mem->prepare();
    busy_or_sleep_us(sim_work_us);
    mem->zero_pages();
    auto *view = new SimBoView();
    bool valid = view->bind(mem, fail_bind);
    if (valid && after_bind_hook) after_bind_hook();

    if (unlocked) {
      lk.lock();
      leave_unlocked();
    }
    if (!valid || (unlocked && !live_)) {
      if (unlocked) {
        wired_ -= allocated;
        b->pending_alloc = false;
        update_free_hint_locked(b);
        lk.unlock();
      }
      if (observed_complete_calls) {
        // Observe complete count safely before mem->release() deletes mem
        if (view) view->release();
        if (!valid) mem->complete();
        *observed_complete_calls = mem->complete_calls.load(std::memory_order_relaxed);
        mem->release();
      } else {
        if (view) view->release();
        if (!valid) mem->complete();
        mem->release();
      }
      return (unlocked && !live_) ? kNotOpen : kNoRes;
    }

    b->pending_alloc = false;
    b->mem = mem;
    b->view = view;
    b->bytes = bytes;
    b->allocated = allocated;
    b->is_import = false;
    b->handle = next_handle_++;
    b->iova = map_immediately ? (0x10000000ULL + b->handle * 0x10000ULL) : 0;
    if (!unlocked) wired_ += allocated;
    ++live_bos_;
    if (out_handle) *out_handle = b->handle;
    return kOk;
  }

  int bo_import(uint64_t bytes, uint64_t sim_work_us, uint64_t *out_handle,
                bool fail_unwire = false) {
    std::unique_lock<std::mutex> lk(lock_);
    if (!live_) return kNotOpen;
    if (wired_ > kMaxWired || bytes > kMaxWired - wired_ || live_imports_ >= kMaxImports) {
      return kNoRes;
    }
    SimBo *b = free_slot_locked();
    if (!b) return kNoRes;

    if (lock_split_) {
      b->pending_alloc = true;
      wired_ += bytes;
      ++live_imports_;
      enter_unlocked();
      lk.unlock();
    }

    auto *mem = new SimBackingMemory(static_cast<size_t>(bytes), fail_unwire);
    mem->prepare();
    busy_or_sleep_us(sim_work_us);
    mem->wire();

    if (lock_split_) {
      lk.lock();
      leave_unlocked();
      if (!live_) {
        wired_ -= bytes;
        if (live_imports_ > 0) --live_imports_;
        b->pending_alloc = false;
        update_free_hint_locked(b);
        lk.unlock();
        (void)mem->unwire();
        mem->complete();
        mem->release();
        return kNotOpen;
      }
    }

    b->pending_alloc = false;
    b->mem = mem;
    b->view = nullptr;
    b->bytes = bytes;
    b->allocated = bytes;
    b->is_import = true;
    b->handle = next_handle_++;
    b->iova = 0x40000000ULL + b->handle * 0x10000ULL;
    if (!lock_split_) {
      wired_ += bytes;
      ++live_imports_;
    }
    ++live_bos_;
    if (out_handle) *out_handle = b->handle;
    return kOk;
  }

  int bo_free(uint64_t handle, uint64_t sim_busy_wait_us = 0) {
    std::unique_lock<std::mutex> lk(lock_);
    if (!live_) return kNotOpen;
    SimBo *b = find_locked(handle);
    if (!b) return kBadArg;
    if (b->leases || b->dropping) return kBusy;

    if (lock_split_) {
      if (sim_busy_wait_us > 0 && last_fence_) {
        b->dropping = true;
        ev_cv_.notify_all();
        (void)wait_unlocked_locked(last_fence_, sim_busy_wait_us, lk, nullptr);
        b = find_locked(handle);
        if (b) b->dropping = false;
        if (!live_) return kNotOpen;
        if (!b) return kBadArg;
        if (b->leases) return kBusy;
      }
      return drop_locked(*b, lk, true);
    }
    if (sim_busy_wait_us > 0) busy_or_sleep_us(sim_busy_wait_us);
    return drop_locked(*b, lk, false);
  }

  void client_close() {
    std::unique_lock<std::mutex> lk(lock_);
    if (!bo_table_) return;
    while (closing_) {
      close_cv_.wait(lk);
    }
    bool do_close = live_;
    if (do_close) {
      live_ = false;
      closing_ = true;
      close_cv_.notify_all();
      while (inflight_unlocked_ > 0) {
        close_cv_.wait(lk);
      }
      for (size_t i = 0; i < kMaxBos; ++i) {
        if (bo_table_[i].handle) {
          assert(!bo_table_[i].dropping);
          (void)drop_locked(bo_table_[i], lk, false);
        }
      }
      if (vm_) {
        vm_->magic.store(0, std::memory_order_release);
        delete vm_;
        vm_ = nullptr;
      }
      closing_ = false;
      close_cv_.notify_all();
    }
    assert(!closing_);
    assert(inflight_unlocked_ == 0);
    assert(wired_ == 0);
    assert(live_bos_ == 0);
    assert(live_imports_ == 0);
    assert(vm_ == nullptr);
  }

  void wait_until_closing_or_closed() {
    std::unique_lock<std::mutex> lk(lock_);
    close_cv_.wait(lk, [&]() { return !live_ || closing_; });
  }
  void wait_until_bo_dropping(uint64_t handle) {
    std::unique_lock<std::mutex> lk(lock_);
    ev_cv_.wait(lk, [&]() {
      SimBo *b = find_locked(handle);
      return !b || b->dropping;
    });
  }

  void set_free_hint_for_test(size_t hint) {
    std::lock_guard<std::mutex> lk(lock_);
    free_hint_ = hint;
  }
  size_t slot_index_of(uint64_t handle) {
    std::lock_guard<std::mutex> lk(lock_);
    SimBo *b = find_locked(handle);
    assert(b != nullptr);
    return static_cast<size_t>(b - bo_table_);
  }

  uint64_t wired_bytes() { std::lock_guard<std::mutex> lk(lock_); return wired_; }
  uint32_t live_bos() { std::lock_guard<std::mutex> lk(lock_); return live_bos_; }
  uint32_t live_imports() { std::lock_guard<std::mutex> lk(lock_); return live_imports_; }
  unsigned inflight_unlocked() { std::lock_guard<std::mutex> lk(lock_); return inflight_unlocked_; }
  bool has_vm() { std::lock_guard<std::mutex> lk(lock_); return vm_ != nullptr; }
  uint64_t unwire_fail_logs() { std::lock_guard<std::mutex> lk(lock_); return unwire_fail_logs_; }
  bool is_bo_dropping(uint64_t handle) {
    std::lock_guard<std::mutex> lk(lock_);
    SimBo *b = find_locked(handle);
    return b ? b->dropping : false;
  }

 private:
  SimBo *find_locked(uint64_t handle) {
    if (!handle || !bo_table_) return nullptr;
    for (size_t i = 0; i < kMaxBos; ++i) {
      if (bo_table_[i].handle == handle) return &bo_table_[i];
    }
    return nullptr;
  }

  SimBo *free_slot_locked() {
    for (size_t i = free_hint_; i < kMaxBos; ++i) {
      if (!bo_table_[i].handle && !bo_table_[i].pending_alloc && !bo_table_[i].dropping) {
        free_hint_ = i;
        return &bo_table_[i];
      }
    }
    for (size_t i = 0; i < free_hint_; ++i) {
      if (!bo_table_[i].handle && !bo_table_[i].pending_alloc && !bo_table_[i].dropping) {
        free_hint_ = i;
        return &bo_table_[i];
      }
    }
    return nullptr;
  }

  void update_free_hint_locked(SimBo *b) {
    size_t idx = static_cast<size_t>(b - bo_table_);
    if (idx < free_hint_) free_hint_ = idx;
  }

  void enter_unlocked() { ++inflight_unlocked_; }
  void leave_unlocked() {
    assert(inflight_unlocked_ > 0);
    if (--inflight_unlocked_ == 0) close_cv_.notify_all();
  }

  int wait_unlocked_locked(uint64_t fence, uint64_t timeout_us,
                           std::unique_lock<std::mutex> &lk,
                           uint64_t *slept_us_out) {
    if (!fence || !first_fence_ || fence < first_fence_ || fence > last_fence_) {
      return kBadArg;
    }
    enter_unlocked();
    SimVm *vm = vm_;
    lk.unlock();

    uint64_t start = now_us();
    uint64_t deadline = start + std::min<uint64_t>(timeout_us, 1000000ULL);
    int rc = kTimeout;
    for (;;) {
      int s = vm ? vm->poll(fence) : 0;
      if (s == 0) {
        rc = kOk;
        break;
      }
      uint64_t t = now_us();
      if (t >= deadline) {
        rc = kTimeout;
        break;
      }
      uint64_t rem_us = deadline - t;
      uint32_t slice = static_cast<uint32_t>(
          std::min<uint64_t>(irq_slice_us_, std::max<uint64_t>(1ULL, rem_us)));
      if (vm) vm->irq_sleep_calls.fetch_add(1, std::memory_order_relaxed);
      busy_or_sleep_us(slice);
    }

    if (slept_us_out) *slept_us_out = now_us() - start;
    lk.lock();
    leave_unlocked();
    return live_ ? rc : kNotOpen;
  }

  int drop_locked(SimBo &b, std::unique_lock<std::mutex> &lk, bool allow_unlock_teardown) {
    SimBackingMemory *mem = b.mem;
    SimBoView *view = b.view;
    uint64_t alloc_bytes = b.allocated;
    bool is_imp = b.is_import;
    if (!allow_unlock_teardown && is_imp && mem && !mem->unwire()) {
      return -6;
    }
    wired_ -= alloc_bytes;
    --live_bos_;
    if (is_imp && live_imports_ > 0) --live_imports_;
    update_free_hint_locked(&b);
    b = SimBo{};

    if (allow_unlock_teardown && mem) {
      enter_unlocked();
      lk.unlock();
      if (is_imp) {
        if (!mem->unwire()) {
          unwire_fail_count_atomic_.fetch_add(1, std::memory_order_relaxed);
        }
        mem->complete();
        mem->release();
      } else {
        if (view) view->release();
        else mem->complete();
        mem->release();
      }
      lk.lock();
      leave_unlocked();
      unwire_fail_logs_ = unwire_fail_count_atomic_.load(std::memory_order_relaxed);
    } else if (mem) {
      if (is_imp) {
        mem->complete();
        mem->release();
      } else {
        if (view) view->release();
        else mem->complete();
        mem->release();
      }
    }
    return kOk;
  }

  bool lock_split_;
  uint32_t irq_slice_us_;
  std::mutex lock_;
  std::condition_variable close_cv_;
  std::condition_variable ev_cv_;
  SimBo *bo_table_ = nullptr;
  SimVm *vm_ = nullptr;
  size_t free_hint_ = 0;
  uint64_t next_handle_ = 1;
  uint64_t wired_ = 0;
  uint64_t first_fence_ = 0;
  uint64_t last_fence_ = 0;
  uint32_t live_bos_ = 0;
  uint32_t live_imports_ = 0;
  unsigned inflight_unlocked_ = 0;
  bool live_ = true;
  bool closing_ = false;
  uint64_t unwire_fail_logs_ = 0;
  std::atomic<uint64_t> unwire_fail_count_atomic_{0};
};

struct FgTeObserver {
  uint64_t edge = 0, period = 0, poll = 0, change = 0, raw_edge = 0;
  unsigned frame = 0, raw_frame = 0, samples = 0, initialized = 0, nominal_us = 0;
};

struct TeSimResult {
  uint64_t wakeups = 0;
  uint64_t vsync_ticks = 0;
  uint64_t active_polls = 0;
  uint64_t idle_polls = 0;
  uint64_t te_losses = 0;
  uint64_t bracket_over_p4 = 0;
  uint64_t max_bracket_us = 0;
  uint64_t past_deadlines = 0;
  uint64_t max_gpu_blit_detect_us = 0;
};

uint32_t clamp_nopoll_mode(uint32_t val) {
  if (val == 1) return 1;
  if (val >= 2 && val <= 7) return 8;
  if (val >= 8 && val <= 16) return val;
  return 0;
}

TeSimResult run_faithful_sweetfb_sim(uint32_t raw_no_poll_mode, uint64_t te_offset_us,
                                      uint64_t true_panel_period_us, uint32_t active_frames,
                                      uint64_t jitter_min_us = 0, uint64_t jitter_max_us = 150,
                                      bool rapid_toggle = false) {
  constexpr uint64_t kNominalUs = 8333;
  constexpr uint64_t kBlitDurUs = 700;
  constexpr uint64_t kIdleUs = 1500000;
  const uint32_t no_poll_mode = clamp_nopoll_mode(raw_no_poll_mode);

  FgTeObserver te{};
  bool te_valid = false;
  bool te_sent = false;
  unsigned te_sent_frame = 0;
  uint64_t te_next = 0;
  uint64_t te_last_notify = 0;
  uint64_t next_tick = kNominalUs;
  uint64_t tick_abs = kNominalUs;
  TeSimResult res{};
  bool ever_locked = false;

  uint32_t rng = 0x12345678u;
  auto next_jitter = [&]() -> uint64_t {
    rng = rng * 1664525u + 1013904223u;
    uint64_t span = (jitter_max_us >= jitter_min_us) ? (jitter_max_us - jitter_min_us + 1u) : 1u;
    return jitter_min_us + ((rng >> 16) % span);
  };

  auto hw_frame_at = [&](uint64_t t) -> unsigned {
    return static_cast<unsigned>(t / true_panel_period_us) & 0xffffu;
  };

  auto fuxi_gpu_scanout_te_sim = [&](uint64_t now) -> int {
    unsigned frame = hw_frame_at(now);
    unsigned delta = (frame - te.frame) & 0xffffu;
    if (!te.initialized || te.nominal_us != kNominalUs || now < te.poll ||
        (!delta && now - te.change > te.period + te.period / 4)) {
      te.period = kNominalUs;
      te.nominal_us = static_cast<unsigned>(kNominalUs);
      te.frame = frame;
      te.poll = te.change = now;
      te.raw_edge = te.edge = 0;
      te.initialized = 1;
      te.samples = 0;
      return 0;
    }
    uint64_t previousPoll = te.poll;
    uint64_t bracket = now - te.poll;
    uint64_t raw = te.poll + bracket / 2;
    te.poll = now;
    if (!delta) return te.samples >= 2 ? 1 : 0;
    if (ever_locked) {
      if (bracket > res.max_bracket_us) res.max_bracket_us = bracket;
      if (bracket > te.period / 4) ++res.bracket_over_p4;
    }
    te.frame = frame;
    te.change = now;
    if (delta != 1 || bracket > te.period / 4) {
      uint64_t predicted = te.edge + delta * te.period;
      uint64_t tolerance = te.period / 16;
      if (te.samples >= 2 && delta <= 4 &&
          predicted + tolerance >= previousPoll && predicted <= now + tolerance) {
        te.edge = predicted < now ? predicted : now;
        return 1;
      }
      te.samples = 0;
      te.raw_edge = 0;
      return 0;
    }
    if (te.samples) {
      unsigned frames = (frame - te.raw_frame) & 0xffffu;
      uint64_t measured = frames ? (raw - te.raw_edge) / frames : 0;
      if (frames > 8 || measured < te.period * 3 / 4 || measured > te.period * 5 / 4) {
        te.samples = 0;
      } else {
        int64_t error = static_cast<int64_t>(measured) - static_cast<int64_t>(te.period);
        te.period += static_cast<uint64_t>(error / 8);
      }
    }
    if (te.samples >= 2) {
      uint64_t predicted = te.edge + te.period;
      int64_t error = static_cast<int64_t>(raw) - static_cast<int64_t>(predicted);
      te.edge = predicted + static_cast<uint64_t>(error / 4);
      if (te.edge > now) te.edge = now;
    } else {
      te.edge = raw;
    }
    te.raw_edge = raw;
    te.raw_frame = frame;
    if (te.samples < 8) te.samples++;
    return te.samples >= 2 ? 1 : 0;
  };

  auto te_vsync_tick = [&](uint64_t now, bool fallback) -> bool {
    bool wasValid = te_valid;
    te_valid = fuxi_gpu_scanout_te_sim(now) != 0;
    if (!te_valid) {
      if (wasValid && te_last_notify) {
        next_tick = te_last_notify + tick_abs;
        return now >= next_tick;
      }
      return fallback;
    }
    if (!wasValid) te_sent = false;
    uint64_t offset = te_offset_us % te.period;
    unsigned target = te_sent ? ((te_sent_frame + 1) & 0xffffu) : te.frame;
    unsigned delta = (te.frame - target) & 0xffffu;
    if (te_sent && delta > 0x7fffu) {
      te_next = te.edge + te.period + offset;
      return false;
    }
    if (!te_sent || delta == 0 || delta > 1) {
      target = te.frame;
      te_next = te.edge + offset;
    }
    if (now < te_next) return false;
    te_sent = true;
    te_sent_frame = target;
    te_next = delta == 1 ? te.edge + offset : te_next + te.period;
    if (te_last_notify && now - te_last_notify < te.period / 2) return false;
    return true;
  };

  auto sweet_fb_rearm = [&](uint64_t now, bool active) -> uint64_t {
    if (now >= next_tick) {
      uint64_t step = (now - next_tick) / tick_abs + 1;
      next_tick += step * tick_abs;
    }
    uint64_t deadline = next_tick;
    uint64_t poll = now + tick_abs / 16;
    if (te_valid) {
      deadline = te_next;
      uint64_t minPoll = te.period / 16;
      if (!minPoll) minPoll = 1;
      poll = now + minPoll;
      if (no_poll_mode && !active && te.samples >= 2) {
        if (no_poll_mode == 1) {
          if (te.samples >= 4) {
            uint64_t margin = te.period / 5;
            uint64_t bracketStep = te.period / 8;
            if (!bracketStep) bracketStep = minPoll;
            uint64_t nextEdge = te.edge + te.period;
            if (nextEdge > now + margin) {
              uint64_t preEdge = nextEdge - margin;
              poll = preEdge > now + minPoll ? preEdge : (now + minPoll);
            } else if (nextEdge + margin > now) {
              poll = now + bracketStep;
            } else {
              poll = now + minPoll;
            }
          }
        } else {
          uint64_t divStep = te.period / no_poll_mode;
          poll = now + (divStep > minPoll ? divStep : minPoll);
        }
      }
    }
    if (poll < deadline || deadline <= now) deadline = poll;
    if (deadline <= now) deadline = now + (te_valid && te.period >= 16 ? te.period / 16 : 1);
    return deadline;
  };

  uint64_t now = 1000;
  bool prev_locked = false;
  uint32_t frames_submitted = 0;
  bool gpu_pending = false;
  uint64_t blit_end = 0;
  uint64_t idle_start = 0;

  while (true) {
    ++res.wakeups;
    bool vsync_tick = te_vsync_tick(now, now >= next_tick);
    if (te_valid) ever_locked = true;
    if (prev_locked && !te_valid) ++res.te_losses;
    prev_locked = te_valid;

    if (gpu_pending && now >= blit_end) {
      uint64_t detect_delay = now - blit_end;
      if (detect_delay > res.max_gpu_blit_detect_us) res.max_gpu_blit_detect_us = detect_delay;
      gpu_pending = false;
    }

    if (vsync_tick) {
      te_last_notify = now;
      ++res.vsync_ticks;
      bool want_submit = frames_submitted < active_frames && !gpu_pending;
      if (rapid_toggle && (res.vsync_ticks % 5 == 2 || res.vsync_ticks % 5 == 4)) {
        want_submit = false;
      }
      if (want_submit) {
        gpu_pending = true;
        blit_end = now + kBlitDurUs;
        ++frames_submitted;
        if (frames_submitted == active_frames) idle_start = now;
      }
    } else {
      if (gpu_pending) ++res.active_polls;
      else if (ever_locked) ++res.idle_polls;
    }

    if (frames_submitted >= active_frames && !gpu_pending && idle_start && now >= idle_start + kIdleUs) {
      break;
    }

    uint64_t dl = sweet_fb_rearm(now, gpu_pending);
    if (dl <= now) ++res.past_deadlines;
    now = std::max(now + 1, dl) + next_jitter();
  }

  te_valid = true;
  te.samples = 4;
  te.period = true_panel_period_us;
  te.edge = now - 2000;
  te_next = now - 500;
  uint64_t safe_dl = sweet_fb_rearm(now, false);
  if (safe_dl <= now) ++res.past_deadlines;

  return res;
}

struct LatencyStats {
  size_t samples_count = 0;
  uint64_t p50_us = 0;
  uint64_t p95_us = 0;
  uint64_t p99_us = 0;
  uint64_t max_us = 0;
  uint64_t over_budget = 0;
  double avg_us = 0.0;
};

LatencyStats compute_stats(std::vector<uint64_t> samples, uint64_t budget_us) {
  assert(!samples.empty());
  std::sort(samples.begin(), samples.end());
  uint64_t sum = 0;
  uint64_t over = 0;
  for (uint64_t v : samples) {
    sum += v;
    if (v > budget_us) ++over;
  }
  auto pct = [&](double p) -> uint64_t {
    size_t idx = static_cast<size_t>(p * static_cast<double>(samples.size() - 1));
    return samples[idx];
  };
  return LatencyStats{
      samples.size(), pct(0.50), pct(0.95), pct(0.99), samples.back(), over,
      static_cast<double>(sum) / static_cast<double>(samples.size()),
  };
}

LatencyStats run_ab_contention_workload(bool lock_split) {
  SimUserClient uc(lock_split);
  uint64_t persistent_bo = 0;
  assert(uc.bo_alloc(65536, 0, &persistent_bo) == 0);

  std::atomic<bool> stop{false};
  std::atomic<bool> bg_started{false};
  std::vector<uint64_t> wait_samples;
  wait_samples.reserve(400);

  std::thread bg([&]() {
    bg_started.store(true, std::memory_order_release);
    for (int i = 0; i < 24; ++i) {
      uint64_t h1 = 0, h2 = 0;
      if (uc.bo_alloc(1024 * 1024, 10000, &h1) == 0) {
        busy_or_sleep_us(1500);
        (void)uc.bo_free(h1, 0);
      }
      busy_or_sleep_us(1500);
      if (uc.bo_import(2 * 1024 * 1024, 14000, &h2) == 0) {
        busy_or_sleep_us(1500);
        uint64_t f = 0;
        (void)uc.submit_pending_fence(persistent_bo, &f);
        std::thread adv([&uc, f]() { busy_or_sleep_us(2000); uc.advance_fence(f); });
        (void)uc.bo_free(h2, 5000);
        adv.join();
      }
      busy_or_sleep_us(1500);
    }
    stop.store(true, std::memory_order_release);
  });

  while (!bg_started.load(std::memory_order_acquire)) std::this_thread::yield();

  while (!stop.load(std::memory_order_acquire)) {
    uint64_t lwait = 0, total = 0;
    int rc = uc.submit(persistent_bo, &lwait, &total);
    assert(rc == 0);
    wait_samples.push_back(lwait);
    busy_or_sleep_us(1500);
    if (wait_samples.size() >= 600) break;
  }

  bg.join();
  uc.client_close();
  return compute_stats(std::move(wait_samples), 8333);
}

void test_bo_alloc_valid_close_no_double_complete() {
  // 1. Regression: valid==true (view->bind succeeded) AND clientClose kills live_ while unlocked.
  //    Must call memory->complete() EXACTLY ONCE (via view->release()) and return kNotOpen.
  {
    SimUserClient uc(true);
    uint64_t h = 0;
    unsigned comp_calls = 0;
    std::thread killer;
    int rc = uc.bo_alloc(
        65536, 200, &h, true, /*fail_bind=*/false,
        [&]() {
          // Concurrent close sets live_=false and waits on inflightUnlocked_ until bo_alloc drops lock
          killer = std::thread([&]() { uc.client_close(); });
          uc.wait_until_closing_or_closed();
        },
        &comp_calls);
    if (killer.joinable()) killer.join();
    assert(rc == SimUserClient::kNotOpen);
    assert(comp_calls == 1);
    assert(uc.wired_bytes() == 0);
    assert(uc.live_bos() == 0);
    assert(uc.inflight_unlocked() == 0);
  }
  // 2. Also verify valid==false (view->bind failed) calls memory->complete() EXACTLY ONCE.
  {
    SimUserClient uc(true);
    uint64_t h = 0;
    unsigned comp_calls = 0;
    int rc = uc.bo_alloc(65536, 0, &h, true, /*fail_bind=*/true, nullptr, &comp_calls);
    assert(rc == SimUserClient::kNoRes);
    assert(comp_calls == 1);
    assert(uc.wired_bytes() == 0);
    uc.client_close();
  }
  std::printf("[PASS] test_bo_alloc_valid_close_no_double_complete (valid==true&&!live_ & fail_bind single complete())\n");
}

void test_free_slot_wraparound() {
  SimUserClient uc(true);
  std::vector<uint64_t> handles(SimUserClient::kMaxBos, 0);
  for (size_t i = 0; i < SimUserClient::kMaxBos; ++i) {
    assert(uc.bo_alloc(16384, 0, &handles[i]) == 0);
  }
  uint64_t extra = 0;
  assert(uc.bo_alloc(16384, 0, &extra) == SimUserClient::kNoRes);
  // Free slot 3 while setting free_hint_ past it; wrap-around scan [0, free_hint_) must reclaim slot 3.
  assert(uc.bo_free(handles[3], 0) == 0);
  uc.set_free_hint_for_test(512);
  uint64_t wrapped = 0;
  assert(uc.bo_alloc(16384, 0, &wrapped) == 0);
  assert(uc.slot_index_of(wrapped) == 3);
  uc.client_close();
  std::printf("[PASS] test_free_slot_wraparound (two-segment [freeHint, max) + [0, freeHint) scan)\n");
}

void test_concurrent_close_and_rollback() {
  for (int trial = 0; trial < 20; ++trial) {
    SimUserClient uc(true);
    uint64_t base_bo = 0;
    assert(uc.bo_alloc(65536, 0, &base_bo) == 0);
    uint64_t f = 0;
    assert(uc.submit_pending_fence(base_bo, &f) == 0);

    std::atomic<bool> started{false};
    std::thread t_alloc([&]() {
      started.store(true, std::memory_order_release);
      uint64_t h = 0;
      (void)uc.bo_alloc(512 * 1024, 4000, &h);
    });
    std::thread t_import([&]() {
      uint64_t h = 0;
      (void)uc.bo_import(512 * 1024, 4000, &h);
    });
    std::thread t_free([&]() { (void)uc.bo_free(base_bo, 4000); });

    while (!started.load(std::memory_order_acquire)) std::this_thread::yield();
    busy_or_sleep_us(1000);
    uc.client_close();

    t_alloc.join();
    t_import.join();
    t_free.join();

    assert(uc.wired_bytes() == 0);
    assert(uc.live_bos() == 0);
    assert(uc.live_imports() == 0);
  }

  {
    SimUserClient uc(true);
    uint64_t h1 = 0, h2 = 0, hi = 0;
    assert(uc.bo_alloc(200ULL * 1024 * 1024, 100, &h1) == 0);
    assert(uc.bo_alloc(100ULL * 1024 * 1024, 100, &h2) == SimUserClient::kNoRes);
    assert(uc.wired_bytes() == 200ULL * 1024 * 1024);
    assert(uc.bo_import(65536, 100, &hi, /*fail_unwire=*/true) == 0);
    assert(uc.bo_free(hi, 0) == 0);
    assert(uc.unwire_fail_logs() == 1);
    uc.client_close();
  }
  std::printf("[PASS] test_concurrent_close_and_rollback (20 close-race trials + quota + unwire-log)\n");
}

void test_bo_free_vs_client_close_uaf() {
  for (int trial = 0; trial < 30; ++trial) {
    SimUserClient uc(true);
    uint64_t bo = 0, f = 0;
    assert(uc.bo_alloc(65536, 0, &bo) == 0);
    assert(uc.submit_pending_fence(bo, &f) == 0);

    std::atomic<bool> free_entered{false};
    int free_rc = 999;
    int sub_rc = 999;

    std::thread t_free([&]() {
      free_entered.store(true, std::memory_order_release);
      free_rc = uc.bo_free(bo, 6000);
    });
    std::thread t_sub([&]() {
      sub_rc = uc.submit(bo, nullptr, nullptr, true, 5000);
    });

    while (!free_entered.load(std::memory_order_acquire)) std::this_thread::yield();
    busy_or_sleep_us(800);
    uc.client_close();

    t_free.join();
    t_sub.join();

    assert(free_rc == SimUserClient::kNotOpen || free_rc == SimUserClient::kOk);
    assert(sub_rc == SimUserClient::kNotOpen || sub_rc == SimUserClient::kBusy || sub_rc == SimUserClient::kOk);
    assert(!uc.is_bo_dropping(bo));
    assert(!uc.has_vm());
    assert(uc.live_bos() == 0);
  }
  std::printf("[PASS] test_bo_free_vs_client_close_uaf (30 trials BoFree waitUnlocked + Submit vs clientClose, 0 ASan UAF)\n");
}

void test_dropping_race_submit_and_cpumap() {
  SimUserClient uc(true);
  uint64_t bo = 0, f = 0;
  assert(uc.bo_alloc(65536, 0, &bo) == 0);
  assert(uc.submit_pending_fence(bo, &f) == 0);

  std::atomic<bool> saw_dropping{false};
  std::atomic<int> submit_during_drop{999};
  std::atomic<int> cpumap_during_drop{999};

  std::thread t_free([&]() {
    int rc = uc.bo_free(bo, 500000);
    assert(rc == SimUserClient::kOk);
  });

  uc.wait_until_bo_dropping(bo);
  if (uc.is_bo_dropping(bo)) {
    saw_dropping.store(true, std::memory_order_release);
    submit_during_drop.store(uc.submit(bo, nullptr, nullptr), std::memory_order_release);
    cpumap_during_drop.store(uc.bo_cpu_map(bo), std::memory_order_release);
  }
  uc.advance_fence(f);

  t_free.join();
  assert(saw_dropping.load(std::memory_order_acquire));
  assert(submit_during_drop.load(std::memory_order_acquire) == SimUserClient::kBusy);
  assert(cpumap_during_drop.load(std::memory_order_acquire) == SimUserClient::kBusy);
  assert(uc.live_bos() == 0);
  assert(uc.wired_bytes() == 0);
  uc.client_close();
  std::printf("[PASS] test_dropping_race_submit_and_cpumap (Submit=-4 Busy, BoCpuMap=-4 Busy while b->dropping=true)\n");
}

void test_double_client_close_reentrancy() {
  for (int trial = 0; trial < 15; ++trial) {
    SimUserClient uc(true);
    uint64_t bo = 0;
    assert(uc.bo_alloc(65536, 0, &bo) == 0);

    std::atomic<bool> alloc_started{false};
    std::thread t_alloc([&]() {
      alloc_started.store(true, std::memory_order_release);
      uint64_t h2 = 0;
      (void)uc.bo_alloc(256 * 1024, 8000, &h2);
    });
    while (!alloc_started.load(std::memory_order_acquire)) std::this_thread::yield();
    busy_or_sleep_us(1000);

    std::atomic<bool> close1_done{false};
    std::atomic<bool> close2_premature{false};
    std::thread t_c1([&]() {
      uc.client_close();
      close1_done.store(true, std::memory_order_release);
    });
    busy_or_sleep_us(1000);
    std::thread t_c2([&]() {
      uc.client_close();
      if (uc.inflight_unlocked() != 0 || uc.live_bos() != 0 || uc.has_vm()) {
        close2_premature.store(true, std::memory_order_release);
      }
    });

    t_alloc.join();
    t_c1.join();
    t_c2.join();
    assert(close1_done.load(std::memory_order_acquire));
    assert(!close2_premature.load(std::memory_order_acquire));
  }
  std::printf("[PASS] test_double_client_close_reentrancy (15 trials: closing_ barrier blocks premature 2nd return)\n");
}

void test_sweetfb_te_observer_and_past_deadline() {
  assert(clamp_nopoll_mode(0) == 0);
  assert(clamp_nopoll_mode(1) == 1);
  for (uint32_t m = 2; m <= 7; ++m) assert(clamp_nopoll_mode(m) == 8);
  for (uint32_t m = 8; m <= 16; ++m) assert(clamp_nopoll_mode(m) == m);
  assert(clamp_nopoll_mode(17) == 0);

  for (uint64_t offset : {6000ULL, 7800ULL}) {
    TeSimResult leg = run_faithful_sweetfb_sim(0, offset, 8252, 600);
    TeSimResult opt = run_faithful_sweetfb_sim(1, offset, 8252, 600);
    TeSimResult div8 = run_faithful_sweetfb_sim(5, offset, 8252, 600);
    assert(leg.wakeups >= 400 && opt.wakeups >= 400 && div8.wakeups >= 400);
    assert(leg.te_losses == 0 && leg.bracket_over_p4 == 0);
    assert(opt.te_losses == 0 && opt.bracket_over_p4 == 0);
    assert(div8.te_losses == 0 && div8.bracket_over_p4 == 0);
    assert(opt.past_deadlines == 0 && div8.past_deadlines == 0);
    assert(opt.vsync_ticks >= leg.vsync_ticks - 1 && opt.vsync_ticks <= leg.vsync_ticks + 1);
    assert(opt.max_gpu_blit_detect_us <= 8252 / 16 + 200);
    assert(opt.idle_polls * 4 < leg.idle_polls);
  }

  for (uint64_t jmin : {420ULL, 0ULL}) {
    for (uint64_t offset : {4000ULL, 6000ULL, 7800ULL}) {
      TeSimResult leg = run_faithful_sweetfb_sim(0, offset, 8252, 300, jmin, 500, true);
      assert(leg.te_losses == 0 && leg.bracket_over_p4 == 0);
      for (uint32_t mode : {1u, 2u, 5u, 7u, 8u, 16u}) {
        TeSimResult r = run_faithful_sweetfb_sim(mode, offset, 8252, 300, jmin, 500, true);
        assert(r.te_losses == 0);
        assert(r.bracket_over_p4 == 0);
        assert(r.past_deadlines == 0);
        assert(r.vsync_ticks >= leg.vsync_ticks - 2 && r.vsync_ticks <= leg.vsync_ticks + 2);
        if (mode == 1u) {
          assert(r.idle_polls * 5 < leg.idle_polls * 2);
        } else if (clamp_nopoll_mode(mode) == 8u) {
          assert(r.idle_polls * 4 < leg.idle_polls * 3);
        }
      }
    }
  }
  std::printf("[PASS] test_sweetfb_te_observer_and_past_deadline (0 TE losses, 0 bracket>P/4, 0 past-dl)\n");
}

void test_irq_slice_clamping() {
  for (uint32_t slice : {1000u, 8000u, 16000u}) {
    SimUserClient uc(true, slice);
    uint64_t bo = 0, f = 0, slept = 0;
    assert(uc.bo_alloc(65536, 0, &bo) == 0);
    assert(uc.submit_pending_fence(bo, &f) == 0);
    int rc = uc.wait_fence(f, 2000, &slept);
    assert(rc == SimUserClient::kTimeout);
    assert(slept <= 4000ULL);
    uc.client_close();
  }
  std::printf("[PASS] test_irq_slice_clamping (2000 us wait bounded under irqSliceUs=1000/8000/16000)\n");
}

}  // namespace

int main() {
  test_bo_alloc_valid_close_no_double_complete();
  test_free_slot_wraparound();
  test_concurrent_close_and_rollback();
  test_bo_free_vs_client_close_uaf();
  test_dropping_race_submit_and_cpumap();
  test_double_client_close_reentrancy();
  test_sweetfb_te_observer_and_past_deadline();
  test_irq_slice_clamping();

  LatencyStats legacy = run_ab_contention_workload(false);
  LatencyStats split = run_ab_contention_workload(true);
  std::printf("[METRIC] lock_ wait legacy p95=%llu max=%llu us | lockSplit=1 p95=%llu max=%llu us\n",
              static_cast<unsigned long long>(legacy.p95_us),
              static_cast<unsigned long long>(legacy.max_us),
              static_cast<unsigned long long>(split.p95_us),
              static_cast<unsigned long long>(split.max_us));
  assert(legacy.p95_us >= 8333);
  assert(legacy.over_budget > 0);
  assert(split.p95_us < 1000);
  assert(split.max_us < 2000);
  assert(split.over_budget == 0);
  std::printf("PASS: Wave 2 userclient lock split, SweetFB nopoll TE observer & IRQ slice ASan/UBSan suite\n");
  return 0;
}
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sanitize', action='store_true', help='Enable ASan+UBSan+LSan (always enabled)')
    ap.parse_args()

    uc = (ROOT / 'xnu/fuxi-adreno/FuxiAdrenoUserClient.cpp').read_text()
    fb = (ROOT / 'xnu/fuxi-unixnu/SweetFramebuffer.cpp').read_text()
    scanout = (ROOT / 'xnu/fuxi-adreno/fuxi_gpu_scanout.inc').read_text()

    for tok in (
        'fuxigpulocksplit', 'fuxi,gpu-locksplit', 'if (!valid) memory->complete();',
        'fuxigpuirqslice', 'fuxi,gpu-irqslice', 'FUXI GPU ABI: lock_ split enabled',
        'FUXI GPU IRQ: WaitFence slice ceiling=',
    ):
        assert tok in uc, f'FuxiAdrenoUserClient.cpp missing {tok!r}'
    for tok in (
        'fuxinopoll', 'fuxi,nopoll', 'SWEET FB: idle TE poll suppress mode=',
        'sweet_fb_nopoll_mode',
    ):
        assert tok in fb, f'SweetFramebuffer.cpp missing {tok!r}'
    assert 'if (te->samples < 8) te->samples++;' in scanout

    with tempfile.TemporaryDirectory(prefix='fuxi-gpu-wave2-') as tmp:
        src = Path(tmp) / 'wave2_sim.cpp'
        exe = Path(tmp) / 'wave2_sim'
        src.write_text('#include <functional>\n' + CPP_SOURCE, encoding='utf-8')
        subprocess.run([
            'clang++', '-std=c++17', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-pthread',
            str(src), '-o', str(exe),
        ], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
