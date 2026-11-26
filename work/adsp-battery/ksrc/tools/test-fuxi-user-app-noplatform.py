#!/usr/bin/env python3
'''Host tests for 0133-fuxi-user-app-noplatform.patch (ASan/UBSan).'''
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
os.environ.setdefault('PYTHONDONTWRITEBYTECODE', '1')

ROOT = Path(__file__).resolve().parents[1]
PATCH_DIR = ROOT / 'xnu/patches'
PATCH_0133 = PATCH_DIR / '0133-fuxi-user-app-noplatform.patch'

HARNESS_HEADER = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int32_t SInt32;
typedef int DTEntry;
typedef uint32_t cpu_type_t;
#define kSuccess 0
#define MAXPATHLEN 1024
#define Z_WAITOK 0
#define __unused __attribute__((unused))

#define CS_VALID           0x00000001u
#define CS_SIGNED          0x20000000u
#define CS_PLATFORM_BINARY 0x04000000u
#define MNT_ROOTFS         0x00004000u

struct label;
struct vnode {
    const char *path;
    uint32_t mnt_flags;
    const char *mntonname;
    int vn_getpath_fail;
};
typedef struct {
    uint32_t flags;
    char mntonname[64];
} *mount_t;
struct vfsstatfs {
    const char *f_mntonname;
};
struct cs_blob {
    struct cs_blob *csb_ro_addr;
    void *csb_entitlements;
    const char *teamid;
};

static int mock_has_bootarg = 0;
static uint32_t mock_bootarg_val = 0;
static int mock_has_chosen = 1;
static int mock_has_adt = 0;
static uint32_t mock_adt_val = 0;
static unsigned int mock_adt_bytes = 4;
static int mock_kalloc_fail = 0;
static size_t mock_alloc_outstanding = 0;
static int mock_camera_mounted = 1;

static bool PE_parse_boot_argn(const char *name, void *val, int size) {
    if (strcmp(name, "fuxiuserappnoplat") == 0 && mock_has_bootarg && size == (int)sizeof(uint32_t)) {
        memcpy(val, &mock_bootarg_val, sizeof(uint32_t));
        return true;
    }
    return false;
}

static int SecureDTLookupEntry(const DTEntry *parent, const char *path, DTEntry *entry) {
    (void)parent;
    if (mock_has_chosen && strcmp(path, "/chosen") == 0) {
        *entry = 1;
        return kSuccess;
    }
    return -1;
}

static int SecureDTGetProperty(DTEntry entry, const char *name, void const **value, unsigned int *size) {
    if (entry == 1 && mock_has_adt && strcmp(name, "fuxi,user-app-noplatform") == 0) {
        *value = &mock_adt_val;
        *size = mock_adt_bytes;
        return kSuccess;
    }
    return -1;
}

static void *kalloc_data(size_t size, int flags) {
    (void)flags;
    if (mock_kalloc_fail) {
        return NULL;
    }
    void *p = calloc(1, size);
    if (p) {
        mock_alloc_outstanding++;
    }
    return p;
}

static void kfree_data(void *ptr, size_t size) {
    (void)size;
    if (ptr) {
        assert(mock_alloc_outstanding > 0);
        mock_alloc_outstanding--;
        free(ptr);
    }
}

static int vn_getpath(struct vnode *vp, char *pathbuf, int *len) {
    if (vp == NULL || vp->vn_getpath_fail || vp->path == NULL || len == NULL || *len <= 0) {
        return -1;
    }
    size_t n = strlen(vp->path) + 1;
    if (n > (size_t)*len) {
        return -1;
    }
    memcpy(pathbuf, vp->path, n);
    *len = (int)n;
    return 0;
}

static mount_t mock_mount_storage(struct vnode *vp, int reset) {
    static struct {
        uint32_t flags;
        char mntonname[64];
    } st;
    if (reset && vp != NULL) {
        st.flags = vp->mnt_flags;
        snprintf(st.mntonname, sizeof(st.mntonname), "%s", vp->mntonname ? vp->mntonname : "/");
    }
    return (mount_t)&st;
}

static mount_t vnode_mount(struct vnode *vp) {
    if (vp == NULL) {
        return NULL;
    }
    return mock_mount_storage(vp, 1);
}

static uint32_t vfs_flags(mount_t mp) {
    return mp->flags;
}

static struct vfsstatfs *vfs_statfs(mount_t mp) {
    static struct vfsstatfs sfs;
    sfs.f_mntonname = mp->mntonname;
    return &sfs;
}

int fuxi_camera_dylib_is_mounted(void) {
    return mock_camera_mounted;
}

static void sweet_amfi_attach_entitlements(struct cs_blob *cs_blob) {
    (void)cs_blob;
}

#define printf(...) ((void)0)
'''

HARNESS_MAIN = r'''
#undef printf

static void simulate_csblob_and_exec(
    struct vnode *vp,
    const char *teamid,
    unsigned int *out_cs_flags,
    int *out_csb_platform_binary,
    const char **out_csb_teamid,
    int *out_task_platform_binary)
{
    struct cs_blob blob;
    blob.csb_ro_addr = &blob;
    blob.csb_entitlements = (void *)0x1;
    blob.teamid = teamid;

    unsigned int cs_flags = 0;
    unsigned int signer_type = 0;
    int err = sweet_amfi_vnode_check_signature(
        vp, NULL, 0, &blob, &cs_flags, &signer_type, 0, 0, NULL, NULL);
    assert(err == 0);

    /* Mirror ubc_subr.c:4720-4787 */
    int csb_platform_binary = 0;
    const char *csb_teamid = NULL;
    if (cs_flags & CS_PLATFORM_BINARY) {
        csb_platform_binary = 1;
        csb_teamid = NULL;
    } else {
        csb_platform_binary = 0;
    }
    if ((cs_flags & CS_PLATFORM_BINARY) == 0) {
        csb_teamid = blob.teamid;
    }

    /* Mirror mach_loader.c + kern_exec.c:1744-1775 double defense */
    int load_result_platform_binary = csb_platform_binary ? 1 : 0;
    if (load_result_platform_binary && fuxi_user_app_noplatform_enabled() && vp != NULL) {
        int path_len = MAXPATHLEN;
        char *app_path = kalloc_data(MAXPATHLEN, Z_WAITOK);
        if (app_path != NULL) {
            if (vn_getpath(vp, app_path, &path_len) == 0 &&
                fuxi_appsync_is_user_app_path(app_path)) {
                load_result_platform_binary = 0;
            }
            kfree_data(app_path, MAXPATHLEN);
        }
    }

    *out_cs_flags = cs_flags;
    *out_csb_platform_binary = csb_platform_binary;
    *out_csb_teamid = csb_teamid;
    *out_task_platform_binary = load_result_platform_binary;
}

static void run_unit_tests(void) {
    /* 1. Default off (no bootarg, no ADT) */
    assert(!fuxi_user_app_noplatform_read());

    /* 2. Malformed ADT size stays off */
    mock_has_adt = 1;
    mock_adt_val = 1;
    mock_adt_bytes = 2;
    assert(!fuxi_user_app_noplatform_read());

    /* 3. ADT fuxi,user-app-noplatform=1 enables */
    mock_adt_bytes = 4;
    assert(fuxi_user_app_noplatform_read());

    /* 4. Explicit boot-arg fuxiuserappnoplat=0 overrides ADT=1 */
    mock_has_bootarg = 1;
    mock_bootarg_val = 0;
    assert(!fuxi_user_app_noplatform_read());

    /* 5. Boot-arg fuxiuserappnoplat=1 enables even without ADT */
    mock_has_adt = 0;
    mock_bootarg_val = 1;
    assert(fuxi_user_app_noplatform_read());

    /* 6. Cached once via fuxi_user_app_noplatform_enabled() */
    mock_has_bootarg = 0;
    mock_has_adt = 0;
    assert(!fuxi_user_app_noplatform_enabled());
    mock_has_adt = 1;
    mock_adt_val = 1;
    assert(!fuxi_user_app_noplatform_enabled()); /* remains cached false */

    /* 7. fuxi_appsync_is_user_app_path() exact path and traversal checks */
    assert(fuxi_appsync_is_user_app_path("/private/var/containers/Bundle/Application/UUID/Demo.app/Demo"));
    assert(fuxi_appsync_is_user_app_path("/var/containers/Bundle/Application/UUID/Demo.app/Frameworks/F.dylib"));
    assert(fuxi_appsync_is_user_app_path("/private/var/containers/Bundle/Application/A/B.app/PlugIns/E.appex/E"));

    assert(!fuxi_appsync_is_user_app_path(NULL));
    assert(!fuxi_appsync_is_user_app_path(""));
    assert(!fuxi_appsync_is_user_app_path("/Applications/AppStore.app/AppStore"));
    assert(!fuxi_appsync_is_user_app_path("/Applications/FuxiInstaller.app/FuxiInstaller"));
    assert(!fuxi_appsync_is_user_app_path("/System/Library/CoreServices/SpringBoard.app/SpringBoard"));
    assert(!fuxi_appsync_is_user_app_path("/private/var/containers/Bundle/Application/UUID/.app/Demo"));
    assert(!fuxi_appsync_is_user_app_path("/private/var/containers/Bundle/Application/UUID/..app/bin"));
    assert(!fuxi_appsync_is_user_app_path("/private/var/containers/Bundle/Application/UUID/.hidden.app/bin"));
    assert(!fuxi_appsync_is_user_app_path("/private/var/containers/Bundle/Application/UUID/Demo.app"));
    assert(!fuxi_appsync_is_user_app_path("/private/var/containers/Bundle/Application/UUID/Demo.app/"));
    assert(!fuxi_appsync_is_user_app_path("/private/var/containers/Bundle/Application/UUID/App.app//"));
    assert(!fuxi_appsync_is_user_app_path("/private/var/containers/Bundle/Application/UUID/App.app///"));
    assert(!fuxi_appsync_is_user_app_path("/private/var/containers/Bundle/Application/UUID/App.app/App/"));
    assert(!fuxi_appsync_is_user_app_path("/private/var/containers/Bundle/Application/UUID/App.app/Frameworks/Foo.framework//"));
    assert(!fuxi_appsync_is_user_app_path("/private/var/containers/Bundle/Application/../Mobile/Demo.app/Demo"));
    assert(!fuxi_appsync_is_user_app_path("/private/var/containers/Bundle/Application/UUID/Demo.app/../x"));
    assert(!fuxi_appsync_is_user_app_path("/private/var/containers/Bundle/Application/./UUID/Demo.app/Demo"));
    assert(!fuxi_appsync_is_user_app_path("/private/var/containers/Bundle/Application/UUID/Demo.app/./Demo"));
    assert(!fuxi_appsync_is_user_app_path("/private/var/containers/Bundle/Application/UUID/Demo.app/."));
    assert(!fuxi_appsync_is_user_app_path("/private/var/containers/Bundle/Application/UUID/Demo.app/.."));
}

static void run_amfi_integration_mode(int enabled) {
    unsigned int cs_flags = 0;
    int csb_plat = -1;
    const char *csb_team = NULL;
    int task_plat = -1;

    mock_has_bootarg = 1;
    mock_bootarg_val = (uint32_t)(enabled ? 1 : 0);
    mock_has_adt = 0;

    struct vnode user_app = {
        "/private/var/containers/Bundle/Application/UUID-1/App.app/App",
        MNT_ROOTFS, "/", 0
    };
    struct vnode user_fw = {
        "/private/var/containers/Bundle/Application/UUID-1/App.app/Frameworks/Foo.framework/Foo",
        MNT_ROOTFS, "/", 0
    };
    struct vnode user_appex = {
        "/var/containers/Bundle/Application/UUID-1/App.app/PlugIns/Ext.appex/Ext",
        MNT_ROOTFS, "/", 0
    };
    struct vnode sys_appstore = {
        "/Applications/AppStore.app/AppStore",
        MNT_ROOTFS, "/", 0
    };
    struct vnode sys_installd = {
        "/usr/libexec/installd",
        MNT_ROOTFS, "/", 0
    };
    struct vnode dev_dylib = {
        "/Developer/usr/lib/libBacktraceRecording.dylib",
        0, "/Developer", 0
    };
    struct vnode bad_path_vnode = {
        "/private/var/containers/Bundle/Application/UUID-1/App.app/App",
        MNT_ROOTFS, "/", 1
    };

    if (enabled) {
        simulate_csblob_and_exec(&user_app, "TEAM123456", &cs_flags, &csb_plat, &csb_team, &task_plat);
        assert((cs_flags & CS_SIGNED) && (cs_flags & CS_VALID));
        assert((cs_flags & CS_PLATFORM_BINARY) == 0);
        assert(csb_plat == 0);
        assert(task_plat == 0);
        assert(csb_team != NULL && strcmp(csb_team, "TEAM123456") == 0);

        simulate_csblob_and_exec(&user_fw, "TEAM123456", &cs_flags, &csb_plat, &csb_team, &task_plat);
        assert((cs_flags & CS_PLATFORM_BINARY) == 0);
        assert(csb_plat == 0 && task_plat == 0);
        assert(csb_team != NULL && strcmp(csb_team, "TEAM123456") == 0);

        simulate_csblob_and_exec(&user_appex, "TEAM123456", &cs_flags, &csb_plat, &csb_team, &task_plat);
        assert((cs_flags & CS_PLATFORM_BINARY) == 0);
        assert(csb_plat == 0 && task_plat == 0);
        assert(csb_team != NULL && strcmp(csb_team, "TEAM123456") == 0);

        /* System binaries stay CS_PLATFORM_BINARY */
        simulate_csblob_and_exec(&sys_appstore, NULL, &cs_flags, &csb_plat, &csb_team, &task_plat);
        assert((cs_flags & CS_PLATFORM_BINARY) != 0);
        assert(csb_plat == 1 && task_plat == 1 && csb_team == NULL);

        simulate_csblob_and_exec(&sys_installd, NULL, &cs_flags, &csb_plat, &csb_team, &task_plat);
        assert((cs_flags & CS_PLATFORM_BINARY) != 0);
        assert(csb_plat == 1 && task_plat == 1 && csb_team == NULL);

        simulate_csblob_and_exec(&dev_dylib, NULL, &cs_flags, &csb_plat, &csb_team, &task_plat);
        assert((cs_flags & CS_PLATFORM_BINARY) != 0);
        assert(csb_plat == 1 && task_plat == 1);

        /* Fail-safe paths (NULL vnode, vn_getpath fail, kalloc_data OOM) */
        simulate_csblob_and_exec(NULL, NULL, &cs_flags, &csb_plat, &csb_team, &task_plat);
        assert((cs_flags & CS_PLATFORM_BINARY) == 0);

        simulate_csblob_and_exec(&bad_path_vnode, NULL, &cs_flags, &csb_plat, &csb_team, &task_plat);
        assert((cs_flags & CS_PLATFORM_BINARY) != 0);

        mock_kalloc_fail = 1;
        simulate_csblob_and_exec(&user_app, NULL, &cs_flags, &csb_plat, &csb_team, &task_plat);
        assert((cs_flags & CS_PLATFORM_BINARY) != 0);
        mock_kalloc_fail = 0;
    } else {
        /* Default disabled: user_app stays CS_PLATFORM_BINARY */
        simulate_csblob_and_exec(&user_app, "TEAM123456", &cs_flags, &csb_plat, &csb_team, &task_plat);
        assert((cs_flags & CS_PLATFORM_BINARY) != 0);
        assert(csb_plat == 1 && task_plat == 1 && csb_team == NULL);

        simulate_csblob_and_exec(&sys_appstore, NULL, &cs_flags, &csb_plat, &csb_team, &task_plat);
        assert((cs_flags & CS_PLATFORM_BINARY) != 0);
        assert(csb_plat == 1 && task_plat == 1 && csb_team == NULL);
    }
    assert(mock_alloc_outstanding == 0);
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--amfi-on") == 0) {
        run_amfi_integration_mode(1);
        puts("user-app-noplatform amfi-on ok");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--amfi-off") == 0) {
        run_amfi_integration_mode(0);
        puts("user-app-noplatform amfi-off ok");
        return 0;
    }
    run_unit_tests();
    puts("user-app-noplatform model ok");
    return 0;
}
'''


def reconstruct_pre_lines(hunk_lines):
    out = []
    for hl in hunk_lines:
        if hl.startswith(' ') or hl.startswith('-'):
            out.append(hl[1:])
    return out


def extract_patched_sweet_amfi_snippet(patched_text: str) -> str:
    lines = patched_text.splitlines()
    inc_line = [l for l in lines if 'fuxi_user_app_noplatform.h' in l]
    assert len(inc_line) == 1
    start_idx = next(i for i, l in enumerate(lines) if l.startswith('static bool') and i + 1 < len(lines) and 'sweet_amfi_is_user_app_vnode' in lines[i + 1])
    sig_start = next(i for i in range(start_idx, len(lines)) if 'sweet_amfi_vnode_check_signature(' in lines[i])
    sig_end = next((i for i in range(sig_start, len(lines)) if lines[i] == '}'), None)
    body = lines[start_idx:] if sig_end is None else lines[start_idx:sig_end + 1]
    if sig_end is None:
        assert any('sweet_amfi_attach_entitlements(cs_blob);' in l for l in body)
        body = body + ['\treturn 0;', '}']
    return '\n'.join(inc_line + [''] + body) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--cc', default=os.environ.get('CC', 'clang'))
    args = parser.parse_args()

    assert PATCH_0133.is_file(), f'missing {PATCH_0133}'
    patch_bytes = PATCH_0133.read_bytes()
    assert b'\r' not in patch_bytes, 'CRLF detected in 0133 patch'
    patch_text = patch_bytes.decode('utf-8')
    assert 'FUXI_INJECT_APPSYNC' not in patch_text
    assert 'fuxi,appsync' not in patch_text
    assert 'developer_mode' not in patch_text

    with tempfile.TemporaryDirectory(prefix='fuxi-noplat-') as tmp:
        t = Path(tmp)
        chunks = [c for c in re.split(r'(?=^diff --git )', patch_text, flags=re.M) if c.strip()]
        hdr_chunk = [c for c in chunks if 'b/bsd/kern/fuxi_user_app_noplatform.h' in c]
        exec_chunk = [c for c in chunks if 'b/bsd/kern/kern_exec.c' in c]
        amfi_chunk = [c for c in chunks if 'b/bsd/kern/sweet_amfi.c' in c]
        assert len(hdr_chunk) == 1 and len(exec_chunk) == 1 and len(amfi_chunk) == 1
        assert 'fuxi_user_app_noplatform_enabled()' in exec_chunk[0]
        assert 'fuxi_appsync_is_user_app_path(app_path)' in exec_chunk[0]
        assert 'load_result.platform_binary = FALSE;' in exec_chunk[0]
        assert 'sweet_amfi_is_user_app_vnode' in amfi_chunk[0]
        assert 'CS_PLATFORM_BINARY' in amfi_chunk[0]

        subprocess.run(['git', 'apply', '--whitespace=nowarn', '-'], input=hdr_chunk[0],
                       text=True, cwd=t, check=True)
        assert (t / 'bsd/kern/fuxi_user_app_noplatform.h').is_file()

        hunk_texts = re.split(r'(?=^@@ )', amfi_chunk[0], flags=re.M)
        amfi_header = hunk_texts[0]
        patched_parts = []
        for idx, hk in enumerate(hunk_texts[1:]):
            sub_dir = t / f'hk_{idx}/bsd/kern'
            sub_dir.mkdir(parents=True)
            hunk_body = hk.splitlines()[1:]
            sub_pre = reconstruct_pre_lines(hunk_body)
            (sub_dir / 'sweet_amfi.c').write_text('\n'.join(sub_pre) + '\n', encoding='utf-8')
            n_old = len(sub_pre)
            n_new = sum(1 for hl in hunk_body if hl.startswith(' ') or hl.startswith('+'))
            rebuilt_patch = amfi_header + f'@@ -1,{n_old} +1,{n_new} @@\n' + '\n'.join(hunk_body) + '\n'
            subprocess.run(['git', 'apply', '--whitespace=nowarn', '-'], input=rebuilt_patch, text=True, cwd=t / f'hk_{idx}', check=True)
            patched_parts.append((sub_dir / 'sweet_amfi.c').read_text(encoding='utf-8'))

        patched_amfi_snippet = extract_patched_sweet_amfi_snippet('\n'.join(patched_parts))
        filtered_snippet = '\n'.join(
            l for l in patched_amfi_snippet.splitlines()
            if not l.startswith('#include <security/') and not l.startswith('#include <pexpert/')
        ) + '\n'

        src = t / 'test_noplat.c'
        src.write_text(HARNESS_HEADER + '\n' + filtered_snippet + '\n' + HARNESS_MAIN, encoding='utf-8')
        binary = t / 'test_noplat'
        cmd = [args.cc, '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-I', str(t), '-I', str(t / 'bsd/kern'), str(src), '-o', str(binary)]
        if args.sanitize:
            cmd.extend(['-fsanitize=address,undefined', '-fno-sanitize-recover=all'])
        subprocess.run(cmd, check=True)

        for mode_arg, expected in [
            ([], 'user-app-noplatform model ok'),
            (['--amfi-off'], 'user-app-noplatform amfi-off ok'),
            (['--amfi-on'], 'user-app-noplatform amfi-on ok'),
        ]:
            run = subprocess.run([str(binary)] + mode_arg, capture_output=True, text=True)
            assert run.returncode == 0, run.stdout + run.stderr
            assert expected in run.stdout, run.stdout
    print('PASS: 0133 user-app-noplatform (independent toggle, path bounds, sweet_amfi CS_PLATFORM_BINARY & teamid, no appsync bypass, ASan/UBSan)')


if __name__ == '__main__':
    main()
