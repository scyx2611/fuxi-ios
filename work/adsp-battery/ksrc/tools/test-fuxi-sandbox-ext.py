#!/usr/bin/env python3
"""Host tests for 0125-fuxi-sandbox-ext.patch (Sandbox op 5 issue / op 6 consume)."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PATCH_DIR = ROOT / 'xnu/patches'
PATCH_0125 = PATCH_DIR / '0125-fuxi-sandbox-ext.patch'

HARNESS = r'''
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uintptr_t user_addr_t;
struct proc { int pid; };
typedef struct proc *proc_t;
typedef int DTEntry;
#define kSuccess 0
#define TUNABLE(t,n,k,v) t n = v
#define os_atomic_add_orig(p,v,m) __atomic_fetch_add((p),(v),__ATOMIC_RELAXED)
#define os_atomic_or_orig(p,v,m) __atomic_fetch_or((p),(v),__ATOMIC_RELAXED)

static uint32_t adt_sbext_val;
static int adt_sbext_present;
static int adt_sbext_size = 4;
static int fail_copyin_at = -1;
static int fail_copyout_at = -1;
static int copyin_calls;
static int copyout_calls;
static const char *cur_proc_name = "nsurlsessiond";
static char last_log[256];
static unsigned log_count;
static uint32_t fuxi_amfi_enabled = 1;

static int model_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(last_log, sizeof(last_log), fmt, ap);
    va_end(ap);
    log_count++;
    return 0;
}
#define printf model_printf

static int SecureDTLookupEntry(const DTEntry *parent, const char *path, DTEntry *entry) {
    (void)parent;
    if (strcmp(path, "/chosen") == 0) {
        *entry = 1;
        return kSuccess;
    }
    return -1;
}

static int SecureDTGetProperty(DTEntry entry, const char *name, void const **value, unsigned int *size) {
    if (entry == 1 && strcmp(name, "fuxi,sb-ext") == 0 && adt_sbext_present) {
        *value = &adt_sbext_val;
        *size = (unsigned int)adt_sbext_size;
        return kSuccess;
    }
    return -1;
}

static int copyin(const user_addr_t uaddr, void *kaddr, size_t len) {
    int idx = copyin_calls++;
    if (idx == fail_copyin_at) {
        return EFAULT;
    }
    memcpy(kaddr, (const void *)uaddr, len);
    return 0;
}

static int copyout(const void *kaddr, user_addr_t uaddr, size_t len) {
    int idx = copyout_calls++;
    if (idx == fail_copyout_at) {
        return EFAULT;
    }
    memcpy((void *)uaddr, kaddr, len);
    return 0;
}

static void proc_selfname(char *buf, int len) {
    snprintf(buf, (size_t)len, "%s", cur_proc_name);
}

static void proc_name(int pid, char *buf, int len) {
    (void)pid;
    snprintf(buf, (size_t)len, "%s", cur_proc_name);
}

static int proc_pid(struct proc *proc_ptr) {
    return proc_ptr->pid;
}

static int fuxi_sandbox_container(user_addr_t arg) { (void)arg; return ENOENT; }
static int fuxi_sys_container_result(int err) { return err; }

#include "fuxi_sbext.h"

static uint64_t sweet_sandbox_seen;
SANDBOX_SYSCALL_FN

int main(void) {
    struct proc proc_obj = { 42 };
    char token_buf[2048];
    const char *cls = "com.apple.mediaserverd.read-write";
    const char *path = "/private/var/mobile/Library/Recordings";
    struct fuxi_sbext_issue_args issue = {
        .extension_class = (user_addr_t)cls,
        .type = 0,
        .target = (user_addr_t)path,
        .flags = 0x10,
        .out_token = (user_addr_t)token_buf,
        .pid = 0,
        .pidversion = 0,
    };
    int64_t handle = -1;
    struct fuxi_sbext_consume_args consume = {
        .token = (user_addr_t)token_buf,
        .token_len = 16,
        .out_handle = (user_addr_t)&handle,
    };

    /* Default off: op 5 and op 6 keep ENOENT, op 7 release stays 0, op 0x43 stays ENOENT. */
    fuxi_sbext_init();
    assert(fuxi_sbext_enabled == 0);
    assert(sweet_sandbox_syscall(&proc_obj, 5, (user_addr_t)&issue) == ENOENT);
    assert(sweet_sandbox_syscall(&proc_obj, 6, (user_addr_t)&consume) == ENOENT);
    assert(sweet_sandbox_syscall(&proc_obj, 7, 0) == 0);
    assert(sweet_sandbox_syscall(&proc_obj, 0x43, 0) == ENOENT);

    /* Malformed ADT size stays off. */
    adt_sbext_present = 1;
    adt_sbext_val = 1;
    adt_sbext_size = 2;
    fuxi_sbext_init();
    assert(fuxi_sbext_enabled == 0);

    /* Enable via ADT fuxi,sb-ext=1. */
    adt_sbext_size = 4;
    fuxi_sbext_init();
    assert(fuxi_sbext_enabled == 1);
    assert(strstr(last_log, "FUXI SBEXT: sandbox extension issue/consume on") != NULL);

    /* op 5: NULL arg and invalid fields. */
    assert(sweet_sandbox_syscall(&proc_obj, 5, 0) == EFAULT);
    struct fuxi_sbext_issue_args bad_issue = issue;
    bad_issue.out_token = 0;
    assert(sweet_sandbox_syscall(&proc_obj, 5, (user_addr_t)&bad_issue) == EINVAL);
    bad_issue = issue;
    bad_issue.extension_class = 0;
    assert(sweet_sandbox_syscall(&proc_obj, 5, (user_addr_t)&bad_issue) == EINVAL);
    const char *empty_str = "";
    bad_issue = issue;
    bad_issue.extension_class = (user_addr_t)empty_str;
    assert(sweet_sandbox_syscall(&proc_obj, 5, (user_addr_t)&bad_issue) == EINVAL);

    /* op 5: copyin / copyout fault propagation. */
    copyin_calls = 0;
    fail_copyin_at = 0;
    assert(sweet_sandbox_syscall(&proc_obj, 5, (user_addr_t)&issue) == EFAULT);
    fail_copyin_at = -1;
    copyin_calls = 0;
    fail_copyin_at = 1;
    assert(sweet_sandbox_syscall(&proc_obj, 5, (user_addr_t)&issue) == EFAULT);
    fail_copyin_at = -1;
    copyout_calls = 0;
    fail_copyout_at = 0;
    assert(sweet_sandbox_syscall(&proc_obj, 5, (user_addr_t)&issue) == EFAULT);
    fail_copyout_at = -1;

    /* op 5: valid file extension issue (first general process). */
    memset(token_buf, 0xaa, sizeof(token_buf));
    unsigned logs_before = log_count;
    assert(sweet_sandbox_syscall(&proc_obj, 5, (user_addr_t)&issue) == 0);
    assert(strcmp(token_buf, "fuxi-ext-2") == 0);
    assert(strcmp(token_buf, "invalid") != 0);
    assert(log_count == logs_before + 1);
    assert(strstr(last_log, "nsurlsessiond op 5 issue") != NULL);

    /* op 5: second general call does not re-log, generic target=0 allowed. */
    struct fuxi_sbext_issue_args generic_issue = issue;
    generic_issue.type = 3;
    generic_issue.target = 0;
    logs_before = log_count;
    assert(sweet_sandbox_syscall(&proc_obj, 5, (user_addr_t)&generic_issue) == 0);
    assert(strcmp(token_buf, "fuxi-ext-3") == 0);
    assert(log_count == logs_before);

    /* op 5: voicememod gets one dedicated log line. */
    cur_proc_name = "voicememod";
    logs_before = log_count;
    assert(sweet_sandbox_syscall(&proc_obj, 5, (user_addr_t)&issue) == 0);
    assert(strcmp(token_buf, "fuxi-ext-4") == 0);
    assert(log_count == logs_before + 1);
    assert(strstr(last_log, "voicememod op 5 issue") != NULL);
    logs_before = log_count;
    assert(sweet_sandbox_syscall(&proc_obj, 5, (user_addr_t)&issue) == 0);
    assert(log_count == logs_before);

    /* op 6: NULL arg, invalid params, empty token, fault propagation. */
    assert(sweet_sandbox_syscall(&proc_obj, 6, 0) == EFAULT);
    struct fuxi_sbext_consume_args bad_consume = consume;
    bad_consume.token = 0;
    assert(sweet_sandbox_syscall(&proc_obj, 6, (user_addr_t)&bad_consume) == EINVAL);
    bad_consume = consume;
    bad_consume.token_len = 0;
    assert(sweet_sandbox_syscall(&proc_obj, 6, (user_addr_t)&bad_consume) == EINVAL);
    bad_consume = consume;
    bad_consume.token_len = FUXI_SBEXT_MAX_TOKEN + 1;
    assert(sweet_sandbox_syscall(&proc_obj, 6, (user_addr_t)&bad_consume) == EINVAL);
    bad_consume = consume;
    bad_consume.out_handle = 0;
    assert(sweet_sandbox_syscall(&proc_obj, 6, (user_addr_t)&bad_consume) == EINVAL);
    bad_consume = consume;
    bad_consume.token = (user_addr_t)empty_str;
    bad_consume.token_len = 1;
    assert(sweet_sandbox_syscall(&proc_obj, 6, (user_addr_t)&bad_consume) == EINVAL);
    copyin_calls = 0;
    fail_copyin_at = 1;
    assert(sweet_sandbox_syscall(&proc_obj, 6, (user_addr_t)&consume) == EFAULT);
    fail_copyin_at = -1;
    copyout_calls = 0;
    fail_copyout_at = 0;
    assert(sweet_sandbox_syscall(&proc_obj, 6, (user_addr_t)&consume) == EFAULT);
    fail_copyout_at = -1;

    /* op 6: valid consume returns monotonic positive handles (!= 0, != -1). */
    cur_proc_name = "com.apple.Stream";
    consume.token_len = strlen(token_buf) + 1;
    handle = -1;
    logs_before = log_count;
    assert(sweet_sandbox_syscall(&proc_obj, 6, (user_addr_t)&consume) == 0);
    assert(handle == 2);
    assert(log_count == logs_before + 1);
    assert(strstr(last_log, "com.apple.Stream op 6 consume") != NULL);

    cur_proc_name = "mediaserverd";
    int64_t handle2 = -1;
    consume.out_handle = (user_addr_t)&handle2;
    logs_before = log_count;
    assert(sweet_sandbox_syscall(&proc_obj, 6, (user_addr_t)&consume) == 0);
    assert(handle2 == 3);
    assert(log_count == logs_before + 1);
    assert(strstr(last_log, "mediaserverd op 6 consume") != NULL);

    /* Boot-arg fuxisbext=1 works even without ADT property. */
    adt_sbext_present = 0;
    fuxi_sbext_bootarg = 1;
    fuxi_sbext_init();
    assert(fuxi_sbext_enabled == 1);
    assert(sweet_sandbox_syscall(&proc_obj, 6, (user_addr_t)&consume) == 0);
    assert(handle2 == 4);

    puts("sandbox-ext model ok");
    return 0;
}
'''


def extract_function(text, name):
    match = re.search(r'(?:static )?(?:void|int)\n' + name + r'\([^;]+?\n\{', text)
    assert match, f'function not found: {name}'
    depth, end = 1, match.end()
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--cc', default='clang')
    args = parser.parse_args()

    assert PATCH_0125.is_file(), f'missing {PATCH_0125}'
    with tempfile.TemporaryDirectory(prefix='fuxi-sbext-test-') as tmp:
        tmp_path = Path(tmp)
        for patch in sorted(PATCH_DIR.glob('*.patch')):
            if patch.name > '0107-fuxi-sys.patch':
                break
            chunks = re.split(r'(?=^--- (?:a/|/dev/null))', patch.read_text(errors='surrogateescape'), flags=re.M)
            for chunk in chunks:
                match = re.search(r'^\+\+\+ b/(.+)$', chunk, re.M)
                if not match or match[1] != 'bsd/kern/sweet_amfi.c':
                    continue
                subprocess.run(['git', 'apply', '--whitespace=nowarn', '-'], input=chunk,
                               text=True, cwd=tmp, check=True)
        subprocess.run(['git', 'apply', '--check', str(PATCH_0125)], cwd=tmp, check=True)
        subprocess.run(['git', 'apply', str(PATCH_0125)], cwd=tmp, check=True)

        amfi = (tmp_path / 'bsd/kern/sweet_amfi.c').read_text()
        header = (tmp_path / 'bsd/kern/fuxi_sbext.h').read_text()
        assert '#include <pexpert/device_tree.h>' in amfi
        assert '#include "fuxi_sbext.h"' in amfi
        assert 'fuxi_sbext_init();' in amfi
        assert 'TUNABLE(uint32_t, fuxi_sbext_bootarg, "fuxisbext", 0)' in header
        assert '"fuxi,sb-ext"' in header

        syscall_fn = extract_function(amfi, 'sweet_sandbox_syscall')
        src = tmp_path / 'test_sbext.c'
        src.write_text(HARNESS.replace('SANDBOX_SYSCALL_FN', syscall_fn))
        binary = tmp_path / 'test_sbext'
        cmd = [args.cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-I', str(tmp_path / 'bsd/kern'), str(src), '-o', str(binary)]
        if args.sanitize:
            cmd.append('-fsanitize=address,undefined')
        subprocess.run(cmd, check=True)
        run = subprocess.run([str(binary)], capture_output=True, text=True)
        assert run.returncode == 0, run.stdout + run.stderr
        assert 'sandbox-ext model ok' in run.stdout
    print('PASS: 0125 sandbox extension issue/consume (patch + ASan/UBSan model)')


if __name__ == '__main__':
    main()
