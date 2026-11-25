#!/usr/bin/env python3
"""0122 fuxi_media_signal_trace：SIGKILL／SIGTERM 送往 media／WebKit 目標時記下送出者的主機測試。

趟 27（2026-11-20）mediaserverd 在 Safari 開影片後被 SIGKILL（exit reason ns=2 code=9，只知是一般 kill），
0044 的退出追蹤看不到是誰送的。0122 在 psignal_internal 決定 sig_proc 之後、kernel task 檢查之前呼叫
fuxi_media_signal_trace，沿用 0044 的 fuxi,media-exit-trace 開關與 kind 分類。

本測試從補丁抽出輔助函式，以模擬的 proc／os_reason／backtrace／printf 編譯執行（可加 ASan／UBSan），
檢查：只追 kind 1、32 筆上限、送出者為其他 user 行程才印 user 呼叫鏈、核心呼叫鏈去 slide 且長行不溢位；
並檢查 0044 以非 static 提供兩個輔助函式、呼叫點的閘門與位置。
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
P0122 = ROOT / 'xnu/patches/0122-fuxi-media-signal-trace.patch'
P0044 = ROOT / 'xnu/patches/0044-sweet-crash-log-reason.patch'

PREFIX = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SIGTERM 15
#define SIGKILL 9
struct proc { const char *name; int pid; };
typedef struct proc *proc_t;
#define PROC_NULL ((proc_t)0)
struct os_reason { uint32_t osr_namespace; uint64_t osr_code; };
typedef struct os_reason *os_reason_t;
#define OS_REASON_NULL ((os_reason_t)0)
static struct proc kernel_proc = { "kernel_task", 0 };
static proc_t kernproc = &kernel_proc;
static proc_t current;
static proc_t current_proc(void) { return current; }
static const char *proc_best_name(proc_t p) { return p->name; }
static int proc_getpid(proc_t p) { return p->pid; }
#define SLIDE 0x1000000ul
#define VM_KERNEL_UNSLIDE(v) ((uintptr_t)(v) - SLIDE)
static unsigned frames_to_give = 12;
static unsigned int backtrace(uintptr_t *bt, unsigned int btlen, void *ctl, void *info) {
 (void)ctl; (void)info;
 unsigned n = frames_to_give < btlen ? frames_to_give : btlen;
 for (unsigned i = 0; i < n; i++) bt[i] = SLIDE + 0xfffffff007000000ul + i * 0x10;
 return n;
}
static char out[4096];
static int printf_k(const char *format, ...) {
 size_t used = strlen(out);
 va_list args; va_start(args, format);
 int n = vsnprintf(out + used, sizeof(out) - used, format, args);
 va_end(args); return n;
}
#define printf printf_k
static char crash_name[64]; static int crash_pid, crash_sig, crash_calls;
static void sweet_crash_log(const char *name, int pid, int sig, uint64_t code, uint64_t subcode) {
 assert(code == 0 && subcode == 0);
 snprintf(crash_name, sizeof(crash_name), "%s", name); crash_pid = pid; crash_sig = sig; crash_calls++;
}
static unsigned fuxi_media_exit_kind(const char *name) {
 if (!strcmp(name, "mediaserverd") || !strncmp(name, "com.apple.WebKit", 16)) return 1;
 if (!strcmp(name, "lskdd")) return 2;
 return 0;
}
'''

TEST = r'''
static void reset(void) { out[0] = 0; crash_calls = 0; crash_name[0] = 0; }
int main(void) {
 struct proc media = { "mediaserverd", 28 }, rb = { "runningboardd", 40 }, ld = { "launchd", 1 };
 struct proc lsk = { "lskdd", 70 }, web = { "com.apple.WebKit.GPU", 338 };
 struct os_reason sig9 = { 2, 9 };

 current = &rb; reset();
 fuxi_media_signal_trace(&media, 0, SIGKILL, &sig9);
 assert(strstr(out, "FUXI SIG: mediaserverd[28] sig=9 by=runningboardd[40] flavor=0x0 ns=2 code=0x9\n"));
 assert(strstr(out, "FUXI SIG: kbt 0xfffffff007000000 0xfffffff007000010 "));
 assert(crash_calls == 1 && !strcmp(crash_name, "sig-sender:runningboardd") && crash_pid == 40 && crash_sig == 9);

 /* 不是 kind 1：不印、不吃預算 */
 reset(); fuxi_media_signal_trace(&ld, 0, SIGKILL, &sig9); assert(!out[0] && !crash_calls);
 reset(); fuxi_media_signal_trace(&lsk, 0, SIGKILL, &sig9); assert(!out[0] && !crash_calls);

 /* 自己送給自己、或核心送出：不印 user 呼叫鏈；原因為空時 ns／code 為 0 */
 current = &media; reset();
 fuxi_media_signal_trace(&media, 0x4, SIGTERM, OS_REASON_NULL);
 assert(strstr(out, "mediaserverd[28] sig=15 by=mediaserverd[28] flavor=0x4 ns=0 code=0x0\n") && !crash_calls);
 current = kernproc; reset();
 fuxi_media_signal_trace(&web, 0, SIGKILL, &sig9);
 assert(strstr(out, "by=kernel_task[0]") && !crash_calls);
 current = PROC_NULL; reset();
 fuxi_media_signal_trace(&web, 0, SIGKILL, &sig9);
 assert(strstr(out, "by=(null)[-1]") && !crash_calls);

 /* 沒有核心呼叫鏈時仍印標頭 */
 current = &rb; frames_to_give = 0; reset();
 fuxi_media_signal_trace(&web, 0, SIGKILL, &sig9);
 assert(strstr(out, "FUXI SIG: kbt\n"));
 frames_to_give = 12;

 /* 已用 5 筆；到 32 筆為止，之後不再印 */
 int printed = 5;
 for (int i = 0; i < 40; i++) {
  reset(); fuxi_media_signal_trace(&media, 0, SIGKILL, &sig9);
  if (out[0]) printed++;
 }
 assert(printed == 32);
 puts("PASS: fuxi_media_signal_trace kind-1 only, sender stack only for other user senders, 32-entry cap, kbt unslid");
 return 0;
}
'''


def added_text(patch, path):
    """取補丁中指定檔案的新增行（去掉 '+'）。"""
    text, keep = [], False
    for line in patch.splitlines():
        if line.startswith('+++ '):
            keep = line[4:].strip() == 'b/' + path
            continue
        if keep and line.startswith('+'):
            text.append(line[1:])
    return '\n'.join(text) + '\n'


def function(source, name):
    match = re.search(r'^static void\n' + re.escape(name) + r'\([^;]*?\)\n\{', source, re.M)
    assert match, name
    depth, end = 1, match.end()
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--sanitize', action='store_true', help='build with ASan/UBSan')
    ap.add_argument('--cc', default='clang')
    args = ap.parse_args()
    p0122, p0044 = P0122.read_text(), P0044.read_text()

    # 0044 以非 static 提供兩個輔助函式（有原型），0122 宣告相同簽名
    for proto in ('unsigned fuxi_media_exit_enabled(void);', 'unsigned fuxi_media_exit_kind(const char *name);'):
        assert ('+' + proto) in p0044, proto
        assert ('+' + proto) in p0122, proto
    assert '+static unsigned\n+fuxi_media_exit_' not in p0044
    # 0122 只改 kern_sig.c；呼叫點在 sig_proc 決定後、kernel task 檢查前，且由開關閘住
    assert re.findall(r'^\+\+\+ (\S+)', p0122, re.M) == ['b/bsd/kern/kern_sig.c']
    call = ('+\tif ((signum == SIGKILL || signum == SIGTERM) && sig_proc != PROC_NULL &&\n'
            '+\t    fuxi_media_exit_enabled()) {\n'
            '+\t\tfuxi_media_signal_trace(sig_proc, flavor, signum, signal_reason);\n')
    assert p0122.count(call) == 1, 'call site changed'
    tail = p0122[p0122.index(call):]
    assert re.search(r'\n \tif \(\(sig_task == TASK_NULL\) \|\| is_kerneltask\(sig_task\)\) \{\n', tail)
    head = p0122[:p0122.index(call)]
    assert head.rstrip().endswith('+#if defined(ARM64_BOARD_CONFIG_SWEET)')
    assert re.search(r'\n \t\tsig_proc = p;\n \t\}\n \n', head[-200:]), 'not right after the sig_proc block'
    print('PASS: 0044 exports the helpers; 0122 call is gated and sits before the kernel-task check')

    helper = function(added_text(p0122, 'bsd/kern/kern_sig.c'), 'fuxi_media_signal_trace')
    with tempfile.TemporaryDirectory(prefix='fuxi-media-signal-') as tmp:
        c = Path(tmp) / 'signal.c'
        c.write_text(PREFIX + helper + TEST)
        cmd = [args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', str(c),
               '-o', str(Path(tmp) / 'signal')]
        if args.sanitize:
            cmd[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-sanitize-recover=all']
        subprocess.run(cmd, check=True)
        subprocess.run([str(Path(tmp) / 'signal')], check=True)


if __name__ == '__main__':
    main()
