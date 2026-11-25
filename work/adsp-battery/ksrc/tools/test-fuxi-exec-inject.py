import os
from pathlib import Path
import subprocess
import tempfile


root = Path(__file__).resolve().parents[1]
source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "fuxi_exec_inject.h"

static int same(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}

int main(void)
{
    const char *sb = "/System/Library/CoreServices/SpringBoard.app/SpringBoard";
    const char *media = "/usr/sbin/mediaserverd";
    const char *wifi = "/usr/sbin/wifid";
    const char *settings = "/Applications/Preferences.app/Preferences";
    const char *app = "/Applications/MobileSafari.app/MobileSafari";
    const char *bluetool = "/usr/sbin/BlueTool";
    const char *bluetoothd = "/usr/sbin/bluetoothd";
    const char *vm = "/Applications/VoiceMemos.app/VoiceMemos";
    const char *vmd = "/System/Library/PrivateFrameworks/VoiceMemos.framework/Support/voicememod";
    const char *timed = "/usr/libexec/timed";
    const char *asstd = "/System/Library/PrivateFrameworks/AssistantServices.framework/assistantd";
    const char *assvc = "/System/Library/PrivateFrameworks/AssistantServices.framework/assistant_service";
    const char *csd = "/System/Library/PrivateFrameworks/CoreSpeech.framework/corespeechd";
    const char *kbd = "/System/Library/TextInput/kbd";
    char env[FUXI_INJECT_ENV_MAX];

    assert(FUXI_INJECT_ALL == 1023u);
    for (unsigned mode = 0; mode <= FUXI_INJECT_ALL; mode++) {
        assert(same(fuxi_inject_library(mode, sb, "SpringBoard"), (mode & 2u) ? FUXI_INJECT_SB_LIB : NULL));
        assert(same(fuxi_inject_library(mode, media, "mediaserverd"), (mode & 4u) ? FUXI_INJECT_SB_LIB : NULL));
        assert(same(fuxi_inject_library(mode, wifi, "wifid"), (mode & 8u) ? FUXI_INJECT_SB_LIB : NULL));
        assert(!fuxi_inject_library(mode, "/var/tmp/wifid", "wifid"));
        assert(!fuxi_inject_library(mode, "/usr/sbin/wifid2", "wifid2"));
        assert(!fuxi_inject_library(mode, wifi, NULL));
        assert(same(fuxi_inject_library(mode, app, "MobileSafari"), (mode & 1u) ? FUXI_INJECT_APP_LIB : NULL));
        const char *settings_library = (mode & 1u) ? FUXI_INJECT_APP_LIB : NULL;
        if (mode & 16u) settings_library = FUXI_INJECT_SB_LIB;
        assert(same(fuxi_inject_library(mode, settings, "Preferences"), settings_library));
        assert(!fuxi_inject_library(mode, "/var/tmp/Preferences", "Preferences"));
        /* 別處同名的 mediaserverd 不注入 */
        assert(!fuxi_inject_library(mode, "/var/tmp/mediaserverd", "mediaserverd"));
        assert(!fuxi_inject_library(mode, "/usr/sbin/mediaserverd2", "mediaserverd2"));
        assert(!fuxi_inject_library(mode, "/usr/libexec/audiomxd", "audiomxd"));
        assert(!fuxi_inject_library(mode, NULL, "mediaserverd"));
        assert(!fuxi_inject_library(mode, media, NULL));
        /* BlueTool 替身與 bluetoothd 只認原廠路徑 */
        assert(same(fuxi_inject_library(mode, bluetool, "BlueTool"), (mode & 32u) ? FUXI_INJECT_SB_LIB : NULL));
        assert(same(fuxi_inject_library(mode, bluetoothd, "bluetoothd"), (mode & 64u) ? FUXI_INJECT_SB_LIB : NULL));
        assert(!fuxi_inject_library(mode, "/var/tmp/BlueTool", "BlueTool"));
        assert(!fuxi_inject_library(mode, "/usr/sbin/BlueTool2", "BlueTool2"));
        assert(!fuxi_inject_library(mode, "/usr/libexec/bluetoothd", "bluetoothd"));
        assert(!fuxi_inject_library(mode, "/usr/sbin/bluetoothuserd", "bluetoothuserd"));
        const char *vm_library = (mode & 1u) ? FUXI_INJECT_APP_LIB : NULL;
        if (mode & 128u) vm_library = FUXI_INJECT_SB_LIB;
        assert(same(fuxi_inject_library(mode, vm, "VoiceMemos"), vm_library));
        assert(same(fuxi_inject_library(mode, vmd, "voicememod"), (mode & 128u) ? FUXI_INJECT_SB_LIB : NULL));
        assert(!fuxi_inject_library(mode, "/var/tmp/VoiceMemos", "VoiceMemos"));
        assert(!fuxi_inject_library(mode, "/var/tmp/voicememod", "voicememod"));
        /* timed 只認原廠路徑 /usr/libexec/timed */
        assert(same(fuxi_inject_library(mode, timed, "timed"), (mode & 256u) ? FUXI_INJECT_SB_LIB : NULL));
        assert(!fuxi_inject_library(mode, "/var/tmp/timed", "timed"));
        assert(!fuxi_inject_library(mode, "/usr/sbin/timed", "timed"));
        assert(!fuxi_inject_library(mode, "/usr/libexec/timed2", "timed2"));
        assert(!fuxi_inject_library(mode, timed, NULL));
        /* assistantd / assistant_service / corespeechd / kbd 只認原廠路徑 (bit 9 = 512) */
        assert(same(fuxi_inject_library(mode, asstd, "assistantd"), (mode & 512u) ? FUXI_INJECT_SB_LIB : NULL));
        assert(same(fuxi_inject_library(mode, assvc, "assistant_service"), (mode & 512u) ? FUXI_INJECT_SB_LIB : NULL));
        assert(same(fuxi_inject_library(mode, csd, "corespeechd"), (mode & 512u) ? FUXI_INJECT_SB_LIB : NULL));
        assert(same(fuxi_inject_library(mode, kbd, "kbd"), (mode & 512u) ? FUXI_INJECT_SB_LIB : NULL));
        assert(!fuxi_inject_library(mode, "/var/tmp/assistantd", "assistantd"));
        assert(!fuxi_inject_library(mode, "/var/tmp/assistant_service", "assistant_service"));
        assert(!fuxi_inject_library(mode, "/var/tmp/corespeechd", "corespeechd"));
        assert(!fuxi_inject_library(mode, "/var/tmp/kbd", "kbd"));
        assert(!fuxi_inject_library(mode, asstd, NULL));
    }
    /* SpringBoard 不會拿到 app 函式庫，.appex 與 app 內 helper 不算 app */
    assert(same(fuxi_inject_library(1u, sb, "SpringBoard"), NULL));
    assert(!fuxi_inject_library(1u, "/Applications/X.app/PlugIns/Y.appex/Y", "Y"));
    assert(!fuxi_inject_library(1u, "/Applications/X.app/Helpers/H", "H"));
    assert(!fuxi_inject_library(1u, "/.app/X", "X"));
    assert(fuxi_inject_env(FUXI_INJECT_SB_LIB, env, sizeof(env)));
    assert(!strcmp(env, "DYLD_INSERT_LIBRARIES=" FUXI_INJECT_SB_LIB));
    assert(!fuxi_inject_env(NULL, env, sizeof(env)));
    assert(!fuxi_inject_env(FUXI_INJECT_SB_LIB, env, 10));
    assert(!fuxi_inject_env(FUXI_INJECT_SB_LIB, env, 0));
    assert(!fuxi_inject_library(16u, "/Applications/Other.app/Preferences", "Preferences"));
    puts("PASS: exec injection modes 0..1023, exact Settings/BlueTool/bluetoothd/VoiceMemos/timed/dictation executables, other app modes and environment bounds");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='fuxi-exec-inject-') as temporary:
    directory = Path(temporary)
    cfile, binary = directory / 'inject.c', directory / 'inject'
    cfile.write_text(source, encoding='utf-8')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(root / 'xnu/fuxi-inject'), str(cfile), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
